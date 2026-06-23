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
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
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
    "spin", "bars", "ripple", "fire", "comet", "plasma", "scan", "confetti",
    "gravimeter", "gravcenter", "waterfall", "djlight", "puddles",
    "fire2012", "aurora", "noise", "twinkle",
    "metaballs", "bursts", "drift", "lissajous",
    "tunnel", "kaleido", "vortex", "rainbow", "breathe", "heartbeat", "lightning", "matrix",
    "starburst", "pinwheel", "glitter", "tide", "radar", "bounce", "embers", "mirror",
    "dna", "blocks", "cylon", "vuspiral", "fireflies", "strobepop", "wipe", "ribbons"};
const int kNumSpatialModes = 59;
const int kWfT = 64;    // waterfall (spectrogram) history depth
const int kHeatN = 32;  // fire2012 heat cells along height
int spatialModeIndex(const std::string& s) {
    for (int i = 0; i < kNumSpatialModes; ++i)
        if (s == kSpatialModes[i]) return i + 1;
    return 1;
}
const char* spatialModeName(int idx) {
    return (idx >= 1 && idx <= kNumSpatialModes) ? kSpatialModes[idx - 1] : "bloom";
}
// Curated order the auto-cycle walks through (1-based mode indices).
const int kCycleList[] = {1, 5, 15, 23, 32, 7, 29, 17, 9, 26, 33, 16, 10, 28, 25, 20, 34, 30, 2, 18, 27, 19, 24, 35, 11, 31, 21, 4, 13, 22,
                          36, 38, 37, 42, 39, 43, 40, 41,
                          44, 48, 45, 51, 47, 49, 50, 46,
                          52, 54, 59, 55, 53, 58, 56, 57};
const int kCycleLen = 54;
// High-coverage designs (>=55% of the layout lit even at silence, measured) used
// as a no-blackout fallback when the mic audio is too weak. Order mixes washes
// and fields for variety: colorwash, spin, noise, pulse, rainbow, djlight,
// plasma, pinwheel, aurora, waterfall, breathe, wave, metaballs.
const int kCoverList[] = {13, 15, 30, 5, 39, 26, 20, 45, 29, 25, 40, 9, 32};
const int kCoverLen = 13;

// "Smart" auto-DJ: the live music is classified into one of these categories,
// each with a pool of the designs that suit it best (1-based mode indices).
const char* const kMusicTypes[] = {"dance", "ambient", "bass", "bright", "groove"};
const int kNumMusicTypes = 5;
const int kSmartPools[5][6] = {
    {44, 57, 26, 53, 49, 54},  // dance/EDM   : starburst, strobepop, djlight, blocks, bounce, cylon
    {29, 40, 47, 36, 56, 52},  // ambient     : aurora, breathe, tide, tunnel, fireflies, dna
    {5, 41, 50, 28, 32, 55},   // bass-heavy  : pulse, heartbeat, embers, fire2012, metaballs, vuspiral
    {8, 46, 39, 51, 59, 58},   // bright/pop  : sparkle, glitter, rainbow, mirror, ribbons, wipe
    {7, 2, 37, 45, 16, 52}     // groove      : chase, spectrum, kaleido, pinwheel, bars, dna
};
const int kSmartPoolLen = 6;

// Colour palettes (WLED-inspired gradients). Designs express colour as a 0..1
// position; when a palette is active that position is mapped through it instead
// of raw HSV, for cohesive themed colour. Index -1 = "auto" (raw HSV).
struct PStop { float p; uint8_t r, g, b; };
struct Palette { const char* name; int n; PStop s[7]; };
const Palette kPalettes[] = {
    {"rainbow", 7, {{0,255,0,0},{0.17f,255,255,0},{0.33f,0,255,0},{0.5f,0,255,255},{0.66f,0,0,255},{0.83f,255,0,255},{1,255,0,0}}},
    {"fire",    5, {{0,0,0,0},{0.3f,200,0,0},{0.6f,255,110,0},{0.85f,255,230,0},{1,255,255,200}}},
    {"ocean",   5, {{0,0,0,50},{0.3f,0,40,130},{0.6f,0,120,190},{0.85f,0,210,200},{1,170,255,255}}},
    {"forest",  5, {{0,0,40,0},{0.3f,0,110,0},{0.6f,70,170,0},{0.85f,170,210,0},{1,220,255,150}}},
    {"sunset",  5, {{0,40,0,70},{0.3f,170,0,80},{0.55f,255,80,0},{0.8f,255,180,0},{1,255,240,130}}},
    {"aurora",  5, {{0,0,40,30},{0.3f,0,170,120},{0.55f,90,225,160},{0.8f,150,120,225},{1,220,160,255}}},
    {"party",   6, {{0,255,0,130},{0.2f,255,0,0},{0.4f,255,160,0},{0.6f,0,200,90},{0.8f,0,120,255},{1,180,0,255}}},
    {"lava",    5, {{0,0,0,0},{0.3f,120,0,0},{0.6f,225,45,0},{0.85f,255,140,0},{1,255,225,90}}},
    {"cloud",   5, {{0,0,0,90},{0.3f,0,40,170},{0.6f,90,90,225},{0.85f,160,185,255},{1,255,255,255}}},
    {"sherbet", 5, {{0,255,80,160},{0.3f,255,160,120},{0.55f,255,255,130},{0.8f,160,255,200},{1,200,200,255}}},
};
const int kNumPalettes = 10;
int paletteIndex(const std::string& s) {
    if (s.empty() || s == "auto") return -1;
    for (int i = 0; i < kNumPalettes; ++i) if (s == kPalettes[i].name) return i;
    return -1;
}
// category -> palette name index for the smart auto-DJ (dance/ambient/bass/bright/groove)
const int kCatPalette[5] = {6, 5, 7, 9, 2};  // party, aurora, lava, sherbet, ocean

// Cooperative gate with the "credits" plugin: when the device is out of credits
// it writes "1" to /dev/shm/credits_block. We honour it and bail out so the
// blackout holds no matter which plugin FPP runs last (plugins load in readdir
// order, so we can't rely on ordering). Cached ~100ms to keep it off the hot path.
inline bool creditsBlocked() {
    static std::chrono::steady_clock::time_point last;
    static bool cached = false, inited = false;
    auto now = std::chrono::steady_clock::now();
    if (!inited || now - last >= std::chrono::milliseconds(100)) {
        inited = true; last = now; cached = false;
        FILE* f = fopen("/dev/shm/credits_block", "r");
        if (f) { cached = (fgetc(f) == '1'); fclose(f); }
    }
    return cached;
}
}  // namespace

class AudioFxPlugin : public FPPPlugin {
public:
    AudioFxPlugin() : FPPPlugin("pixelpulse") {
        mLastReload = std::chrono::steady_clock::now();
        mLastStatus = mLastReload;
        mLastFrame = mLastReload;
        mLastFrameWrite = mLastReload;
        applySettings();
        loadLayoutIfChanged();
        pollSwitch();
        mNoiseLearnSeen = cfg("noise_learn");  // consume any persisted value (don't auto-learn at boot)
        mAnalyzer.configure(mSampleRate, 1024, 8);
        restartCapture();
        // Write the live status from a background thread (~8x/sec) so the
        // settings-page Live monitor works even when nothing is playing - FPP
        // doesn't call modifyChannelData when idle, but capture/analysis run.
        mStatusRun.store(true);
        mStatusThread = std::thread([this] {
            while (mStatusRun.load()) { writeStatus(); ambientTick(); std::this_thread::sleep_for(std::chrono::milliseconds(120)); }
        });
    }
    ~AudioFxPlugin() override {
        mStatusRun.store(false);
        if (mStatusThread.joinable()) mStatusThread.join();
        mCapture.stop();
    }

