/*
 * FPP "pixelpulse" plugin  -  live audio-reactive lighting (FPP 5.4 - 9.x)
 *
 * Captures live audio from a USB sound card (ALSA), analyzes it in real time
 * (level / frequency bands / beat / bpm via AudioAnalyzer), and modulates the
 * channel data each frame so the lights react to the room's audio.
 *
 * Phase 1 mappings:
 *   - level  -> brightness   (the design pulses with loudness)
 *   - beat   -> white flash   (a punch on each beat)
 *
 * Like pixelfx: a ChannelData modifier, never touches test patterns, optional
 * "only while a sequence is playing", settings re-read ~2x/sec so app/UI changes
 * apply live. Writes /dev/shm/pixelpulse_status.json for the settings-page meters.
 */
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <string>
#include <vector>

#include <sys/stat.h>

#include "Plugin.h"
#include "Sequence.h"
#include "channeltester/ChannelTester.h"

#include "AlsaCapture.h"
#include "AudioAnalyzer.h"

// FPP playback-rate control (channeloutputthread.cpp). Declared here so we don't
// pull the whole channeloutput header; resolved against fppd at load.
void SetChannelOutputRefreshRate(float rate);
float GetChannelOutputRefreshRate();

namespace {
long toLong(const std::string& v, long d) {
    if (v.empty()) return d;
    char* e = nullptr;
    long r = std::strtol(v.c_str(), &e, 10);
    return e == v.c_str() ? d : r;
}
double toDouble(const std::string& v, double d) {
    if (v.empty()) return d;
    char* e = nullptr;
    double r = std::strtod(v.c_str(), &e);
    return e == v.c_str() ? d : r;
}
inline uint8_t clamp8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : (uint8_t)v); }

void hsv2rgb(double h, double s, double v, uint8_t& R, uint8_t& G, uint8_t& B) {
    h = std::fmod(h, 360.0); if (h < 0) h += 360.0;
    double c = v * s, x = c * (1 - std::fabs(std::fmod(h / 60.0, 2.0) - 1)), m = v - c, r = 0, g = 0, b = 0;
    if (h < 60) { r = c; g = x; } else if (h < 120) { r = x; g = c; }
    else if (h < 180) { g = c; b = x; } else if (h < 240) { g = x; b = c; }
    else if (h < 300) { r = x; b = c; } else { r = c; b = x; }
    R = clamp8((int)std::lround((r + m) * 255)); G = clamp8((int)std::lround((g + m) * 255)); B = clamp8((int)std::lround((b + m) * 255));
}
void rgb2hsv(uint8_t R, uint8_t G, uint8_t B, double& h, double& s, double& v) {
    double r = R / 255.0, g = G / 255.0, b = B / 255.0;
    double mx = std::max({r, g, b}), mn = std::min({r, g, b}), d = mx - mn;
    v = mx; s = mx <= 0 ? 0 : d / mx;
    if (d <= 0) { h = 0; return; }
    if (mx == r) h = 60 * std::fmod((g - b) / d, 6.0);
    else if (mx == g) h = 60 * (((b - r) / d) + 2);
    else h = 60 * (((r - g) / d) + 4);
    if (h < 0) h += 360;
}
// One prop from the parsed xLights layout: its absolute channel range plus a
// normalized position (0..1 within the display bounds) and distance from the
// display centre. Written by uploadlayout.php, read here for spatial reactions.
struct LayoutProp {
    long start;   // absolute 1-based start channel
    long count;   // channel count
    int step;     // channels per node (3 RGB / 4 RGBW)
    float nx, ny, nz, dist;
};
const char* const kLayoutPath = "/home/fpp/media/config/pixelpulse_layout.txt";
}  // namespace

class AudioFxPlugin : public FPPPlugin {
public:
    AudioFxPlugin() : FPPPlugin("pixelpulse") {
        mLastReload = std::chrono::steady_clock::now();
        mLastStatus = mLastReload;
        mLastFrame = mLastReload;
        applySettings();
        loadLayoutIfChanged();
        mAnalyzer.configure(mSampleRate, 1024, 8);
        restartCapture();
    }
    ~AudioFxPlugin() override { mCapture.stop(); }

