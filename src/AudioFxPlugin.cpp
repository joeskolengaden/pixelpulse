/*
 * FPP "audiofx" plugin  -  live audio-reactive lighting (FPP 5.4 - 9.x)
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
 * apply live. Writes /tmp/audiofx_status.json for the settings-page meters.
 */
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "Plugin.h"
#include "Sequence.h"
#include "channeltester/ChannelTester.h"

#include "AlsaCapture.h"
#include "AudioAnalyzer.h"

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
}  // namespace

class AudioFxPlugin : public FPPPlugin {
public:
    AudioFxPlugin() : FPPPlugin("audiofx") {
        mLastReload = std::chrono::steady_clock::now();
        mLastStatus = mLastReload;
        applySettings();
        mAnalyzer.configure(mSampleRate, 1024, 8);
        restartCapture();
    }
    ~AudioFxPlugin() override { mCapture.stop(); }

    void modifyChannelData(int /*ms*/, uint8_t* d) override {
        maybeReload();
        writeStatus();
        if (!mEnabled || d == nullptr) return;
        if (!shouldModify()) return;

        long si, by;
        if (!range(si, by)) return;
        const long cpp = mChPerPix;
        const long pixels = (by >= 3) ? ((by - 3) / cpp + 1) : 0;

        const float level = mAnalyzer.level();
        const float beat = mAnalyzer.beat();
        const bool active = mAnalyzer.active();

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
            int prevRate = mSampleRate;
            reloadSettings();
            applySettings();
            mAnalyzer.setGain(mGain);
            mAnalyzer.setGate(mGate);
            mAnalyzer.setSensitivity(mSensitivity);
            if (mDevice != prevDev || mSampleRate != prevRate) restartCapture();
        }
    }
    void restartCapture() {
        mAnalyzer.configure(mSampleRate, 1024, 8);
        mCapture.start(mDevice, mSampleRate, &mAnalyzer);
    }
    void writeStatus() {
        auto now = std::chrono::steady_clock::now();
        if (now - mLastStatus < std::chrono::milliseconds(120)) return;
        mLastStatus = now;
        FILE* f = fopen("/tmp/audiofx_status.json", "w");
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
        mChPerPix = std::min<long>(8, std::max<long>(1, toLong(cfg("channelsPerPixel"), 3)));
        mStart = toLong(cfg("startChannel"), 1);
        mCount = std::max<long>(0, toLong(cfg("channelCount"), 1500));

        mBrEnabled = toLong(cfg("br_enabled"), 1) != 0;
        mBrMin = std::min(100L, std::max(0L, toLong(cfg("br_min"), 15)));

        mFlEnabled = toLong(cfg("fl_enabled"), 1) != 0;
        mFlIntensity = std::min(100L, std::max(0L, toLong(cfg("fl_intensity"), 80)));
    }

    AudioAnalyzer mAnalyzer;
    AlsaCapture mCapture;
    std::chrono::steady_clock::time_point mLastReload, mLastStatus;

    bool mEnabled = false, mOnlyWhenPlaying = true;
    std::string mDevice = "default";
    int mSampleRate = 44100;
    float mGain = 1.0f, mGate = 0.02f, mSensitivity = 1.5f;
    long mChPerPix = 3, mStart = 1, mCount = 1500;

    bool mBrEnabled = true;
    long mBrMin = 15;
    bool mFlEnabled = true;
    long mFlIntensity = 80;
};

extern "C" {
FPPPlugin* createPlugin() { return new AudioFxPlugin(); }
}