    void modifyChannelData(int /*ms*/, uint8_t* d) override {
        maybeReload();
        if (!mEnabled || !mSwitchOn || d == nullptr) return;
        if (creditsBlocked()) return;   // out of credits -> leave the buffer dark
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
            writeFramePreview(d);
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
        writeFramePreview(d);
    }

private:
    std::string cfg(const std::string& k) const {
        auto it = settings.find(k);
        if (it == settings.end()) return std::string();
        // FPP's settings API persists values quoted (key = "value") and
        // reloadSettings() keeps the quotes. Strip them so numeric parsing and
        // string comparisons work, and live edits apply without an fppd restart.
        std::string v = it->second;
        if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
        return v;
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
        if (mDetectedCat != mSmartCategory) { mSmartCategory = mDetectedCat; mSmartPalette = kCatPalette[mDetectedCat]; repick = true; }
        if (mSongChanged) { mSongChanged = false; mSmartPoolIdx++; repick = true; }
        mSmartTimer += dt;
        if (mSmartTimer >= (float)mCycleSecs) { mSmartPoolIdx++; repick = true; }  // honour the interval (was hardcoded 16s)
        if (repick || mSmartMode == 0) {
            mSmartTimer = 0.f;
            mSmartMode = kSmartPools[mSmartCategory][mSmartPoolIdx % kSmartPoolLen];
        }
        return mSmartMode;
    }

    // Map a 0..1 position through a palette, scaled by brightness.
    void paletteRGB(int pi, float t, float br, uint8_t& R, uint8_t& G, uint8_t& B) {
        t -= std::floor(t);
        const Palette& P = kPalettes[pi];
        int i = 0; while (i < P.n - 1 && t > P.s[i + 1].p) i++;
        const PStop& a = P.s[i]; const PStop& b = P.s[i < P.n - 1 ? i + 1 : i];
        float span = b.p - a.p, f = span > 1e-4f ? (t - a.p) / span : 0.f;
        if (f < 0) f = 0; if (f > 1) f = 1;
        R = clamp8((int)((a.r + (b.r - a.r) * f) * br));
        G = clamp8((int)((a.g + (b.g - a.g) * f) * br));
        B = clamp8((int)((a.b + (b.b - a.b) * f) * br));
    }