    void modifyChannelData(int /*ms*/, uint8_t* d) override {
        maybeReload();
        writeStatus();
        if (!mEnabled || d == nullptr) return;
        if (!shouldModify()) return;

        applySpeed();  // Phase 3: audio -> playback speed (light-only sequences)

        // Phase 4: layout-aware spatial reactions. When a parsed xLights layout
        // is loaded, the effect is driven by each prop's real-world position, so
        // it renders the whole display (and takes over from the range pipeline).
        if (mSpatialEnabled && !mLayout.empty()) {
            auto now = std::chrono::steady_clock::now();
            float dt = std::chrono::duration<float>(now - mLastFrame).count();
            mLastFrame = now;
            if (dt < 0.f) dt = 0.f;
            if (dt > 0.2f) dt = 0.2f;
            applySpatial(d, dt);
            return;
        }

        long si, by;
        if (!range(si, by)) return;
        const long cpp = mChPerPix;
        const long pixels = (by >= 3) ? ((by - 3) / cpp + 1) : 0;

        const float level = mAnalyzer.level();
        const float beat = mAnalyzer.beat();
        const bool active = mAnalyzer.active();

        // Phase 2: direct visualizer (overwrites the range) and hue drive.
        if (mVisMode != 0) applyVisualizer(d, si, by, pixels, cpp);
        if (mHuEnabled && active) applyHueDrive(d, si, pixels, cpp);

        // level -> brightness: scale the range by floor + (1-floor)*level.
        // When silent, pass through (scale 1) so it doesn't sit dim.
        if (mBrEnabled && active) {
            float floor = mBrMin / 100.f;
            float scale = floor + (1.f - floor) * level;
            for (long i = si; i < si + by; ++i)
                d[i] = (uint8_t)((int)d[i] * (int)std::lround(scale * 256) / 256);
        }

        // beat -> white flash overlaid on the pixels (scaled by beat envelope).
        if (mFlEnabled && beat > 0.04f) {
            int w = (int)std::lround(beat * (mFlIntensity / 100.f) * 255.f);
            if (w > 0) {
                for (long p = 0; p < pixels; ++p) {
                    long i = si + p * cpp;
                    d[i] = std::max<uint8_t>(d[i], (uint8_t)w);
                    d[i + 1] = std::max<uint8_t>(d[i + 1], (uint8_t)w);
                    d[i + 2] = std::max<uint8_t>(d[i + 2], (uint8_t)w);
                }
            }
        }
    }

private:
    std::string cfg(const std::string& k) const {
        auto it = settings.find(k);
        return it == settings.end() ? std::string() : it->second;
    }

    // Phase 2: spectrum/VU visualizer - generates pixels from the audio.
    void applyVisualizer(uint8_t* d, long si, long by, long pixels, long cpp) {
        if (pixels < 1) return;
        const int nb = mAnalyzer.numBands();
        if (mVisMode == 1) {  // VU: fill from start, length = level
            std::memset(d + si, 0, by);
            long lit = (long)std::lround(mAnalyzer.level() * pixels);
            for (long p = 0; p < lit; ++p) {
                double frac = (double)p / std::max<long>(1, pixels - 1);  // green->red
                uint8_t r, g, b; hsv2rgb(120.0 * (1.0 - frac), 1.0, 1.0, r, g, b);
                long i = si + p * cpp; d[i] = r; d[i + 1] = g; d[i + 2] = b;
            }
        } else {  // spectrum: each pixel sampled from the band at its position
            for (long p = 0; p < pixels; ++p) {
                int bi = (int)((double)p * nb / pixels);
                if (bi >= nb) bi = nb - 1;
                float e = mAnalyzer.band(bi);
                uint8_t r, g, b; hsv2rgb(280.0 * (double)bi / std::max(1, nb - 1), 1.0, e, r, g, b);
                long i = si + p * cpp; d[i] = r; d[i + 1] = g; d[i + 2] = b;
            }
        }
    }

