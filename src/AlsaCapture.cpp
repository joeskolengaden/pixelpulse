#include "AlsaCapture.h"
#include "AudioAnalyzer.h"

#include <chrono>
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

#if defined(__linux__)
#include <alsa/asoundlib.h>

void AlsaCapture::run() {
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
                    mono[i] = (raw[i * 2] + raw[i * 2 + 1]) * 0.5f / 32768.f;
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
void AlsaCapture::run() {
    while (mRun.load()) std::this_thread::sleep_for(std::chrono::milliseconds(200));
}
#endif