    // Render one LED for a given design, using the per-frame context (mCtx*) and
    // effect state. Separated out so two designs can be blended during a
    // crossfade transition.
    void nodeColor(int mode, const LayoutNode& p, uint8_t& R, uint8_t& G, uint8_t& B) {
        const int nb = mCtxNb;
        const float level = mCtxLevel, beat = mCtxBeat, bass = mCtxBass, treble = mCtxTreble;
        float br = 0.f; double hue = 0.0, sat = 1.0;
        switch (mode) {
        case 1: if (mRingOn) br = std::exp(-std::pow((p.dist - mRingPhase) / 0.16f, 2.f));
            br *= (0.45f + 0.55f * level); hue = 210.0 - 170.0 * bass; break;
        case 2: { int bi = (int)(p.nx * nb); bi = bi < 0 ? 0 : (bi >= nb ? nb - 1 : bi);
            br = mCtxBand[bi]; hue = 280.0 * p.nx; } break;
        case 3: br = (p.ny <= level) ? (0.4f + 0.6f * (1.f - (level - p.ny))) : 0.f; hue = 120.0 * (1.0 - p.ny); break;
        case 4: { int bi = (int)(p.dist * nb); bi = bi < 0 ? 0 : (bi >= nb ? nb - 1 : bi);
            br = mCtxBand[bi]; hue = 200.0 + 100.0 * p.dist; } break;
        case 5: br = 0.1f + 0.9f * level; hue = 210.0 - 170.0 * bass + 90.0 * treble; break;
        case 6: br = beat; hue = 40.0 + 200.0 * bass; break;
        case 7: { float dd = std::fabs(p.nx - mCtxChase); dd = std::min(dd, 1.f - dd);
            br = std::exp(-std::pow(dd / 0.10f, 2.f)) * (0.4f + 0.6f * level); hue = 200.0 + 120.0 * p.nx; } break;
        case 8: { float tw = std::sin(mWavePhase * 6.0f + p.nx * 53.0f + p.ny * 97.0f);
            br = (tw > (1.f - 0.5f * level - (mCtxBeatTrig ? 0.4f : 0.f))) ? 1.f : 0.f; hue = 180.0 + 120.0 * p.ny; } break;
        case 9: { float w = 0.5f + 0.5f * std::sin((p.nx + p.ny) * (5.f + 8.f * bass) - mWavePhase * 6.2832f);  // wave: bands tighten with bass
            br = w * (0.25f + 0.75f * level); hue = 260.0 * w; } break;
        case 10: { for (const auto& b : mBursts) if (b.on) { float rd = std::hypot(p.nx - b.x, p.ny - b.y);
            br += std::exp(-std::pow((rd - b.age * 0.9f) / 0.08f, 2.f)) * (1.f - b.age / 1.2f); }
            br = std::min(1.f, br) * (0.5f + 0.5f * level); hue = 30.0 + 300.0 * bass; } break;
        case 11: { for (float rf : mRainFront) if (rf >= 0.f) br += std::exp(-std::pow((p.ny - rf) / 0.10f, 2.f));
            br = std::min(1.f, br); hue = 200.0; } break;
        case 12: br = (beat > 0.5f) ? 1.f : 0.f; sat = 0.0; break;
        case 13: br = 0.15f + 0.85f * level; hue = 280.0 * mCtxDom / std::max(1, nb - 1); break;
        case 14: br = (p.dist <= level * 1.15f) ? (0.5f + 0.5f * level) : 0.f; hue = 140.0 - 120.0 * p.dist; break;
        case 15: { float ang = std::atan2(p.ny - 0.5f, p.nx - 0.5f) * 57.2958f;
            br = 0.2f + 0.8f * level; hue = ang + mSpinPhase * 360.0f + 180.0 * p.dist; } break;
        case 16: { int col = (int)(p.nx * nb); col = col < 0 ? 0 : (col >= nb ? nb - 1 : col);
            float h = mCtxBand[col]; br = (p.ny <= h) ? (0.4f + 0.6f * h) : 0.f; hue = 280.0 * p.nx; } break;
        case 17: { float v = 0.5f + 0.5f * std::sin((p.dist * 5.0f - mRipplePhase) * 6.2832f);
            br = std::pow(v, 3.f) * (0.3f + 0.7f * level); hue = 190.0 + 130.0 * p.dist; } break;
        case 18: { float flick = 0.55f + 0.45f * std::sin(mWavePhase * 8.f + p.nx * 40.f + p.ny * 25.f);
            float base = (1.f - p.ny); base *= base;
            br = base * (0.35f + 0.65f * bass) * flick * (0.5f + 0.5f * level); hue = 50.0 * std::min(1.f, br * 1.3f); } break;
        case 19: { float delta = mCometPhase - p.nx; if (delta < 0.f) delta += 1.f;
            br = (delta < 0.35f) ? (1.f - delta / 0.35f) * (0.4f + 0.6f * level) : 0.f; hue = 190.0 + 90.0 * delta; } break;
        case 20: { float v = std::sin(p.nx * 6.f + mWavePhase * 3.f) + std::sin(p.ny * 6.f + mWavePhase * 2.f)
                       + std::sin((p.nx + p.ny) * 5.f + mWavePhase); v = (v / 3.f + 1.f) * 0.5f;
            br = (0.3f + 0.7f * level) * (0.4f + 0.6f * v); hue = 360.0 * v; } break;
        case 21: { float scan = 0.5f + 0.5f * std::sin(mScanPhase * 6.2832f); float dd = std::fabs(p.ny - scan);  // scan: line thickens with volume
            br = std::exp(-std::pow(dd / (0.035f + 0.08f * level), 2.f)) * (0.4f + 0.6f * level); hue = 0.0 + 30.0 * scan; } break;
        case 22: { float h1 = std::fmod(std::sin(p.ch * 12.9898f) * 43758.5453f, 1.f); if (h1 < 0) h1 += 1.f;  // confetti
            br = (h1 < 0.15f + 0.35f * beat) ? beat : 0.f;
            float h2 = std::fmod(std::sin(p.ch * 78.233f) * 43758.5453f, 1.f); if (h2 < 0) h2 += 1.f;
            hue = 360.0 * h2; } break;
        case 23:  // gravimeter - VU bar with gravity + a peak that lingers
            br = (p.ny <= mVu) ? (0.4f + 0.6f * (1.f - (mVu - p.ny))) : 0.f;
            if (std::fabs(p.ny - mVuPeak) < 0.02f) br = 1.f;
            hue = 360.0 * p.ny; break;
        case 24: { float dc = std::fabs(p.ny - 0.5f) * 2.f;  // gravcenter - mirrored from the middle
            br = (dc <= mVu) ? (0.4f + 0.6f * (1.f - (mVu - dc))) : 0.f;
            if (std::fabs(dc - mVuPeak) < 0.03f) br = 1.f;
            hue = 360.0 * dc; } break;
        case 25: { int ti = (int)(p.ny * (kWfT - 1)); if (ti < 0) ti = 0; if (ti >= kWfT) ti = kWfT - 1;  // waterfall
            int bi = (int)(p.nx * nb); if (bi < 0) bi = 0; if (bi >= nb) bi = nb - 1;
            int idx = (mWfHead - ti) % kWfT; if (idx < 0) idx += kWfT;
            br = mWaterfall[idx][bi]; hue = 280.0 * p.nx; } break;
        case 26: { float dd = p.dist; float e;  // djlight - bass/mid/treble rings out from centre
            if (dd < 0.34f) { e = bass; hue = 0.0; } else if (dd < 0.67f) { e = mCtxMid; hue = 120.0; } else { e = treble; hue = 240.0; }
            br = 0.12f + 0.88f * e; } break;
        case 27: { for (const auto& pu : mPuddles) if (pu.on) {  // puddles - pools pop on beats and soak out
            float rd = std::hypot(p.nx - pu.x, p.ny - pu.y), radius = pu.age * 0.3f;
            if (radius > 0.f && rd < radius) br += (1.f - rd / radius) * (1.f - pu.age / 1.4f); }
            br = std::min(1.f, br); hue = 360.0 * p.nx; } break;
        case 28: { int hi = (int)(p.ny * (kHeatN - 1)); if (hi < 0) hi = 0; if (hi >= kHeatN) hi = kHeatN - 1;  // fire2012
            float h = mHeat[hi]; br = h; hue = 360.0 * h; } break;
        case 29: { float v = 0.5f + (0.3f + 0.3f * bass) * std::sin(p.ny * 4.f + mWavePhase * 0.8f)  // aurora: curtain amplitude swells with bass
                       + 0.2f * std::sin(p.nx * 3.f - mWavePhase * 0.5f) + 0.2f * std::sin((p.nx + p.ny) * 2.f + mWavePhase * 0.3f);
            if (v < 0) v = 0; if (v > 1) v = 1; br = (0.2f + 0.6f * v) * (0.55f + 0.45f * level); hue = 360.0 * v; } break;
        case 30: { float v = std::sin(p.nx * 8.f + mWavePhase * 2.f) * std::cos(p.ny * 7.f - mWavePhase * 1.5f)  // noise field
                       + std::sin((p.nx * p.ny) * 12.f + mWavePhase); v = (v + 2.f) * 0.25f;
            br = (0.25f + 0.75f * level) * (0.4f + 0.6f * v); hue = 360.0 * v; } break;
        case 31: { float tw = std::sin(mWavePhase * 1.8f + p.ch * 2.399f);  // twinkle (colortwinkles)
            float on = tw > 0.5f ? (tw - 0.5f) * 2.f : 0.f; br = on * (0.5f + 0.5f * level);
            float h = std::fmod(std::sin(p.ch * 0.0173f) * 43758.5453f, 1.f); if (h < 0) h += 1.f; hue = 360.0 * h; } break;
        case 32: { float f = 0.f;  // metaballs - blobby fields from moving centres
            for (int k = 0; k < 3; ++k) { float ax = p.nx - mBallX[k], ay = p.ny - mBallY[k]; f += mBallR / (ax * ax + ay * ay + 0.004f); }
            br = f > 1.f ? 1.f : (f < 0.25f ? 0.f : (f - 0.25f) / 0.75f); hue = 360.0 * p.nx; } break;
        case 33: { float ang = std::atan2(p.ny - 0.5f, p.nx - 0.5f);  // bursts - rotating spokes
            float v = 0.5f + 0.5f * std::sin(ang * 6.f + mSpinPhase * 6.2832f + p.dist * 8.f);
            br = v * v * (0.3f + 0.7f * level); hue = 360.0 * (ang / 6.2832f + 0.5f); } break;
        case 34: { float ang = std::atan2(p.ny - 0.5f, p.nx - 0.5f);  // drift - spiral arms
            float v = 0.5f + 0.5f * std::sin(ang * 3.f + p.dist * 10.f - mSpinPhase * 6.2832f);
            br = v * v * (0.3f + 0.7f * level); hue = 360.0 * p.dist; } break;
        case 36: { float z = p.dist * 3.f - mRipplePhase * 2.f; z -= std::floor(z);  // tunnel - flowing colour rings
            br = std::pow(0.5f + 0.5f * std::sin(z * 6.2832f), 2.f) * (0.3f + 0.7f * level);
            hue = std::fmod(360.0 * z + mSpinPhase * 360.0, 360.0); } break;
        case 37: { float ang = std::atan2(p.ny - 0.5f, p.nx - 0.5f) + 3.14159f, seg = 1.0472f;  // kaleido - 6-fold mirror
            float a = std::fabs(std::fmod(ang, seg) - seg * 0.5f);
            float v = 0.5f + 0.5f * std::sin(a * 12.f + mWavePhase * 3.f - p.dist * 10.f);
            br = v * v * (0.35f + 0.65f * level); hue = std::fmod(360.0 * (a / (seg * 0.5f)) + mSpinPhase * 360.0, 360.0); } break;
        case 38: { float ang = std::atan2(p.ny - 0.5f, p.nx - 0.5f);  // vortex - rotating log spiral
            float v = 0.5f + 0.5f * std::sin(ang * 2.f + std::log(p.dist + 0.05f) * 6.f - mSpinPhase * 12.566f);
            br = v * v * (0.3f + 0.7f * level); hue = std::fmod(360.0 * (ang / 6.2832f + 0.5f) + mSpinPhase * 180.0, 360.0); } break;
        case 39: { float s = p.nx + p.ny * 0.25f - mCometPhase; s -= std::floor(s);  // rainbow - scrolling spectrum
            br = 0.45f + 0.55f * level; hue = 360.0 * s; } break;
        case 40: { float b = 0.5f + 0.5f * std::sin(mWavePhase * 1.2f);  // breathe - calm whole-layout swell
            br = (0.2f + 0.8f * level) * (0.45f + 0.55f * b) * (1.f - 0.35f * p.dist); hue = std::fmod(mWavePhase * 18.0, 360.0); } break;
        case 41: { float ph = mHeartPhase;  // heartbeat - BPM-synced double thump
            float thump = std::exp(-std::pow(ph / 0.09f, 2.f)) + 0.7f * std::exp(-std::pow((ph - 0.18f) / 0.09f, 2.f));
            if (thump > 1.f) thump = 1.f; br = (0.15f + 0.85f * thump) * (0.35f + 0.65f * level) * (1.f - 0.25f * p.dist);
            hue = 350.0; sat = 1.0 - 0.5 * thump; } break;
        case 42: { float bx = 0.5f + 0.42f * std::sin(mScanPhase * 8.168f);  // lightning - electric bolts driven by treble
            float bx2 = 0.5f + 0.42f * std::sin(mScanPhase * 8.168f + 2.1f), w = 0.025f + 0.05f * treble;
            float bolt = std::exp(-std::pow((p.nx - bx) / w, 2.f));
            float b2 = 0.6f * std::exp(-std::pow((p.nx - bx2) / w, 2.f)); if (b2 > bolt) bolt = b2;
            float flick = 0.6f + 0.4f * std::sin(p.ny * 40.f + mWavePhase * 30.f);
            float field = (0.06f + 0.20f * treble) * (0.45f + 0.55f * std::sin(p.ny * 28.f + p.nx * 19.f + mWavePhase * 22.f));  // faint electric haze, brighter on treble
            br = bolt * (0.2f + 0.8f * treble) * flick; if (field > br) br = field; hue = 215.0; sat = 0.45; } break;  // lightning: bolt fires + thickens on treble
        case 43: {  // matrix - falling code rain, per-column streams
            const int cols = 24; int col = (int)(p.nx * cols); if (col < 0) col = 0; if (col >= cols) col = cols - 1;
            float hsh = std::fmod(std::sin(col * 12.9898f) * 43758.5453f, 1.f); if (hsh < 0) hsh += 1.f;
            float ph = std::fmod(mMatrixPhase * (0.5f + hsh) + hsh, 1.f), head = 1.1f - ph * 1.2f, dd = p.ny - head;
            br = (dd >= 0.f && dd < 0.4f) ? (1.f - dd / 0.4f) * (0.4f + 0.6f * level) : 0.f; hue = 130.0; } break;
        case 44: { if (mRingOn) { float ang = std::atan2(p.ny - 0.5f, p.nx - 0.5f);  // starburst - spokes blast out on beats
            float spoke = std::pow(0.5f + 0.5f * std::sin(ang * 8.f), 3.f);
            br = spoke * std::exp(-std::pow((p.dist - mRingPhase * 0.8f) / 0.18f, 2.f)); }
            br *= (0.5f + 0.5f * level); hue = 40.0 + 280.0 * bass; } break;
        case 45: { float ang = std::atan2(p.ny - 0.5f, p.nx - 0.5f) / 6.2832f + 0.5f;  // pinwheel - rotating hard sectors
            float s = ang + mSpinPhase; s -= std::floor(s); int sect = (int)(s * 8.f);
            br = (0.3f + 0.7f * level) * (0.6f + 0.4f * std::cos(p.dist * 6.f)); hue = sect * 45.0; } break;
        case 46: { float h = std::fmod(std::sin(p.ch * 7.13f + std::floor(mWavePhase * 12.f) * 3.7f) * 43758.5453f, 1.f); if (h < 0) h += 1.f;  // glitter
            br = (h > 0.97f - 0.4f * treble) ? 1.f : 0.f; br *= (0.5f + 0.5f * level); sat = 0.15; hue = 210.0; } break;
        case 47: { float surf = 0.12f + 0.85f * mVu + 0.05f * std::sin(p.nx * 9.f + mWavePhase * 4.f);  // tide - wavy rising fill
            br = (p.ny <= surf) ? (0.35f + 0.65f * (1.f - (surf - p.ny))) : 0.f; hue = 200.0 - 60.0 * p.ny; } break;
        case 48: { float ang = std::atan2(p.ny - 0.5f, p.nx - 0.5f) / 6.2832f + 0.5f;  // radar - sweeping ray with trail
            float sweep = ang - mSpinPhase; sweep -= std::floor(sweep);
            br = (sweep < 0.3f) ? (1.f - sweep / 0.3f) : 0.f; br *= (0.3f + 0.7f * level) * (0.35f + 0.65f * p.dist); hue = 120.0; } break;
        case 49: { float by = 0.08f + 0.84f * std::fabs(std::sin(mScanPhase * 3.14159f));  // bounce - bar bounces on the beat
            float dd = std::fabs(p.ny - by); br = std::exp(-std::pow(dd / (0.05f + 0.09f * level), 2.f)) * (0.4f + 0.6f * level); hue = 20.0 + 320.0 * by; } break;  // bounce: bar thickens with volume
        case 50: { float h = std::fmod(std::sin(p.ch * 2.71f) * 43758.5453f, 1.f); if (h < 0) h += 1.f;  // embers - glowing motes rise
            float y = std::fmod(h + mWavePhase * 0.4f, 1.f), dd = std::fabs(p.ny - y);
            br = std::exp(-std::pow(dd / 0.05f, 2.f)) * (0.4f + 0.6f * bass) * (0.5f + 0.5f * level) * (1.f - 0.6f * y); hue = 32.0 - 32.0 * y; } break;
        case 51: { int bi = (int)(std::fabs(p.nx - 0.5f) * 2.f * nb); if (bi < 0) bi = 0; if (bi >= nb) bi = nb - 1;  // mirror - spectrum from centre
            float h = mCtxBand[bi]; br = (std::fabs(p.ny - 0.5f) * 2.f <= h) ? (0.4f + 0.6f * h) : 0.f; hue = 280.0 * std::fabs(p.nx - 0.5f) * 2.f; } break;
        case 52: { float a = p.nx * 9.f + mWavePhase * 3.f;  // dna - two intertwining strands
            float amp = 0.22f + 0.28f * bass, s1 = 0.5f + amp * std::sin(a), s2 = 0.5f - amp * std::sin(a);  // dna: helix opens with bass
            float d1 = std::fabs(p.ny - s1), d2 = std::fabs(p.ny - s2);
            br = (std::exp(-std::pow(d1 / 0.06f, 2.f)) + std::exp(-std::pow(d2 / 0.06f, 2.f))) * (0.4f + 0.6f * level);
            hue = (d1 < d2) ? 200.0 : 320.0; } break;
        case 53: { int cx = (int)(p.nx * 6.f), cy = (int)(p.ny * 6.f);  // blocks - checkerboard, beat-inverting
            bool on = ((cx + cy + mBeatCount) & 1); int bi = (cx + cy) % std::max(1, nb);
            br = on ? (0.2f + 0.8f * mCtxBand[bi]) : 0.f; hue = 60.0 * ((cx + cy) % 6); } break;
        case 54: { float eye = 0.5f + 0.46f * std::sin(mScanPhase * 6.2832f);  // cylon - horizontal sweeping eye
            float dd = std::fabs(p.nx - eye); br = std::exp(-std::pow(dd / (0.035f + 0.08f * level), 2.f)) * (0.4f + 0.6f * level); hue = 0.0; } break;  // cylon: eye thickens with volume
        case 55: { float ang = std::atan2(p.ny - 0.5f, p.nx - 0.5f) / 6.2832f + 0.5f;  // vuspiral - level fills a spiral
            float sp = ang * 0.25f + p.dist; sp -= std::floor(sp);
            br = (sp <= mVu) ? (0.4f + 0.6f * (1.f - (mVu - sp))) : 0.f; hue = 360.0 * sp; } break;
        case 56: { float h = std::fmod(std::sin(p.ch * 4.19f) * 43758.5453f, 1.f); if (h < 0) h += 1.f;  // fireflies - slow wandering glows
            float tw = std::sin(mWavePhase * 0.9f + h * 31.4f); br = (tw > 0.75f) ? (tw - 0.75f) * 4.f : 0.f; br *= (0.5f + 0.5f * level);
            float h2 = std::fmod(std::sin(p.ch * 0.07f) * 43758.5453f, 1.f); if (h2 < 0) h2 += 1.f; hue = 80.0 + 60.0 * h2; } break;
        case 57: { br = (beat > 0.5f) ? 1.f : 0.f; hue = std::fmod(mBeatCount * 0.27f, 1.f) * 360.0; } break;  // strobepop - colour strobe, new hue per beat
        case 58: { float w = mCometPhase, dd = p.nx - w; if (dd < 0.f) dd += 1.f;  // wipe - colour sweep with trail
            br = std::pow(1.f - dd, 2.f) * (0.35f + 0.65f * level); hue = std::fmod(mCometPhase * 360.0 + 110.0 * p.ny, 360.0); } break;
        case 59: { float r = 0.f; for (int k = 0; k < 3; ++k) {  // ribbons - flowing horizontal bands
                float yc = 0.25f + 0.25f * k + (0.05f + 0.16f * bass) * std::sin(p.nx * 6.f + mWavePhase * (2.f + k) + k);  // ribbons: sway grows with bass
                float e = std::exp(-std::pow((p.ny - yc) / 0.05f, 2.f)); if (e > r) r = e; }
            br = r * (0.35f + 0.65f * level); hue = std::fmod(mWavePhase * 30.0 + 120.0 * p.ny, 360.0); } break;
        default: { for (int k = 0; k < 8; ++k) {  // lissajous - a point tracing a curve with a trail
            float ax = p.nx - mLissX[k], ay = p.ny - mLissY[k], d = ax * ax + ay * ay, a = (8 - k) / 8.f;
            float bb = std::exp(-d / 0.008f) * a; if (bb > br) br = bb; }
            br *= (0.5f + 0.5f * level); hue = 360.0 * std::fmod(mWavePhase * 0.2f, 1.f); } break;
        }
        if (mFreshPerChange) hue += mHueShift;   // rotate the palette so each design load feels fresh
        if (br < mMinGlow) br = mMinGlow;        // floor: keep a minimum number of LEDs glowing
        br *= mSpatialIntensity / 100.f;
        if (br < 0.f) br = 0.f; if (br > 1.f) br = 1.f;
        if (mCtxPalette >= 0) paletteRGB(mCtxPalette, (float)(std::fmod(hue, 360.0) / 360.0), br, R, G, B);
        else hsv2rgb(hue, sat, br, R, G, B);
    }