    // Phase 3: modulate FPP's playback rate from the audio. Only while a
    // sequence is running, and meaningful for LIGHT-ONLY sequences (audio-backed
    // ones are slaved to their media clock). Self-correcting: mult 1.0 restores
    // the native rate, so turning it off returns to normal speed.
    void applySpeed(int /*unused*/ = 0) {
        if (sequence == nullptr || !sequence->IsSequenceRunning()) return;
        int st = sequence->GetSeqStepTime();
        if (st <= 0) return;
        float native = 1000.0f / st;
        float mult = 1.0f;
        if (mSpeedMode != 0) {
            float a = (mSpeedMode == 2) ? mAnalyzer.beat() : mAnalyzer.level();
            float target = 1.0f + a * (float)mSpeedAmount / 100.0f;
            mSpeedMult += (target - mSpeedMult) * 0.15f;  // smooth to avoid jitter
            mult = mSpeedMult;
        } else {
            mSpeedMult = 1.0f;
        }
        float rate = std::max(1.0f, std::min(120.0f, native * mult));
        if (std::fabs(GetChannelOutputRefreshRate() - rate) > 0.05f)
            SetChannelOutputRefreshRate(rate);
    }

    // Phase 2: rotate hue of lit pixels by the spectral balance (bass<->treble).
    void applyHueDrive(uint8_t* d, long si, long pixels, long cpp) {
        double shift = (mAnalyzer.treble() - mAnalyzer.bass()) * mHuAmount;
        if (std::fabs(shift) < 0.5) return;
        for (long p = 0; p < pixels; ++p) {
            long i = si + p * cpp;
            double h, s, v; rgb2hsv(d[i], d[i + 1], d[i + 2], h, s, v);
            if (v <= 0.0) continue;
            hsv2rgb(h + shift, s, v, d[i], d[i + 1], d[i + 2]);
        }
    }
    // Phase 4: drive each prop by its physical position in the display.
    //   1 bloom    - beats radiate a shockwave outward from the centre
    //   2 spectrum - frequency mapped across X (bass left -> treble right)
    //   3 vu       - loudness fills the display bottom-to-top by height
    //   4 radial   - frequency mapped centre-out (bass middle -> treble edge)
    void applySpatial(uint8_t* d, float dt) {
        const long cap = (long)FPPD_MAX_CHANNELS;
        const int nb = mAnalyzer.numBands();
        const float level = mAnalyzer.level();
        const float beat = mAnalyzer.beat();
        const float bass = mAnalyzer.bass();
        const float gainI = mSpatialIntensity / 100.f;

        if (mSpatialMode == 1) {  // advance the beat shockwave
            if (beat > 0.5f && !mBeatLatch) { mBeatLatch = true; mRingOn = true; mRingPhase = 0.f; }
            if (beat < 0.2f) mBeatLatch = false;
            if (mRingOn) { mRingPhase += dt / 0.6f; if (mRingPhase > 1.5f) mRingOn = false; }
        }

        for (const LayoutProp& p : mLayout) {
            long s = p.start - 1;
            if (s < 0 || s >= cap) continue;
            long c = p.count;
            if (s + c > cap) c = cap - s;
            if (c < 3) continue;

            float bright = 0.f;
            double hue = 0.0;
            if (mSpatialMode == 1) {
                if (mRingOn) bright = std::exp(-std::pow((p.dist - mRingPhase) / 0.16f, 2.f));
                bright *= (0.45f + 0.55f * level);
                hue = 210.0 - 170.0 * bass;            // bass shifts blue -> red
            } else if (mSpatialMode == 2) {
                int bi = (int)(p.nx * nb); if (bi >= nb) bi = nb - 1; if (bi < 0) bi = 0;
                bright = mAnalyzer.band(bi);
                hue = 280.0 * p.nx;
            } else if (mSpatialMode == 3) {
                bright = (p.ny <= level) ? (0.4f + 0.6f * (1.f - (level - p.ny))) : 0.f;
                hue = 120.0 * (1.0 - p.ny);            // green low -> red high
            } else {
                int bi = (int)(p.dist * nb); if (bi >= nb) bi = nb - 1; if (bi < 0) bi = 0;
                bright = mAnalyzer.band(bi);
                hue = 200.0 + 100.0 * p.dist;
            }
            bright *= gainI;
            if (bright < 0.f) bright = 0.f;
            if (bright > 1.f) bright = 1.f;

            uint8_t R, G, B;
            hsv2rgb(hue, 1.0, bright, R, G, B);
            for (long i = 0; i + 2 < c; i += p.step) { d[s + i] = R; d[s + i + 1] = G; d[s + i + 2] = B; }
        }
    }

