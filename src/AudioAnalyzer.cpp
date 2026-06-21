#include "AudioAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

AudioAnalyzer::~AudioAnalyzer() {
    if (mCfg) free(mCfg);
}

void AudioAnalyzer::configure(int sampleRate, int fftSize, int numBands) {
    mSampleRate = sampleRate > 0 ? sampleRate : 44100;
    mFftSize = fftSize >= 256 ? fftSize : 1024;
    mNumBands = std::min(MAX_BANDS, std::max(1, numBands));

    if (mCfg) { free(mCfg); mCfg = nullptr; }
    mCfg = kiss_fftr_alloc(mFftSize, 0, nullptr, nullptr);

    const int nb = mFftSize / 2 + 1;
    mAccum.assign(mFftSize, 0.f);
    mFill = 0;
    mFreq.assign(nb, kiss_fft_cpx{});
    mMag.assign(nb, 0.f);
    mPrevMag.assign(nb, 0.f);

    // Hann window
    mHann.assign(mFftSize, 0.f);
    for (int i = 0; i < mFftSize; ++i)
        mHann[i] = 0.5f * (1.f - std::cos(2.f * (float)M_PI * i / (mFftSize - 1)));

    // Log-spaced band bin ranges over [40Hz, Nyquist].
    mBandLo.assign(mNumBands, 0);
    mBandHi.assign(mNumBands, 0);
    const double fmin = 40.0, fmax = mSampleRate / 2.0;
    const double binHz = (double)mSampleRate / mFftSize;
    for (int b = 0; b < mNumBands; ++b) {
        double lo = fmin * std::pow(fmax / fmin, (double)b / mNumBands);
        double hi = fmin * std::pow(fmax / fmin, (double)(b + 1) / mNumBands);
        int klo = std::max(1, (int)(lo / binHz));
        int khi = std::min(nb - 1, std::max(klo + 1, (int)(hi / binHz)));
        mBandLo[b] = klo;
        mBandHi[b] = khi;
        mBandPeak[b] = 0.0001f;
    }
    mLevelPeak = 0.0001f;
    mFluxAvg = 0.f;
    mTimeMs = 0.0;
    mLastBeatMs = -1000.0;
}

void AudioAnalyzer::pushSamples(const float* mono, int n) {
    if (!mCfg) return;
    for (int i = 0; i < n; ++i) {
        mAccum[mFill++] = mono[i];
        if (mFill >= mFftSize) {
            analyzeWindow();
            // 50% overlap: keep the second half for the next window
            const int half = mFftSize / 2;
            std::copy(mAccum.begin() + half, mAccum.end(), mAccum.begin());
            mFill = half;
        }
    }
}

void AudioAnalyzer::analyzeWindow() {
    // RMS level on the raw window
    double sumSq = 0.0;
    std::vector<float> w(mFftSize);
    for (int i = 0; i < mFftSize; ++i) {
        sumSq += (double)mAccum[i] * mAccum[i];
        w[i] = mAccum[i] * mHann[i] * mGain;
    }
    float rms = (float)std::sqrt(sumSq / mFftSize);

    kiss_fftr(mCfg, w.data(), mFreq.data());
    const int nb = mFftSize / 2 + 1;
    for (int k = 0; k < nb; ++k)
        mMag[k] = std::sqrt(mFreq[k].r * mFreq[k].r + mFreq[k].i * mFreq[k].i);

    // spectral flux (sum of positive magnitude increases) -> onset
    float flux = 0.f;
    for (int k = 1; k < nb; ++k) {
        float d = mMag[k] - mPrevMag[k];
        if (d > 0) flux += d;
    }
    std::copy(mMag.begin(), mMag.end(), mPrevMag.begin());

    // AGC level
    mLevelPeak = std::max(mLevelPeak * 0.9995f, rms);
    float lvl = (mLevelPeak > 1e-6f) ? (rms / mLevelPeak) : 0.f;
    bool active = rms > mGate;
    if (!active) lvl = 0.f;

    // per-band energy, AGC'd
    float grp[3] = {0, 0, 0};  // bass/mid/treble accumulators (by band thirds)
    for (int b = 0; b < mNumBands; ++b) {
        float e = 0.f;
        for (int k = mBandLo[b]; k <= mBandHi[b]; ++k) e += mMag[k];
        e /= (mBandHi[b] - mBandLo[b] + 1);
        mBandPeak[b] = std::max(mBandPeak[b] * 0.999f, e);
        float v = active ? std::min(1.f, e / mBandPeak[b]) : 0.f;
        mBands[b].store(v, std::memory_order_relaxed);
        int g = b * 3 / mNumBands;
        if (g > 2) g = 2;
        grp[g] = std::max(grp[g], v);
    }
    mBass.store(grp[0], std::memory_order_relaxed);
    mMid.store(grp[1], std::memory_order_relaxed);
    mTreble.store(grp[2], std::memory_order_relaxed);
    mLevel.store(std::min(1.f, lvl), std::memory_order_relaxed);
    mActive.store(active, std::memory_order_relaxed);

    // beat: flux over an adaptive average, with a refractory period
    mFluxAvg = 0.98f * mFluxAvg + 0.02f * flux;
    double windowMs = 1000.0 * (mFftSize / 2) / mSampleRate;  // hop time
    mTimeMs += windowMs;
    float curBeat = mBeat.load(std::memory_order_relaxed) * 0.82f;  // decay
    if (active && flux > mFluxAvg * mSensitivity && (mTimeMs - mLastBeatMs) > 180.0) {
        curBeat = 1.f;
        double interval = mTimeMs - mLastBeatMs;
        mLastBeatMs = mTimeMs;
        if (interval > 250.0 && interval < 1500.0) {  // 40-240 bpm
            // snap to the running estimate if close, else converge quickly
            if (mLastInterval <= 0) mLastInterval = interval;
            else if (std::fabs(interval - mLastInterval) < 0.25 * mLastInterval)
                mLastInterval = 0.6 * mLastInterval + 0.4 * interval;  // refine
            else
                mLastInterval = 0.4 * mLastInterval + 0.6 * interval;  // jump to new tempo
            mBpm.store((float)(60000.0 / mLastInterval), std::memory_order_relaxed);
        }
    }
    mBeat.store(curBeat, std::memory_order_relaxed);
}