    // Phase 4: drive each LED by its physical position. 22 designs, optionally
    // auto-cycled or smart-selected, with smooth beat-aligned crossfades between
    // designs (so changes feel dynamic to the music, not a hard cut).
    void applySpatial(uint8_t* d, float dt) {
        const long cap = (long)FPPD_MAX_CHANNELS;
        const int nb = mAnalyzer.numBands();
        float level = mAnalyzer.level();
        float beat = mAnalyzer.beat();
        float bass = mAnalyzer.bass();
        float treble = mAnalyzer.treble();
        float mid = mAnalyzer.mid();
        for (int i = 0; i < nb && i < 64; ++i) mCtxBand[i] = mAnalyzer.band(i);

        // Idle design: when there's (almost) no sound, gently self-animate every
        // design with a soft synthetic profile so it still shows its FORM (bars
        // bounce, waves flow, beats occasionally fire) instead of going flat.
        float idleAmt = 1.f - level / 0.07f; if (idleAmt < 0.f) idleAmt = 0.f; if (idleAmt > 1.f) idleAmt = 1.f;
        float ik = idleAmt * (mIdleDesign / 100.f);
        if (ik > 0.f) {
            float t = mWavePhase;
            level  += (0.22f + 0.10f * std::sin(t * 1.3f) - level) * ik;
            bass   += (0.18f + 0.18f * (0.5f + 0.5f * std::sin(t * 0.8f)) - bass) * ik;
            mid    += (0.15f + 0.15f * (0.5f + 0.5f * std::sin(t * 1.1f + 2.f)) - mid) * ik;
            treble += (0.12f + 0.14f * (0.5f + 0.5f * std::sin(t * 1.7f + 4.f)) - treble) * ik;
            for (int i = 0; i < nb && i < 64; ++i) {
                float sBand = 0.12f + 0.32f * (0.5f + 0.5f * std::sin(t * 1.5f + i * 0.55f));
                mCtxBand[i] += (sBand - mCtxBand[i]) * ik;
            }
        }
        mCtxMid = mid;

        bool beatTrig = false;  // rising edge of a beat
        if (beat > 0.5f && !mBeatLatch) { mBeatLatch = true; beatTrig = true; mBeatCount++; }
        if (beat < 0.2f) mBeatLatch = false;
        // synthetic idle beats so beat-driven designs (spike/strobe/fireworks/rain…) still fire when silent
        if (mIdleDesign > 0 && idleAmt > 0.4f) {
            mIdleBeatT += dt;
            if (mIdleBeatT >= 1.3f) { mIdleBeatT = 0.f; beatTrig = true; mBeatCount++; mIdleBeatEnv = 1.f; }
        }
        if (mIdleBeatEnv > 0.f) { float fl = mIdleBeatEnv * (0.45f + 0.45f * mIdleDesign / 100.f); if (fl > beat) beat = fl; mIdleBeatEnv -= dt * 2.5f; if (mIdleBeatEnv < 0.f) mIdleBeatEnv = 0.f; }

        updateProfile(dt);  // keep the live music profile current

        // target design: manual / timer / every 16 beats / smart (music-aware)
        int target = mSpatialMode;
        if (mAutoCycle == 1) {
            mCycleTimer += dt;
            if (mCycleTimer >= (float)mCycleSecs) { mCycleTimer = 0.f; mCycleIdx++; }
            target = kCycleList[mCycleIdx % kCycleLen];
        } else if (mAutoCycle == 2) {
            if (beatTrig && ++mCycleBeats >= 16) { mCycleBeats = 0; mCycleIdx++; }
            target = kCycleList[mCycleIdx % kCycleLen];
        } else if (mAutoCycle == 3) {
            target = smartSelect(dt);
        }
        // Avoid blackout: when the audio is sustained-low (mic too quiet / between
        // songs), override to a curated high-coverage design so the layout stays
        // full instead of running a sparse design over near-silence.
        if (mQuietFallback) {
            if (mAvgLevel < 0.08f) mQuietMode = true; else if (mAvgLevel > 0.18f) mQuietMode = false;
            if (mQuietMode) {
                mCoverTimer += dt;
                if (mCoverTimer >= (float)mCycleSecs) { mCoverTimer = 0.f; mCoverIdx++; }
                target = kCoverList[mCoverIdx % kCoverLen];
            }
        }
        // Switch smoothly: wait for a beat to land the change on (or 2.5s max),
        // then crossfade old->new over a duration that's snappier on louder music.
        if (target != mCurMode && !mWantSwitch && mTransition >= 1.f) { mWantSwitch = true; mSwitchWait = 0.f; }
        if (mWantSwitch) {
            mSwitchWait += dt;
            if (beatTrig || mSwitchWait > 2.5f) {
                mPrevMode = mCurMode; mCurMode = target; mTransition = 0.f; mWantSwitch = false;
                mTransDur = 0.7f - 0.4f * mAvgLevel; if (mTransDur < 0.25f) mTransDur = 0.25f;
                if (mFreshPerChange) {  // fresh palette + motion every time a design loads
                    mHueShift = std::fmod(mHueShift + 80.f + (std::rand() % 90), 360.f);
                    mVarSpeed = 0.7f + (std::rand() % 100) / 100.f * 0.8f;   // 0.7..1.5x motion
                    mVarDir = (std::rand() % 2) ? 1.f : -1.f;                // sometimes reverse
                }
            }
        }
        if (mTransition < 1.f) { mTransition += dt / mTransDur; if (mTransition > 1.f) mTransition = 1.f; }
        mEffectiveMode = mCurMode;

        // per-frame state advance. The new phases are wrapped to [0,1) and used
        // as phase*2pi / phase*360, so wrapping stays continuous and bounded.
        const float vs = mFreshPerChange ? mVarSpeed : 1.f, vd = mFreshPerChange ? mVarDir : 1.f;
        if (mFreshPerChange) { mHueShift += dt * 5.f; mHueShift -= 360.f * std::floor(mHueShift / 360.f); }  // gentle continuous drift
        mChasePhase += dt * (0.12f + 0.5f * level) * vs;
        mWavePhase += dt * 0.6f * vs;
        mSpinPhase += dt * (0.08f + 0.25f * level) * vs * vd; mSpinPhase -= std::floor(mSpinPhase);
        mRipplePhase += dt * (0.25f + 0.6f * level) * vs * vd; mRipplePhase -= std::floor(mRipplePhase);
        mCometPhase += dt * (0.22f + 0.5f * level) * vs * vd; mCometPhase -= std::floor(mCometPhase);
        mScanPhase += dt * (0.25f + 0.6f * level) * vs * vd; mScanPhase -= std::floor(mScanPhase);
        { float bpm = mAnalyzer.bpm(); mHeartPhase += dt * (bpm > 30.f ? bpm / 60.f : 1.25f); mHeartPhase -= std::floor(mHeartPhase); }
        mMatrixPhase += dt * (0.3f + 0.7f * level); mMatrixPhase -= std::floor(mMatrixPhase);
        // advance ALL effect state every frame so both designs in a crossfade are live
        if (beatTrig) { mRingOn = true; mRingPhase = 0.f; }
        if (mRingOn) { mRingPhase += dt / 0.6f; if (mRingPhase > 1.5f) mRingOn = false; }
        if (beatTrig && !mNodes.empty()) {
            const LayoutNode& q = mNodes[std::rand() % (int)mNodes.size()];
            for (auto& b : mBursts) if (!b.on) { b.on = true; b.age = 0.f; b.x = q.nx; b.y = q.ny; break; }
        }
        for (auto& b : mBursts) if (b.on) { b.age += dt; if (b.age > 1.2f) b.on = false; }
        if (beatTrig) for (auto& rf : mRainFront) if (rf < 0.f) { rf = 1.05f; break; }
        for (auto& rf : mRainFront) if (rf >= 0.f) { rf -= dt / 1.1f; if (rf < -0.1f) rf = -1.f; }
        // gravity VU: rises to the level instantly, falls under gravity; peak lingers
        if (level > mVu) mVu = level; else mVu -= 1.2f * dt; if (mVu < 0.f) mVu = 0.f;
        if (mVu > mVuPeak) mVuPeak = mVu; else mVuPeak -= 0.35f * dt; if (mVuPeak < 0.f) mVuPeak = 0.f;
        // waterfall: push the current spectrum into the scrolling history ~25x/sec
        mWfAccum += dt;
        if (mWfAccum >= 0.04f) { mWfAccum -= 0.04f; mWfHead = (mWfHead + 1) % kWfT;
            for (int b = 0; b < 16; ++b) mWaterfall[mWfHead][b] = (b < nb) ? mCtxBand[b] : 0.f; }
        // puddles: spawn at a random LED on each beat, then age out
        if (beatTrig && !mNodes.empty()) {
            const LayoutNode& q = mNodes[std::rand() % (int)mNodes.size()];
            for (auto& pu : mPuddles) if (!pu.on) { pu.on = true; pu.age = 0.f; pu.x = q.nx; pu.y = q.ny; break; }
        }
        for (auto& pu : mPuddles) if (pu.on) { pu.age += dt; if (pu.age > 1.4f) pu.on = false; }
        // fire2012 heat model (throttled for consistent flames): cool, rise, spark on bass
        mFireAccum += dt;
        if (mFireAccum >= 0.03f) { mFireAccum -= 0.03f;
            for (int i = 0; i < kHeatN; ++i) { float cl = (std::rand() % 100) / 100.f * (0.5f / kHeatN + 0.015f); mHeat[i] -= cl; if (mHeat[i] < 0.f) mHeat[i] = 0.f; }
            for (int i = kHeatN - 1; i >= 2; --i) mHeat[i] = (mHeat[i - 1] + mHeat[i - 2] + mHeat[i - 2]) / 3.f;
            if ((std::rand() % 100) / 100.f < 0.15f + 0.85f * bass) {
                int y = std::rand() % (kHeatN / 4 + 1);
                mHeat[y] += (0.3f + 0.7f * bass) * (0.5f + 0.5f * ((std::rand() % 100) / 100.f));
                if (mHeat[y] > 1.f) mHeat[y] = 1.f;
            }
        }
        // metaballs: three centres drifting on parametric paths, size from level
        { float t = mWavePhase;
          mBallX[0] = 0.5f + 0.40f * std::sin(t * 0.7f);       mBallY[0] = 0.5f + 0.40f * std::cos(t * 0.9f);
          mBallX[1] = 0.5f + 0.35f * std::sin(t * 1.1f + 2.f); mBallY[1] = 0.5f + 0.40f * std::cos(t * 0.6f + 1.f);
          mBallX[2] = 0.5f + 0.40f * std::sin(t * 0.5f + 4.f); mBallY[2] = 0.5f + 0.35f * std::cos(t * 1.3f + 3.f);
          mBallR = 0.012f + 0.03f * level; }
        // lissajous: advance the tracing point and shift its trail
        for (int k = 7; k > 0; --k) { mLissX[k] = mLissX[k - 1]; mLissY[k] = mLissY[k - 1]; }
        { float t = mWavePhase * 1.5f; mLissX[0] = 0.5f + 0.42f * std::sin(t * 3.f + 1.f); mLissY[0] = 0.5f + 0.42f * std::sin(t * 2.f); }
        int dom = 0; float dmax = 0.f;  // dominant band (for colorwash)
        for (int b = 0; b < nb; ++b) { float e = mCtxBand[b]; if (e > dmax) { dmax = e; dom = b; } }

        // stash per-frame context so nodeColor() can render any design
        mCtxLevel = level; mCtxBeat = beat; mCtxBass = bass; mCtxTreble = treble;
        mCtxNb = nb; mCtxDom = dom; mCtxChase = std::fmod(mChasePhase, 1.f); mCtxBeatTrig = beatTrig;
        // effective palette: explicit choice, else the smart-DJ's pick, else auto (HSV)
        mCtxPalette = (mPalette >= 0) ? mPalette : (mAutoCycle == 3 ? mSmartPalette : -1);

        // optional model-group filter (only light LEDs in the chosen group)
        unsigned long selMask = 0; bool filter = false;
        if (!mSpatialGroup.empty() && mSpatialGroup != "(all)")
            for (size_t gi = 0; gi < mGroupNames.size(); ++gi)
                if (mGroupNames[gi] == mSpatialGroup) { selMask = (1UL << gi); filter = true; break; }

        const bool blending = mTransition < 1.f && mPrevMode != mCurMode;
        const float tt = mTransition;
        for (const LayoutNode& p : mNodes) {
            if (filter && !(p.mask & selMask)) continue;
            long s = p.ch - 1;
            if (s < 0 || s + 2 >= cap) continue;
            uint8_t R, G, B;
            nodeColor(mCurMode, p, R, G, B);
            if (blending) {  // crossfade from the previous design
                uint8_t R2, G2, B2; nodeColor(mPrevMode, p, R2, G2, B2);
                R = (uint8_t)((int)R2 + (int)(((int)R - (int)R2) * tt));
                G = (uint8_t)((int)G2 + (int)(((int)G - (int)G2) * tt));
                B = (uint8_t)((int)B2 + (int)(((int)B - (int)B2) * tt));
            }
            // how the effect combines with the playing sequence on this LED
            switch (mBlend) {
            case 1:  // overlay - keep whichever is brighter (effect floats over the show)
                if (R > d[s]) d[s] = R; if (G > d[s + 1]) d[s + 1] = G; if (B > d[s + 2]) d[s + 2] = B; break;
            case 2:  // add - brighten the show by the effect
                d[s] = clamp8(d[s] + R); d[s + 1] = clamp8(d[s + 1] + G); d[s + 2] = clamp8(d[s + 2] + B); break;
            case 3: {  // modulate - the effect's brightness pulses the show's own colours
                int lum = (R * 54 + G * 183 + B * 19) >> 8;
                d[s] = d[s] * lum / 255; d[s + 1] = d[s + 1] * lum / 255; d[s + 2] = d[s + 2] * lum / 255; } break;
            default:  // replace - the effect overrides the show (default)
                d[s] = R; d[s + 1] = G; d[s + 2] = B; break;
            }
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
        if (ChannelTester::INSTANCE.Testing()) return false;  // test patterns never touched
        bool playing = sequence != nullptr && sequence->IsSequenceRunning();
        if (mRunWhen == 0) return playing;    // only while a sequence plays
        if (mRunWhen == 1) return !playing;   // only while NOT playing (bridge / effects / between shows)
        return true;                          // always (whenever FPP outputs)
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
            pollSwitch();
            std::string nl = cfg("noise_learn");  // a new value = "Calibrate silence" was pressed
            if (nl != mNoiseLearnSeen) { mNoiseLearnSeen = nl; if (!nl.empty()) mAnalyzer.startNoiseLearn(3.0f); }
            if (mDevice != prevDev || mSampleRate != prevRate || mChMode != prevCh) restartCapture();
        }
    }
    // Read a GPIO input via sysfs (no extra library dependency, works on BBB and
    // Pi). Lazily exports the pin as an input. Returns 0/1, or -1 on error.
    int readGpio(int pin) {
        if (pin < 0) return -1;
        char path[80];
        snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", pin);
        FILE* f = fopen(path, "r");
        if (!f) {
            FILE* e = fopen("/sys/class/gpio/export", "w");
            if (e) { fprintf(e, "%d", pin); fclose(e); }
            char dpath[80];
            snprintf(dpath, sizeof(dpath), "/sys/class/gpio/gpio%d/direction", pin);
            FILE* dd = fopen(dpath, "w");
            if (dd) { fprintf(dd, "in"); fclose(dd); }
            f = fopen(path, "r");
            if (!f) return -1;
        }
        int c = fgetc(f);
        fclose(f);
        return c == '1' ? 1 : (c == '0' ? 0 : -1);
    }
    int runGpioGet(const char* cmd) {
        FILE* f = popen(cmd, "r");
        if (!f) return -1;
        char buf[24] = {0};
        bool got = fgets(buf, sizeof(buf), f) != nullptr;
        pclose(f);
        if (!got) return -1;
        for (char* p = buf; *p; ++p) { if (*p == '0') return 0; if (*p == '1') return 1; }
        return -1;
    }
    // Read a GPIO line via libgpiod's gpioget, applying a pull bias (the only way
    // to set the internal pull resistor). Tries the libgpiod v1 syntax then v2
    // (newer FPP). Returns 0/1, or -1 if unavailable.
    int gpiogetRead(int chip, int line, int pull) {
        if (chip < 0 || line < 0) return -1;
        const char* bias = pull == 1 ? "pull-up" : (pull == 2 ? "pull-down" : "disable");
        char cmd[200];
        snprintf(cmd, sizeof(cmd), "gpioget --bias=%s gpiochip%d %d 2>/dev/null", bias, chip, line);
        int v = runGpioGet(cmd);
        if (v < 0) {
            snprintf(cmd, sizeof(cmd), "gpioget --numeric --bias=%s -c gpiochip%d %d 2>/dev/null", bias, chip, line);
            v = runGpioGet(cmd);
        }
        return v;
    }
    // A configured physical switch gates the plugin. Prefer gpioget (so the pull
    // bias applies); fall back to sysfs by GPIO number. Unconfigured/unreadable
    // leaves mSwitchOn true (never gets stuck off).
    void pollSwitch() {
        if (!mSwitchEnabled) { mSwitchOn = true; return; }
        int v = gpiogetRead(mSwitchChip, mSwitchLine, mSwitchPull);
        if (v < 0 && mSwitchGpio >= 0) v = readGpio(mSwitchGpio);
        if (v >= 0) mSwitchOn = mSwitchActiveHigh ? (v == 1) : (v == 0);
    }
    void pushAnalyzerParams() {
        mAnalyzer.setGain(mGain);
        mAnalyzer.setGate(mGate);
        mAnalyzer.setSensitivity(mSensitivity);
        mAnalyzer.setSmoothing(mSmoothing);
        mAnalyzer.setAgc(mAgcEnabled, mAgcSpeed);
        mAnalyzer.setTrims(mBassTrim, mMidTrim, mTrebleTrim);
        mAnalyzer.setAutoLevel(mAutoLevel);
        mAnalyzer.setNoiseReduction(mNoiseReduction);
    }
    void restartCapture() {
        mAnalyzer.configure(mSampleRate, 1024, 8);
        pushAnalyzerParams();
        mCapture.setChannelMode(mChMode);
        mCapture.start(mDevice, mSampleRate, &mAnalyzer);
    }
    // Ambient mode: when enabled and nothing is playing, (re)start the looping
    // blank playlist so FPP runs its output loop and the audio designs light the
    // display with no real show. Reads the flags straight from the persistent
    // config (the settings map isn't refreshed while idle). Runs on the status
    // thread; the curl is backgrounded so it never blocks status writes.
    void ambientTick() {
        auto now = std::chrono::steady_clock::now();
        if (now - mLastAmbientCheck < std::chrono::seconds(2)) return;
        mLastAmbientCheck = now;
        bool ambient = false, enabled = false, autocycle = false;
        std::string pl;
        std::ifstream cf("/home/fpp/media/config/plugin.pixelpulse");
        std::string line;
        while (std::getline(cf, line)) {
            if (line.rfind("ambient_mode", 0) == 0) ambient = line.find("\"1\"") != std::string::npos;
            else if (line.rfind("enabled ", 0) == 0) enabled = line.find("\"1\"") != std::string::npos;
            else if (line.rfind("spatial_autocycle", 0) == 0)
                autocycle = line.find("\"off\"") == std::string::npos && line.find("\"\"") == std::string::npos;
            else if (line.rfind("ambient_playlist", 0) == 0) {
                size_t q1 = line.find('"');
                if (q1 != std::string::npos) { size_t q2 = line.find('"', q1 + 1); if (q2 != std::string::npos) pl = line.substr(q1 + 1, q2 - q1 - 1); }
            }
        }
        // Auto design change implies a running show: if cycling is enabled, run the
        // output loop too (like ambient) so designs cycle standalone, no show needed.
        if ((!ambient && !autocycle) || !enabled) return;
        if (ChannelTester::INSTANCE.Testing()) return;
        if (sequence != nullptr && sequence->IsSequenceRunning()) return;  // a show is already playing
        if (now - mLastAmbientStart < std::chrono::seconds(3)) return;
        mLastAmbientStart = now;
        // the chosen playlist of real designs, or the blank fallback (audio-only).
        // sanitize - the name is interpolated into a shell command.
        bool safe = !pl.empty();
        for (char c : pl) if (!(std::isalnum((unsigned char)c) || c == ' ' || c == '_' || c == '-' || c == '.')) { safe = false; break; }
        if (!safe) pl = "pixelpulse_ambient";
        std::string cmd = "curl -s -m 3 -X POST -H 'Content-Type: application/json' --data "
                          "'{\"command\":\"Start Playlist\",\"args\":[\"" + pl + "\",\"true\",\"true\"]}' "
                          "http://127.0.0.1/api/command >/dev/null 2>&1 &";
        std::system(cmd.c_str());
    }
    const char* effPaletteName() const {
        int ep = (mPalette >= 0) ? mPalette : (mAutoCycle == 3 ? mSmartPalette : -1);
        return ep >= 0 ? kPalettes[ep].name : "auto";
    }
    void writeStatus() {
        // Called from the background status thread (~8x/sec) so the Live monitor
        // updates even when idle. Reads only thread-safe atomics + plain ints.
        // /dev/shm (RAM, no SD wear) is shared across mount namespaces, so the
        // web server (Apache runs with systemd PrivateTmp, a private /tmp) can
        // read what fppd writes. /tmp would be invisible to the settings page.
        FILE* f = fopen("/dev/shm/pixelpulse_status.json", "w");
        if (!f) return;
        fprintf(f,
            "{\"deviceOk\":%s,\"active\":%s,\"level\":%.3f,\"beat\":%.3f,"
            "\"bass\":%.3f,\"mid\":%.3f,\"treble\":%.3f,\"bpm\":%.0f,\"rawLevel\":%.4f,"
            "\"spatialMode\":\"%s\",\"musicType\":\"%s\",\"palette\":\"%s\",\"switchEnabled\":%s,\"switchOn\":%s,\"bands\":[",
            mCapture.ok() ? "true" : "false", mAnalyzer.active() ? "true" : "false",
            mAnalyzer.level(), mAnalyzer.beat(), mAnalyzer.bass(), mAnalyzer.mid(),
            mAnalyzer.treble(), mAnalyzer.bpm(), mAnalyzer.rawLevel(), spatialModeName(mEffectiveMode),
            (mDetectedCat >= 0 && mDetectedCat < kNumMusicTypes) ? kMusicTypes[mDetectedCat] : "",
            effPaletteName(), mSwitchEnabled ? "true" : "false", mSwitchOn ? "true" : "false");
        for (int b = 0; b < mAnalyzer.numBands(); ++b)
            fprintf(f, "%s%.3f", b ? "," : "", mAnalyzer.band(b));
        fprintf(f, "]}");
        fclose(f);
    }

