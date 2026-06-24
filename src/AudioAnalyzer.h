/*
 * AudioAnalyzer - real-time audio feature extraction for the pixelpulse plugin.
 *
 * Fed mono float samples; produces (thread-safe, lock-free) live features:
 *   - level   : overall loudness 0..1 (AGC'd)
 *   - band[i] : per-band energy 0..1 (log-spaced, AGC'd)
 *   - bass/mid/treble : grouped band energy 0..1
 *   - beat    : decaying onset envelope 0..1 (spectral-flux beat detection)
 *   - bpm     : estimated tempo
 *   - active  : true when audio is above the noise gate
 *
 * Pure DSP (kiss_fftr), no I/O - so it is unit-testable without a sound card.
 * pushSamples() is called from the capture thread; the getters are read from
 * the output thread.
 */
#pragma once

#include <atomic>
#include <vector>

#include "kissfft/kiss_fftr.h"

class AudioAnalyzer {
public:
    static constexpr int MAX_BANDS = 16;

    AudioAnalyzer() = default;
    ~AudioAnalyzer();

    void configure(int sampleRate, int fftSize, int numBands);
    void setGain(float g) { mGain = g < 0.01f ? 0.01f : g; }
    void setGate(float g) { mGate = g; }            // 0..1 noise gate
    void setSensitivity(float s) { mSensitivity = s; }  // beat threshold mult
    void setSmoothing(float s) { mSmoothing = s < 0.f ? 0.f : (s > 0.97f ? 0.97f : s); }  // release smoothing 0..0.97
    void setAgc(bool on, float speed) { mAgcEnabled = on; mAgcSpeed = speed < 0.f ? 0.f : (speed > 1.f ? 1.f : speed); }
    void setTrims(float b, float m, float t) { mBassTrim = b; mMidTrim = m; mTrebleTrim = t; }
    void setAutoLevel(bool on) { mAutoLevel = on; }   // slow per-song input leveling
    void setNoiseReduction(float r) { mNoiseReduction = r < 0.f ? 0.f : (r > 1.f ? 1.f : r); }
    void startNoiseLearn(float seconds) { mNoiseLearnReq.store(seconds, std::memory_order_relaxed); }  // capture the silent background

    // Feed mono samples (any count); processes whole FFT windows as they fill.
    void pushSamples(const float* mono, int n);

    float level() const { return mLevel.load(std::memory_order_relaxed); }
    float band(int i) const { return (i >= 0 && i < mNumBands) ? mBands[i].load(std::memory_order_relaxed) : 0.f; }
    int numBands() const { return mNumBands; }
    float bass() const { return mBass.load(std::memory_order_relaxed); }
    float mid() const { return mMid.load(std::memory_order_relaxed); }
    float treble() const { return mTreble.load(std::memory_order_relaxed); }
    float beat() const { return mBeat.load(std::memory_order_relaxed); }
    float bpm() const { return mBpm.load(std::memory_order_relaxed); }
    bool active() const { return mActive.load(std::memory_order_relaxed); }
    float rawLevel() const { return mRawLevel.load(std::memory_order_relaxed); }  // raw RMS, pre-gain/AGC (for calibration)
    // tempo intelligence (autocorrelation tempo + beat-phase PLL)
    float beatPhase() const { return mBeatPhaseA.load(std::memory_order_relaxed); }  // 0..1, 0 = on the beat (predicted, leads the onset)
    long beatNum() const { return mBeatNumA.load(std::memory_order_relaxed); }        // running beat counter (for phrase boundaries)
    float tempoConf() const { return mTempoConfA.load(std::memory_order_relaxed); }   // 0..1 how locked the tempo is
    long onsetCount() const { return mOnsetCount.load(std::memory_order_relaxed); }    // total detected onsets (rate = diff over time)

private:
    void analyzeWindow();

    int mSampleRate = 44100, mFftSize = 1024, mNumBands = 8;
    std::vector<float> mAccum;   // accumulation buffer (mFftSize)
    int mFill = 0;
    std::vector<float> mHann;
    kiss_fftr_cfg mCfg = nullptr;
    std::vector<kiss_fft_cpx> mFreq;     // mFftSize/2+1
    std::vector<float> mMag, mPrevMag;   // magnitude spectra
    std::vector<int> mBandLo, mBandHi;   // bin ranges per band
    float mBandPeak[MAX_BANDS] = {0};    // AGC running peak per band
    float mLevelPeak = 0.0001f;

    // background-noise profile (spectral subtraction): captured during silence
    std::vector<float> mNoiseMag, mNoiseAccum;
    int mNoiseLearn = 0, mNoiseCount = 0;
    bool mHasNoise = false;
    float mNoiseReduction = 1.0f;
    std::atomic<float> mNoiseLearnReq{-1.f};

    float mGain = 1.0f, mGate = 0.02f, mSensitivity = 1.5f;
    float mSmoothing = 0.0f, mAgcSpeed = 0.5f;
    bool mAgcEnabled = true;
    bool mAutoLevel = true;
    float mAutoGainFactor = 1.0f, mAvgRaw = 0.02f;  // slow per-song leveling state
    float mBassTrim = 1.0f, mMidTrim = 1.0f, mTrebleTrim = 1.0f;
    float mLvlS = 0.f, mBassS = 0.f, mMidS = 0.f, mTrebS = 0.f, mBandS[MAX_BANDS] = {0};  // smoothing state

    // beat detection
    float mFluxAvg = 0.0f;
    double mTimeMs = 0.0, mLastBeatMs = -1000.0;
    double mLastInterval = 0.0;

    // tempo tracking: ring of recent onset strengths, autocorrelated for tempo,
    // plus a beat-phase clock (PLL) nudged toward detected onsets.
    static constexpr int ONSET_N = 512;   // ~6s of onset history at the hop rate
    float mOnset[ONSET_N] = {0};
    int mOnsetHead = 0, mTempoTick = 0;
    float mTempoBpm = 120.f, mBeatPhase = 0.f;
    long mBeatNum = 0;
    std::atomic<float> mBeatPhaseA{0.f}, mTempoConfA{0.f};
    std::atomic<long> mBeatNumA{0}, mOnsetCount{0};

    std::atomic<float> mLevel{0}, mBass{0}, mMid{0}, mTreble{0}, mBeat{0}, mBpm{0}, mRawLevel{0};
    std::atomic<float> mBands[MAX_BANDS]{};
    std::atomic<bool> mActive{false};
};
