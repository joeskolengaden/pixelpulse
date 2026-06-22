#include "AlsaCapture.h"
#include "AudioAnalyzer.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

void AlsaCapture::start(const std::string& device, int sampleRate, AudioAnalyzer* analyzer) {
    stop();
    mDevice = device.empty() ? "default" : device;
    mRate = sampleRate > 0 ? sampleRate : 44100;
    mAnalyzer = analyzer;
    mRun.store(true);
    mThread = std::thread([this] { run(); });
}

void AlsaCapture::stop() {
    mRun.store(false);
    if (mThread.joinable()) mThread.join();
    mOk.store(false);
}

void AlsaCapture::run() {
    if (mDevice == "test") { runTestSignal(); return; }
    runCapture();
}

// Synthetic audio so the whole pipeline can be exercised with no hardware:
// a swelling 60 Hz bass + a kick burst every 0.5 s (120 BPM). Paced to real time.
void AlsaCapture::runTestSignal() {
    const int block = 512;
    std::vector<float> buf(block);
    if (mAnalyzer) mAnalyzer->configure(mRate, 1024, mAnalyzer->numBands() > 0 ? mAnalyzer->numBands() : 8);
    mOk.store(true);
    long n = 0;
    const int beatPeriod = mRate / 2;  // 0.5 s
    while (mRun.load()) {
        for (int j = 0; j < block; ++j, ++n) {
            float bass = 0.28f * std::sin(2.f * (float)M_PI * 60.f * n / mRate);
            float swell = 0.55f + 0.45f * std::sin(2.f * (float)M_PI * 0.2f * n / mRate);
            int ph = (int)(n % beatPeriod);
            float kick = (ph < 400) ? 0.6f * ((std::rand() / (float)RAND_MAX) * 2 - 1) * (1.f - ph / 400.f) : 0.f;
            buf[j] = bass * swell + kick;
        }
        if (mAnalyzer) mAnalyzer->pushSamples(buf.data(), block);
        std::this_thread::sleep_for(std::chrono::microseconds((long)(1000000.0 * block / mRate)));
    }
    mOk.store(false);
}

#if defined(__linux__) || defined(PFX_FORCE_ALSA)
#include <alsa/asoundlib.h>
#include <cerrno>
#ifndef ESTRPIPE
#define ESTRPIPE EPIPE  // Linux-only errno; fall back to EPIPE elsewhere
#endif

void AlsaCapture::runCapture() {
    const snd_pcm_uframes_t period = 512;  // ~11ms @ 44.1k
    std::vector<int16_t> raw;
    std::vector<float> mono;

    while (mRun.load()) {
        snd_pcm_t* pcm = nullptr;
        if (snd_pcm_open(&pcm, mDevice.c_str(), SND_PCM_STREAM_CAPTURE, 0) < 0) {
            mOk.store(false);
            std::this_thread::sleep_for(std::chrono::seconds(2));  // device not ready - retry
            continue;
        }
        unsigned int rate = mRate;
        int channels = 2;
        snd_pcm_hw_params_t* hw;
        snd_pcm_hw_params_alloca(&hw);
        snd_pcm_hw_params_any(pcm, hw);
        snd_pcm_hw_params_set_access(pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
        snd_pcm_hw_params_set_format(pcm, hw, SND_PCM_FORMAT_S16_LE);
        // try stereo, fall back to mono
        if (snd_pcm_hw_params_set_channels(pcm, hw, 2) < 0) {
            channels = 1;
            snd_pcm_hw_params_set_channels(pcm, hw, 1);
        }
        snd_pcm_hw_params_set_rate_near(pcm, hw, &rate, nullptr);
        snd_pcm_uframes_t per = period;
        snd_pcm_hw_params_set_period_size_near(pcm, hw, &per, nullptr);
        if (snd_pcm_hw_params(pcm, hw) < 0) {
            snd_pcm_close(pcm);
            mOk.store(false);
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }
        if (mAnalyzer) mAnalyzer->configure((int)rate, 1024, mAnalyzer->numBands() > 0 ? mAnalyzer->numBands() : 8);
        snd_pcm_prepare(pcm);
        mOk.store(true);

        raw.assign(per * channels, 0);
        mono.assign(per, 0.f);
        while (mRun.load()) {
            snd_pcm_sframes_t n = snd_pcm_readi(pcm, raw.data(), per);
            if (n == -EPIPE || n == -ESTRPIPE) {
                snd_pcm_recover(pcm, (int)n, 1);
                continue;
            }
            if (n < 0) break;  // fatal - reopen
            for (snd_pcm_sframes_t i = 0; i < n; ++i) {
                if (channels == 2) {
                    if (mChMode == 1) mono[i] = raw[i * 2] / 32768.f;          // left only
                    else if (mChMode == 2) mono[i] = raw[i * 2 + 1] / 32768.f;  // right only
                    else mono[i] = (raw[i * 2] + raw[i * 2 + 1]) * 0.5f / 32768.f;  // mix
                } else {
                    mono[i] = raw[i] / 32768.f;
                }
            }
            if (mAnalyzer) mAnalyzer->pushSamples(mono.data(), (int)n);
        }
        snd_pcm_close(pcm);
        mOk.store(false);
    }
}

#else  // non-Linux: no-op stub so the plugin still compiles for verification
void AlsaCapture::runCapture() {
    while (mRun.load()) std::this_thread::sleep_for(std::chrono::milliseconds(200));
}
#endif