    // Snapshot the ACTUAL output colours at a downsampled set of LED positions so
    // the settings page can preview what's really playing (sequence + reactions /
    // spatial effect), not just a simulation. Same stride as uploadlayout's
    // points, so the preview can line colours up with positions.
    void writeFramePreview(const uint8_t* d) {
        if (mNodes.empty() || d == nullptr) return;
        auto now = std::chrono::steady_clock::now();
        if (now - mLastFrameWrite < std::chrono::milliseconds(100)) return;
        mLastFrameWrite = now;
        const int n = (int)mNodes.size();
        int stride = (n + 699) / 700; if (stride < 1) stride = 1;
        const long cap = (long)FPPD_MAX_CHANNELS;
        FILE* f = fopen("/dev/shm/pixelpulse_frame.txt", "w");
        if (!f) return;
        static const char* H = "0123456789abcdef";
        std::string hex; hex.reserve(2200); int cnt = 0;
        for (int i = 0; i < n; i += stride) {
            long s = mNodes[i].ch - 1; uint8_t r = 0, g = 0, b = 0;
            if (s >= 0 && s + 2 < cap) { r = d[s]; g = d[s + 1]; b = d[s + 2]; }
            char buf[7] = {H[r >> 4], H[r & 15], H[g >> 4], H[g & 15], H[b >> 4], H[b & 15], 0};
            hex += buf; cnt++;
        }
        fprintf(f, "%d\n%s", cnt, hex.c_str());
        fclose(f);
    }