    // Load the parsed layout if the file changed (uploaded). Cheap flat format
    // so we avoid pulling a JSON/XML dependency into the .so.
    void loadLayoutIfChanged() {
        struct stat stt;
        if (stat(kLayoutPath, &stt) != 0) { mLayout.clear(); mLayoutMtime = 0; return; }
        if (stt.st_mtime == mLayoutMtime && !mLayout.empty()) return;
        mLayoutMtime = stt.st_mtime;
        std::ifstream in(kLayoutPath);
        if (!in.good()) return;
        std::string magic; int ver = 0, n = 0;
        in >> magic >> ver >> n;
        if (magic != "PIXELPULSE_LAYOUT" || n <= 0 || n > 500000) { mLayout.clear(); return; }
        std::vector<LayoutProp> tmp; tmp.reserve(n);
        for (int i = 0; i < n; ++i) {
            LayoutProp p;
            if (!(in >> p.start >> p.count >> p.step >> p.nx >> p.ny >> p.nz >> p.dist)) break;
            if (p.step < 1) p.step = 3;
            tmp.push_back(p);
        }
        mLayout.swap(tmp);
    }

    bool range(long& startIdx, long& bytes) const {
        long count = mCount;
        if (count < 1) return false;
        startIdx = std::max<long>(1, mStart) - 1;
        bytes = count;
        const long cap = (long)FPPD_MAX_CHANNELS;
        if (startIdx >= cap) return false;
        if (startIdx + bytes > cap) bytes = cap - startIdx;
        return bytes >= 1;
    }
    bool shouldModify() const {
        if (ChannelTester::INSTANCE.Testing()) return false;
        if (mOnlyWhenPlaying && (sequence == nullptr || !sequence->IsSequenceRunning())) return false;
        return true;
    }
    void maybeReload() {
        auto now = std::chrono::steady_clock::now();
        if (now - mLastReload >= std::chrono::milliseconds(500)) {
            mLastReload = now;
            std::string prevDev = mDevice;
            int prevRate = mSampleRate, prevCh = mChMode;
            reloadSettings();
            applySettings();
            loadLayoutIfChanged();
            pushAnalyzerParams();
            if (mDevice != prevDev || mSampleRate != prevRate || mChMode != prevCh) restartCapture();
        }
    }
    void pushAnalyzerParams() {
        mAnalyzer.setGain(mGain);
        mAnalyzer.setGate(mGate);
        mAnalyzer.setSensitivity(mSensitivity);
        mAnalyzer.setSmoothing(mSmoothing);
        mAnalyzer.setAgc(mAgcEnabled, mAgcSpeed);
        mAnalyzer.setTrims(mBassTrim, mMidTrim, mTrebleTrim);
    }
    void restartCapture() {
        mAnalyzer.configure(mSampleRate, 1024, 8);
        pushAnalyzerParams();
        mCapture.setChannelMode(mChMode);
        mCapture.start(mDevice, mSampleRate, &mAnalyzer);
    }
    void writeStatus() {
        auto now = std::chrono::steady_clock::now();
        if (now - mLastStatus < std::chrono::milliseconds(120)) return;
        mLastStatus = now;
        // /dev/shm (RAM, no SD wear) is shared across mount namespaces, so the
        // web server (Apache runs with systemd PrivateTmp, a private /tmp) can
        // read what fppd writes. /tmp would be invisible to the settings page.
        FILE* f = fopen("/dev/shm/pixelpulse_status.json", "w");
        if (!f) return;
        fprintf(f,
            "{\"deviceOk\":%s,\"active\":%s,\"level\":%.3f,\"beat\":%.3f,"
            "\"bass\":%.3f,\"mid\":%.3f,\"treble\":%.3f,\"bpm\":%.0f,\"bands\":[",
            mCapture.ok() ? "true" : "false", mAnalyzer.active() ? "true" : "false",
            mAnalyzer.level(), mAnalyzer.beat(), mAnalyzer.bass(), mAnalyzer.mid(),
            mAnalyzer.treble(), mAnalyzer.bpm());
        for (int b = 0; b < mAnalyzer.numBands(); ++b)
            fprintf(f, "%s%.3f", b ? "," : "", mAnalyzer.band(b));
        fprintf(f, "]}");
        fclose(f);
    }

