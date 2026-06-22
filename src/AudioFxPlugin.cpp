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
#include <sstream>
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
// One LED node from the parsed xLights layout: the absolute channel of its
// (RGB) pixel plus a normalized world position (0..1 within the display) and
// distance from centre, and a bitmask of the model-groups it belongs to.
// Written by uploadlayout.php, read here for per-LED spatial reactions.
struct LayoutNode {
    long ch;          // absolute 1-based start channel of this LED
    float nx, ny, nz, dist;
    unsigned long mask;
};
const char* const kLayoutPath = "/home/fpp/media/config/pixelpulse_layout.txt";

// Spatial effect vocabulary (live takes on xLights' VU-Meter styles + extras).
const char* const kSpatialModes[] = {
    "bloom", "spectrum", "vu", "radial", "pulse", "spike", "chase",
    "sparkle", "wave", "fireworks", "rain", "strobe", "colorwash", "grow",
    "spin", "bars", "ripple", "fire", "comet", "plasma", "scan", "confetti"};
const int kNumSpatialModes = 22;
int spatialModeIndex(const std::string& s) {
    for (int i = 0; i < kNumSpatialModes; ++i)
        if (s == kSpatialModes[i]) return i + 1;
    return 1;
}
const char* spatialModeName(int idx) {
    return (idx >= 1 && idx <= kNumSpatialModes) ? kSpatialModes[idx - 1] : "bloom";
}
// Curated order the auto-cycle walks through (1-based mode indices).
const int kCycleList[] = {1, 5, 15, 7, 17, 9, 16, 10, 20, 2, 18, 19, 8, 11, 21, 4, 13, 22};
const int kCycleLen = 18;

