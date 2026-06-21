/*
 * AlsaCapture - reads PCM from an ALSA capture device (the USB audio input) in
 * a background thread and feeds mono float samples to an AudioAnalyzer.
 *
 * Robust by design: if the device can't be opened it retries quietly and
 * reports ok()==false; an xrun is recovered; stop() joins cleanly. The real
 * ALSA path is Linux-only (the device); elsewhere it compiles as a no-op stub
 * so the plugin still builds for verification.
 */
#pragma once

#include <atomic>
#include <string>
#include <thread>

class AudioAnalyzer;

class AlsaCapture {
public:
    AlsaCapture() = default;
    ~AlsaCapture() { stop(); }

    void start(const std::string& device, int sampleRate, AudioAnalyzer* analyzer);
    void stop();
    bool ok() const { return mOk.load(); }
    const std::string& device() const { return mDevice; }

private:
    void run();
    void runCapture();      // real ALSA capture (Linux) / stub elsewhere
    void runTestSignal();   // synthetic audio for testing without hardware

    std::thread mThread;
    std::atomic<bool> mRun{false};
    std::atomic<bool> mOk{false};
    std::string mDevice;
    int mRate = 44100;
    AudioAnalyzer* mAnalyzer = nullptr;
};