    void applySettings() {
        mEnabled = toLong(cfg("enabled"), 0) != 0;
        std::string rw = cfg("run_when");
        if (!rw.empty()) mRunWhen = (rw == "idle") ? 1 : (rw == "always") ? 2 : 0;
        else mRunWhen = (toLong(cfg("onlyWhenPlaying"), 1) != 0) ? 0 : 2;  // back-compat with onlyWhenPlaying
        mSwitchEnabled = toLong(cfg("switch_enabled"), 0) != 0;
        mSwitchGpio = (int)toLong(cfg("switch_gpio"), -1);
        mSwitchChip = (int)toLong(cfg("switch_chip"), -1);
        mSwitchLine = (int)toLong(cfg("switch_line"), -1);
        std::string sp = cfg("switch_pull");
        mSwitchPull = (sp == "up") ? 1 : (sp == "down") ? 2 : 0;
        mSwitchActiveHigh = cfg("switch_active") != "low";  // default active-high
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
        mNoiseReduction = (float)toDouble(cfg("noise_reduction"), 1.0);
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
        std::string bl = cfg("spatial_blend");
        mBlend = (bl == "over") ? 1 : (bl == "add") ? 2 : (bl == "modulate") ? 3 : 0;
        mPalette = paletteIndex(cfg("palette"));
        std::string ac = cfg("spatial_autocycle");
        mAutoCycle = (ac == "time") ? 1 : (ac == "beats") ? 2 : (ac == "smart") ? 3 : 0;
        mCycleSecs = std::max(3L, toLong(cfg("spatial_cyclesecs"), 20));
        mFreshPerChange = toLong(cfg("fresh_per_change"), 1) != 0;
        mMinGlow = (float)std::min(0.5, std::max(0.0, toDouble(cfg("min_glow"), 8.0) / 100.0));
        mIdleDesign = std::min(100L, std::max(0L, toLong(cfg("idle_design"), 35)));
        mQuietFallback = toLong(cfg("quiet_fallback"), 1) != 0;
        mAutoLevel = toLong(cfg("auto_level"), 1) != 0;
        mSpatialGroup = cfg("spatial_group");
    }