    void applySettings() {
        mEnabled = toLong(cfg("enabled"), 0) != 0;
        mOnlyWhenPlaying = toLong(cfg("onlyWhenPlaying"), 1) != 0;
        mDevice = cfg("audioDevice");
        if (mDevice.empty()) mDevice = "default";
        mSampleRate = (int)toLong(cfg("sampleRate"), 44100);
        mGain = (float)toDouble(cfg("gain"), 1.0);
        mGate = (float)toDouble(cfg("gate"), 0.02);
        mSensitivity = (float)std::max(1.05, toDouble(cfg("sensitivity"), 1.5));
        mSmoothing = (float)toDouble(cfg("smoothing"), 0.0);
        mAgcEnabled = toLong(cfg("agc_enabled"), 1) != 0;
        mAgcSpeed = (float)toDouble(cfg("agc_speed"), 0.5);
        mBassTrim = (float)toDouble(cfg("bass_trim"), 1.0);
        mMidTrim = (float)toDouble(cfg("mid_trim"), 1.0);
        mTrebleTrim = (float)toDouble(cfg("treble_trim"), 1.0);
        std::string ich = cfg("input_channel");
        mChMode = (ich == "left") ? 1 : (ich == "right") ? 2 : 0;
        mChPerPix = std::min<long>(8, std::max<long>(1, toLong(cfg("channelsPerPixel"), 3)));
        mStart = toLong(cfg("startChannel"), 1);
        mCount = std::max<long>(0, toLong(cfg("channelCount"), 1500));

        mBrEnabled = toLong(cfg("br_enabled"), 1) != 0;
        mBrMin = std::min(100L, std::max(0L, toLong(cfg("br_min"), 15)));

        mFlEnabled = toLong(cfg("fl_enabled"), 1) != 0;
        mFlIntensity = std::min(100L, std::max(0L, toLong(cfg("fl_intensity"), 80)));

        std::string vm = cfg("vis_mode");
        mVisMode = (vm == "vu") ? 1 : (vm == "spectrum") ? 2 : 0;
        mHuEnabled = toLong(cfg("hu_enabled"), 0) != 0;
        mHuAmount = toDouble(cfg("hu_amount"), 60.0);

        std::string sm = cfg("speed_mode");
        mSpeedMode = (sm == "level") ? 1 : (sm == "beat") ? 2 : 0;
        mSpeedAmount = std::min(300L, std::max(0L, toLong(cfg("speed_amount"), 50)));

        mSpatialEnabled = toLong(cfg("spatial_enabled"), 0) != 0;
        std::string spm = cfg("spatial_mode");
        mSpatialMode = (spm == "spectrum") ? 2 : (spm == "vu") ? 3 : (spm == "radial") ? 4 : 1;
        mSpatialIntensity = std::min(200L, std::max(0L, toLong(cfg("spatial_intensity"), 100)));
    }

    AudioAnalyzer mAnalyzer;
    AlsaCapture mCapture;
    std::chrono::steady_clock::time_point mLastReload, mLastStatus;

    bool mEnabled = false, mOnlyWhenPlaying = true;
    std::string mDevice = "default";
    int mSampleRate = 44100;
    float mGain = 1.0f, mGate = 0.02f, mSensitivity = 1.5f;
    float mSmoothing = 0.0f, mAgcSpeed = 0.5f, mBassTrim = 1.0f, mMidTrim = 1.0f, mTrebleTrim = 1.0f;
    bool mAgcEnabled = true;
    int mChMode = 0;  // input channel: 0 mix, 1 left, 2 right
    long mChPerPix = 3, mStart = 1, mCount = 1500;

    bool mBrEnabled = true;
    long mBrMin = 15;
    bool mFlEnabled = true;
    long mFlIntensity = 80;
    int mVisMode = 0;          // 0 off, 1 vu, 2 spectrum
    bool mHuEnabled = false;
    double mHuAmount = 60.0;
    int mSpeedMode = 0;        // 0 off, 1 level, 2 beat
    long mSpeedAmount = 50;
    float mSpeedMult = 1.0f;

    // Phase 4: spatial / layout-aware reactions
    std::vector<LayoutProp> mLayout;
    time_t mLayoutMtime = 0;
    bool mSpatialEnabled = false;
    int mSpatialMode = 1;      // 1 bloom, 2 spectrum, 3 vu, 4 radial
    long mSpatialIntensity = 100;
    std::chrono::steady_clock::time_point mLastFrame;
    bool mBeatLatch = false, mRingOn = false;
    float mRingPhase = 0.f;
};

extern "C" {
FPPPlugin* createPlugin() { return new AudioFxPlugin(); }
}