// "Smart" auto-DJ: the live music is classified into one of these categories,
// each with a pool of the designs that suit it best (1-based mode indices).
const char* const kMusicTypes[] = {"dance", "ambient", "bass", "bright", "groove"};
const int kNumMusicTypes = 5;
const int kSmartPools[5][6] = {
    {1, 10, 12, 6, 16, 5},    // dance/EDM   : bloom, fireworks, strobe, spike, bars, pulse
    {9, 20, 13, 17, 15, 14},  // ambient     : wave, plasma, colorwash, ripple, spin, grow
    {5, 1, 16, 14, 19, 3},    // bass-heavy  : pulse, bloom, bars, grow, comet, vu
    {8, 22, 2, 21, 7, 17},    // bright/pop  : sparkle, confetti, spectrum, scan, chase, ripple
    {7, 2, 3, 19, 4, 16},     // groove      : chase, spectrum, vu, comet, radial, bars
};
const int kSmartPoolLen = 6;
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
        if (mSpatialEnabled && !mNodes.empty()) {
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
    // Track a slow profile of the music (~8s) for the smart auto-DJ, and flag
    // song changes (a silence gap then resume).
    void updateProfile(float dt) {
        float a = 1.f - std::exp(-dt / 8.0f);
        float lv = mAnalyzer.level();
        mAvgLevel += (lv - mAvgLevel) * a;
        mAvgBass += (mAnalyzer.bass() - mAvgBass) * a;
        mAvgMid += (mAnalyzer.mid() - mAvgMid) * a;
        mAvgTreble += (mAnalyzer.treble() - mAvgTreble) * a;
        mAvgBeat += (mAnalyzer.beat() - mAvgBeat) * a;
        if (lv < 0.03f) {
            mSilenceT += dt;
        } else {
            if (mSilenceT > 1.5f) mSongChanged = true;  // resumed after a gap = new song
            mSilenceT = 0.f;
        }
        mDetectedCat = classifyCategory();
    }
    int classifyCategory() const {
        float tot = mAvgBass + mAvgMid + mAvgTreble + 1e-4f;
        float bassR = mAvgBass / tot, trebR = mAvgTreble / tot;
        float bpm = mAnalyzer.bpm();
        if (mAvgLevel > 0.5f && mAvgBeat > 0.32f && bpm > 118.f) return 0;  // dance
        if (mAvgLevel < 0.33f && mAvgBeat < 0.25f) return 1;                // ambient
        if (bassR > 0.45f) return 2;                                       // bass
        if (trebR > 0.40f) return 3;                                       // bright
        return 4;                                                          // groove
    }
    // Pick a design from the detected category's pool; switch on a category
    // change, a new song, or every ~16 s for variety. Returns 1-based mode.
    int smartSelect(float dt) {
        bool repick = false;
        if (mDetectedCat != mSmartCategory) { mSmartCategory = mDetectedCat; repick = true; }
        if (mSongChanged) { mSongChanged = false; mSmartPoolIdx++; repick = true; }
        mSmartTimer += dt;
        if (mSmartTimer >= 16.f) { mSmartPoolIdx++; repick = true; }
        if (repick || mSmartMode == 0) {
            mSmartTimer = 0.f;
            mSmartMode = kSmartPools[mSmartCategory][mSmartPoolIdx % kSmartPoolLen];
        }
        return mSmartMode;
    }

    // Phase 4: drive each prop by its physical position in the display. 22 modes
    // (live takes on xLights VU-Meter styles + extras), optionally auto-cycled
    // or smart-selected from the live music profile.
    void applySpatial(uint8_t* d, float dt) {
        const long cap = (long)FPPD_MAX_CHANNELS;
        const int nb = mAnalyzer.numBands();
        const float level = mAnalyzer.level();
        const float beat = mAnalyzer.beat();
        const float bass = mAnalyzer.bass();
        const float treble = mAnalyzer.treble();
        const float gainI = mSpatialIntensity / 100.f;

        bool beatTrig = false;  // rising edge of a beat
        if (beat > 0.5f && !mBeatLatch) { mBeatLatch = true; beatTrig = true; }
        if (beat < 0.2f) mBeatLatch = false;

        updateProfile(dt);  // keep the live music profile current

        // auto design change: timer / every 16 beats / smart (music-aware)
        int mode = mSpatialMode;
        if (mAutoCycle == 1) {
            mCycleTimer += dt;
            if (mCycleTimer >= (float)mCycleSecs) { mCycleTimer = 0.f; mCycleIdx++; }
            mode = kCycleList[mCycleIdx % kCycleLen];
        } else if (mAutoCycle == 2) {
            if (beatTrig && ++mCycleBeats >= 16) { mCycleBeats = 0; mCycleIdx++; }
            mode = kCycleList[mCycleIdx % kCycleLen];
        } else if (mAutoCycle == 3) {
            mode = smartSelect(dt);
        }
        mEffectiveMode = mode;

        // per-frame state advance. The new phases are wrapped to [0,1) and used
        // as phase*2pi / phase*360, so wrapping stays continuous and bounded.
        mChasePhase += dt * (0.12f + 0.5f * level);
        mWavePhase += dt * 0.6f;
        mSpinPhase += dt * (0.08f + 0.25f * level); mSpinPhase -= std::floor(mSpinPhase);
        mRipplePhase += dt * (0.25f + 0.6f * level); mRipplePhase -= std::floor(mRipplePhase);
        mCometPhase += dt * (0.22f + 0.5f * level); mCometPhase -= std::floor(mCometPhase);
        mScanPhase += dt * (0.25f + 0.6f * level); mScanPhase -= std::floor(mScanPhase);
        if (mode == 1) {
            if (beatTrig) { mRingOn = true; mRingPhase = 0.f; }
            if (mRingOn) { mRingPhase += dt / 0.6f; if (mRingPhase > 1.5f) mRingOn = false; }
        }
        if (mode == 10) {  // fireworks: spawn a burst at a random LED on each beat
            if (beatTrig && !mNodes.empty()) {
                const LayoutNode& q = mNodes[std::rand() % (int)mNodes.size()];
                for (auto& b : mBursts) if (!b.on) { b.on = true; b.age = 0.f; b.x = q.nx; b.y = q.ny; break; }
            }
            for (auto& b : mBursts) if (b.on) { b.age += dt; if (b.age > 1.2f) b.on = false; }
        }
        if (mode == 11) {  // rain: drop a band from the top on each beat
            if (beatTrig) for (auto& rf : mRainFront) if (rf < 0.f) { rf = 1.05f; break; }
            for (auto& rf : mRainFront) if (rf >= 0.f) { rf -= dt / 1.1f; if (rf < -0.1f) rf = -1.f; }
        }
        int dom = 0; float dmax = 0.f;  // dominant band (for colorwash)
        for (int b = 0; b < nb; ++b) { float e = mAnalyzer.band(b); if (e > dmax) { dmax = e; dom = b; } }
        const float chase = std::fmod(mChasePhase, 1.f);

        // optional model-group filter (only light LEDs in the chosen group)
        unsigned long selMask = 0; bool filter = false;
        if (!mSpatialGroup.empty() && mSpatialGroup != "(all)")
            for (size_t gi = 0; gi < mGroupNames.size(); ++gi)
                if (mGroupNames[gi] == mSpatialGroup) { selMask = (1UL << gi); filter = true; break; }

        for (const LayoutNode& p : mNodes) {
            if (filter && !(p.mask & selMask)) continue;
            long s = p.ch - 1;
            if (s < 0 || s + 2 >= cap) continue;

            float br = 0.f; double hue = 0.0, sat = 1.0;
            switch (mode) {
            case 1:  // bloom - beat shockwave from centre
                if (mRingOn) br = std::exp(-std::pow((p.dist - mRingPhase) / 0.16f, 2.f));
                br *= (0.45f + 0.55f * level); hue = 210.0 - 170.0 * bass; break;
            case 2: { int bi = (int)(p.nx * nb); bi = bi < 0 ? 0 : (bi >= nb ? nb - 1 : bi);
                br = mAnalyzer.band(bi); hue = 280.0 * p.nx; } break;  // spectrum across X
            case 3:  // vu - loudness by height
                br = (p.ny <= level) ? (0.4f + 0.6f * (1.f - (level - p.ny))) : 0.f; hue = 120.0 * (1.0 - p.ny); break;
            case 4: { int bi = (int)(p.dist * nb); bi = bi < 0 ? 0 : (bi >= nb ? nb - 1 : bi);
                br = mAnalyzer.band(bi); hue = 200.0 + 100.0 * p.dist; } break;  // radial spectrum
            case 5:  // pulse - whole display breathes with level
                br = 0.1f + 0.9f * level; hue = 210.0 - 170.0 * bass + 90.0 * treble; break;
            case 6:  // spike - sharp full flash on beats (decaying)
                br = beat; hue = 40.0 + 200.0 * bass; break;
            case 7: { float dd = std::fabs(p.nx - chase); dd = std::min(dd, 1.f - dd);  // chase sweep
                br = std::exp(-std::pow(dd / 0.10f, 2.f)) * (0.4f + 0.6f * level); hue = 200.0 + 120.0 * p.nx; } break;
            case 8: { float tw = std::sin(mWavePhase * 6.0f + p.nx * 53.0f + p.ny * 97.0f);  // sparkle
                br = (tw > (1.f - 0.5f * level - (beatTrig ? 0.4f : 0.f))) ? 1.f : 0.f; hue = 180.0 + 120.0 * p.ny; } break;
            case 9: { float w = 0.5f + 0.5f * std::sin((p.nx + p.ny) * 9.42f - mWavePhase * 6.2832f);  // intensity wave
                br = w * (0.25f + 0.75f * level); hue = 260.0 * w; } break;
            case 10: { for (const auto& b : mBursts) if (b.on) {  // fireworks
                float rd = std::hypot(p.nx - b.x, p.ny - b.y);
                br += std::exp(-std::pow((rd - b.age * 0.9f) / 0.08f, 2.f)) * (1.f - b.age / 1.2f); }
                br = std::min(1.f, br) * (0.5f + 0.5f * level); hue = 30.0 + 300.0 * bass; } break;
            case 11: { for (float rf : mRainFront) if (rf >= 0.f) br += std::exp(-std::pow((p.ny - rf) / 0.10f, 2.f));  // rain
                br = std::min(1.f, br); hue = 200.0; } break;
            case 12:  // strobe - full white flash on beat
                br = (beat > 0.5f) ? 1.f : 0.f; sat = 0.0; break;
            case 13:  // colorwash - whole display follows the dominant frequency
                br = 0.15f + 0.85f * level; hue = 280.0 * dom / std::max(1, nb - 1); break;
            case 14:  // grow - lit region expands from centre with level (Level Shape)
                br = (p.dist <= level * 1.15f) ? (0.5f + 0.5f * level) : 0.f; hue = 140.0 - 120.0 * p.dist; break;
            case 15: {  // spin - rotating colour wheel, speeds up with level
                float ang = std::atan2(p.ny - 0.5f, p.nx - 0.5f) * 57.2958f;
                br = 0.2f + 0.8f * level; hue = ang + mSpinPhase * 360.0f + 180.0 * p.dist; } break;
            case 16: {  // bars - graphic-EQ columns: light up to each band's level by X
                int col = (int)(p.nx * nb); col = col < 0 ? 0 : (col >= nb ? nb - 1 : col);
                float h = mAnalyzer.band(col); br = (p.ny <= h) ? (0.4f + 0.6f * h) : 0.f; hue = 280.0 * p.nx; } break;
            case 17: {  // ripple - continuous concentric rings from centre
                float v = 0.5f + 0.5f * std::sin((p.dist * 5.0f - mRipplePhase) * 6.2832f);
                br = std::pow(v, 3.f) * (0.3f + 0.7f * level); hue = 190.0 + 130.0 * p.dist; } break;
            case 18: {  // fire - hot at the bottom, flickering, bass-fed
                float flick = 0.55f + 0.45f * std::sin(mWavePhase * 8.f + p.nx * 40.f + p.ny * 25.f);
                float base = (1.f - p.ny); base *= base;
                br = base * (0.35f + 0.65f * bass) * flick * (0.5f + 0.5f * level); hue = 50.0 * std::min(1.f, br * 1.3f); } break;
            case 19: {  // comet - bright head with a fading trail travelling across X
                float delta = mCometPhase - p.nx; if (delta < 0.f) delta += 1.f;
                br = (delta < 0.35f) ? (1.f - delta / 0.35f) * (0.4f + 0.6f * level) : 0.f; hue = 190.0 + 90.0 * delta; } break;
            case 20: {  // plasma - flowing colour field, level-modulated
                float v = std::sin(p.nx * 6.f + mWavePhase * 3.f) + std::sin(p.ny * 6.f + mWavePhase * 2.f)
                          + std::sin((p.nx + p.ny) * 5.f + mWavePhase); v = (v / 3.f + 1.f) * 0.5f;
                br = (0.3f + 0.7f * level) * (0.4f + 0.6f * v); hue = 360.0 * v; } break;
            case 21: {  // scan - a bar sweeps up and down (Larson), tempo-driven
                float scan = 0.5f + 0.5f * std::sin(mScanPhase * 6.2832f); float dd = std::fabs(p.ny - scan);
                br = std::exp(-std::pow(dd / 0.07f, 2.f)) * (0.4f + 0.6f * level); hue = 0.0 + 30.0 * scan; } break;
            default: {  // confetti - random multicolour LEDs pop on each beat
                float h1 = std::fmod(std::sin(p.ch * 12.9898f) * 43758.5453f, 1.f); if (h1 < 0) h1 += 1.f;
                br = (h1 < 0.15f + 0.35f * beat) ? beat : 0.f;
                float h2 = std::fmod(std::sin(p.ch * 78.233f) * 43758.5453f, 1.f); if (h2 < 0) h2 += 1.f;
                hue = 360.0 * h2; } break;
            }
            br *= gainI;
            if (br < 0.f) br = 0.f; if (br > 1.f) br = 1.f;

            uint8_t R, G, B;
            hsv2rgb(hue, sat, br, R, G, B);
            d[s] = R; d[s + 1] = G; d[s + 2] = B;
        }
    }

    // Load the parsed per-LED layout if the file changed (uploaded). Cheap flat
    // format so we avoid pulling a JSON/XML dependency into the .so. v3 only
    // (per-LED nodes + groups); older per-prop files are ignored (re-upload).
    void loadLayoutIfChanged() {
        struct stat stt;
        if (stat(kLayoutPath, &stt) != 0) { mNodes.clear(); mGroupNames.clear(); mLayoutMtime = 0; return; }
        if (stt.st_mtime == mLayoutMtime && !mNodes.empty()) return;
        mLayoutMtime = stt.st_mtime;
        std::ifstream in(kLayoutPath);
        if (!in.good()) return;
        std::string line;
        if (!std::getline(in, line)) return;
        std::istringstream hs(line);
        std::string magic; int ver = 0, n = 0, ng = 0; float ar = 1.f;
        hs >> magic >> ver >> n;
        if (magic != "PIXELPULSE_LAYOUT" || ver < 3 || n <= 0 || n > 2000000) { mNodes.clear(); mGroupNames.clear(); return; }
        hs >> ar >> ng;
        std::vector<std::string> gnames;
        if (std::getline(in, line) && line.rfind("GROUPS", 0) == 0) {
            std::string rest = line.size() > 7 ? line.substr(7) : "";
            size_t p = 0, q;
            while (p <= rest.size()) { q = rest.find('|', p); std::string nm = rest.substr(p, q == std::string::npos ? std::string::npos : q - p);
                if (!nm.empty()) gnames.push_back(nm); if (q == std::string::npos) break; p = q + 1; }
        }
        std::vector<LayoutNode> tmp; tmp.reserve(n);
        for (int i = 0; i < n; ++i) {
            LayoutNode nd;
            if (!(in >> nd.ch >> nd.nx >> nd.ny >> nd.nz >> nd.dist >> nd.mask)) break;
            tmp.push_back(nd);
        }
        mNodes.swap(tmp);
        mGroupNames.swap(gnames);
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
        mAnalyzer.setAutoLevel(mAutoLevel);
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
            "\"bass\":%.3f,\"mid\":%.3f,\"treble\":%.3f,\"bpm\":%.0f,\"rawLevel\":%.4f,"
            "\"spatialMode\":\"%s\",\"musicType\":\"%s\",\"bands\":[",
            mCapture.ok() ? "true" : "false", mAnalyzer.active() ? "true" : "false",
            mAnalyzer.level(), mAnalyzer.beat(), mAnalyzer.bass(), mAnalyzer.mid(),
            mAnalyzer.treble(), mAnalyzer.bpm(), mAnalyzer.rawLevel(), spatialModeName(mEffectiveMode),
            (mDetectedCat >= 0 && mDetectedCat < kNumMusicTypes) ? kMusicTypes[mDetectedCat] : "");
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
        mSpatialMode = spatialModeIndex(cfg("spatial_mode"));
        mSpatialIntensity = std::min(200L, std::max(0L, toLong(cfg("spatial_intensity"), 100)));
        std::string ac = cfg("spatial_autocycle");
        mAutoCycle = (ac == "time") ? 1 : (ac == "beats") ? 2 : (ac == "smart") ? 3 : 0;
        mCycleSecs = std::max(3L, toLong(cfg("spatial_cyclesecs"), 20));
        mAutoLevel = toLong(cfg("auto_level"), 1) != 0;
        mSpatialGroup = cfg("spatial_group");
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
    std::vector<LayoutNode> mNodes;
    std::vector<std::string> mGroupNames;
    std::string mSpatialGroup;
    time_t mLayoutMtime = 0;
    bool mSpatialEnabled = false;
    int mSpatialMode = 1;          // 1..14 (see kSpatialModes)
    int mEffectiveMode = 1;        // the mode actually rendering (auto-cycle may override)
    long mSpatialIntensity = 100;
    int mAutoCycle = 0;            // 0 off, 1 time, 2 beats
    long mCycleSecs = 20;
    int mCycleIdx = 0, mCycleBeats = 0;
    float mCycleTimer = 0.f;
    bool mAutoLevel = true;
    std::chrono::steady_clock::time_point mLastFrame;
    bool mBeatLatch = false, mRingOn = false;
    float mRingPhase = 0.f, mChasePhase = 0.f, mWavePhase = 0.f;
    float mSpinPhase = 0.f, mRipplePhase = 0.f, mCometPhase = 0.f, mScanPhase = 0.f;
    float mRainFront[3] = {-1.f, -1.f, -1.f};
    // smart auto-DJ: live music profile + selection state
    float mAvgLevel = 0.4f, mAvgBass = 0.3f, mAvgMid = 0.3f, mAvgTreble = 0.3f, mAvgBeat = 0.3f;
    float mSilenceT = 0.f, mSmartTimer = 0.f;
    bool mSongChanged = false;
    int mDetectedCat = 4, mSmartCategory = -1, mSmartPoolIdx = 0, mSmartMode = 0;
    struct Burst { float x = 0, y = 0, age = 0; bool on = false; };
    Burst mBursts[5];
};

extern "C" {
FPPPlugin* createPlugin() { return new AudioFxPlugin(); }
}