    AudioAnalyzer mAnalyzer;
    AlsaCapture mCapture;
    std::thread mStatusThread;
    std::atomic<bool> mStatusRun{false};
    std::chrono::steady_clock::time_point mLastReload, mLastStatus, mLastFrameWrite, mLastAmbientCheck, mLastAmbientStart;

    bool mEnabled = false;
    int mRunWhen = 0;  // 0 only while playing, 1 only while not playing, 2 always
    bool mSwitchEnabled = false, mSwitchActiveHigh = true, mSwitchOn = true;
    int mSwitchGpio = -1, mSwitchChip = -1, mSwitchLine = -1, mSwitchPull = 0;  // pull: 0 none,1 up,2 down
    std::string mDevice = "default";
    int mSampleRate = 44100;
    float mGain = 1.0f, mGate = 0.02f, mSensitivity = 1.5f;
    float mSmoothing = 0.0f, mAgcSpeed = 0.5f, mBassTrim = 1.0f, mMidTrim = 1.0f, mTrebleTrim = 1.0f;
    float mNoiseReduction = 1.0f;
    std::string mNoiseLearnSeen;
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
    int mSpatialMode = 1;          // 1..35 (see kSpatialModes)
    int mBlend = 0;                // 0 replace(override), 1 overlay, 2 add, 3 modulate
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
    float mHeartPhase = 0.f, mMatrixPhase = 0.f;  // heartbeat (BPM-synced), matrix (falling streams)
    int mBeatCount = 0;  // increments each beat (blocks invert / strobepop hue)
    float mHueShift = 0.f, mVarSpeed = 1.f, mVarDir = 1.f;  // per-design freshness: palette rotation + motion variation
    bool mFreshPerChange = true; float mMinGlow = 0.08f;
    float mRainFront[3] = {-1.f, -1.f, -1.f};
    // audio meters (phase 2): gravity VU, waterfall spectrogram, puddles
    float mVu = 0.f, mVuPeak = 0.f, mWfAccum = 0.f;
    float mWaterfall[kWfT][16] = {};
    int mWfHead = 0;
    struct Puddle { float x = 0, y = 0, age = 0; bool on = false; };
    Puddle mPuddles[4];
    float mHeat[kHeatN] = {}, mFireAccum = 0.f;  // fire2012 heat model (phase 3)
    float mBallX[3] = {0.5f, 0.5f, 0.5f}, mBallY[3] = {0.5f, 0.5f, 0.5f}, mBallR = 0.02f;  // metaballs (phase 4)
    float mLissX[8] = {}, mLissY[8] = {};  // lissajous trail
    // smart auto-DJ: live music profile + selection state
    float mAvgLevel = 0.4f, mAvgBass = 0.3f, mAvgMid = 0.3f, mAvgTreble = 0.3f, mAvgBeat = 0.3f;
    float mSilenceT = 0.f, mSmartTimer = 0.f;
    bool mSongChanged = false;
    int mDetectedCat = 4, mSmartCategory = -1, mSmartPoolIdx = 0, mSmartMode = 0;
    // smooth crossfade between designs
    int mCurMode = 1, mPrevMode = 1;
    float mTransition = 1.f, mTransDur = 0.5f, mSwitchWait = 0.f;
    bool mWantSwitch = false;
    // per-frame render context for nodeColor()
    float mCtxLevel = 0.f, mCtxBeat = 0.f, mCtxBass = 0.f, mCtxTreble = 0.f, mCtxChase = 0.f;
    float mCtxBand[64] = {0}, mCtxMid = 0.f;   // idle-substitutable spectrum + mid
    int mCtxNb = 8, mCtxDom = 0;
    bool mCtxBeatTrig = false;
    long mIdleDesign = 35; float mIdleBeatT = 0.f, mIdleBeatEnv = 0.f;  // self-animate designs when silent
    bool mQuietFallback = true, mQuietMode = false; float mCoverTimer = 0.f; int mCoverIdx = 0;  // no-blackout high-coverage fallback
    // colour palette: -1 auto (HSV), else index into kPalettes
    int mPalette = -1, mSmartPalette = 6, mCtxPalette = -1;
    struct Burst { float x = 0, y = 0, age = 0; bool on = false; };
    Burst mBursts[5];
};

extern "C" {
FPPPlugin* createPlugin() { return new AudioFxPlugin(); }
}
