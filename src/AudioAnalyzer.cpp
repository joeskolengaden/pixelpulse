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
    mNoiseMag.assign(nb, 0.f);
    mNoiseAccum.assign(nb, 0.f);
    mNoiseLearn = 0; mNoiseCount = 0; mHasNoise = false;

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
    // effectiveGain folds the manual gain with a slow per-song auto-level factor
    // so a quiet song and a loud song drive the analysis similarly.
    float eg = mGain * (mAutoLevel ? mAutoGainFactor : 1.f);

    // RMS level on the raw window (raw = pre-gain, for gate + calibration)
    double sumSq = 0.0;
    std::vector<float> w(mFftSize);
    for (int i = 0; i < mFftSize; ++i) {
        sumSq += (double)mAccum[i] * mAccum[i];
        w[i] = mAccum[i] * mHann[i] * eg;
    }
    float rms = (float)std::sqrt(sumSq / mFftSize);
    mRawLevel.store(rms, std::memory_order_relaxed);  // pre-gain/AGC, for calibration

    // adapt the auto-level factor very slowly toward a target average loudness
    if (mAutoLevel) {
        if (rms > mGate) mAvgRaw = 0.999f * mAvgRaw + 0.001f * rms;
        float desired = (mAvgRaw > 1e-4f) ? (0.06f / mAvgRaw) : 1.f;
        if (desired < 0.2f) desired = 0.2f; else if (desired > 30.f) desired = 30.f;
        mAutoGainFactor += (desired - mAutoGainFactor) * 0.001f;
    } else {
        mAutoGainFactor = 1.f;
    }

    kiss_fftr(mCfg, w.data(), mFreq.data());
    const int nb = mFftSize / 2 + 1;
    for (int k = 0; k < nb; ++k)
        mMag[k] = std::sqrt(mFreq[k].r * mFreq[k].r + mFreq[k].i * mFreq[k].i);

    // background-noise calibration: a new request starts a learn window; while
    // learning we average the spectrum (the silent background); otherwise we
    // subtract that profile so steady hum/noise doesn't drive the reactions.
    float req = mNoiseLearnReq.exchange(-1.f, std::memory_order_relaxed);
    if (req > 0.f) {
        int hop = mFftSize / 2; if (hop < 1) hop = 1;
        int wps = mSampleRate / hop; if (wps < 1) wps = 1;
        mNoiseLearn = (int)(req * wps); mNoiseCount = 0;
        std::fill(mNoiseAccum.begin(), mNoiseAccum.end(), 0.f);
    }
    if (mNoiseLearn > 0) {
        for (int k = 0; k < nb; ++k) mNoiseAccum[k] += mMag[k];
        if (++mNoiseCount, --mNoiseLearn == 0 && mNoiseCount > 0) {
            for (int k = 0; k < nb; ++k) mNoiseMag[k] = mNoiseAccum[k] / mNoiseCount;
            mHasNoise = true;
        }
    } else if (mHasNoise && mNoiseReduction > 0.f) {
        for (int k = 0; k < nb; ++k) { mMag[k] -= mNoiseMag[k] * mNoiseReduction; if (mMag[k] < 0.f) mMag[k] = 0.f; }
    }

    // spectral flux (sum of positive magnitude increases) -> onset
    float flux = 0.f;
    for (int k = 1; k < nb; ++k) {
        float d = mMag[k] - mPrevMag[k];
        if (d > 0) flux += d;
    }
    std::copy(mMag.begin(), mMag.end(), mPrevMag.begin());

    bool active = rms > mGate;

    // Level: AGC normalizes loudness to 0..1 (adapts to the room); the AGC speed
    // sets how fast the running peak recovers. AGC off = absolute, gain-driven.
    const float decay = 0.999f + 0.00099f * (1.f - mAgcSpeed);  // 0.999 (fast) .. ~0.99999 (slow)
    float lvl;
    if (mAgcEnabled) {
        mLevelPeak = std::max(mLevelPeak * decay, rms);
        lvl = (mLevelPeak > 1e-6f) ? (rms / mLevelPeak) : 0.f;
    } else {
        lvl = std::min(1.f, rms * eg * 6.0f);
    }
    if (!active) lvl = 0.f;

    // per-band energy
    float grp[3] = {0, 0, 0};  // bass/mid/treble accumulators (by band thirds)
    float bandV[MAX_BANDS] = {0};
    for (int b = 0; b < mNumBands; ++b) {
        float e = 0.f;
        for (int k = mBandLo[b]; k <= mBandHi[b]; ++k) e += mMag[k];
        e /= (mBandHi[b] - mBandLo[b] + 1);
        float v;
        if (mAgcEnabled) {
            mBandPeak[b] = std::max(mBandPeak[b] * decay, e);
            v = active ? std::min(1.f, e / mBandPeak[b]) : 0.f;
        } else {
            v = active ? std::min(1.f, e * 4.0f) : 0.f;
        }
        bandV[b] = v;
        int g = b * 3 / mNumBands;
        if (g > 2) g = 2;
        grp[g] = std::max(grp[g], v);
    }
    // per-group trim (lets the user rebalance bass / mid / treble response)
    grp[0] = std::min(1.f, grp[0] * mBassTrim);
    grp[1] = std::min(1.f, grp[1] * mMidTrim);
    grp[2] = std::min(1.f, grp[2] * mTrebleTrim);

    // Smoothing: instant attack, eased release (so reactions breathe instead of
    // flickering). mSmoothing 0 = raw, ~0.9 = slow decay.
    const float sm = mSmoothing;
    auto smo = [sm](float prev, float nw) { return nw >= prev ? nw : (prev * sm + nw * (1.f - sm)); };
    mLvlS = smo(mLvlS, std::min(1.f, lvl));
    mBassS = smo(mBassS, grp[0]);
    mMidS = smo(mMidS, grp[1]);
    mTrebS = smo(mTrebS, grp[2]);
    for (int b = 0; b < mNumBands; ++b) {
        float tg = (b < mNumBands / 3) ? mBassTrim : (b < 2 * mNumBands / 3 ? mMidTrim : mTrebleTrim);
        mBandS[b] = smo(mBandS[b], std::min(1.f, bandV[b] * tg));
        mBands[b].store(mBandS[b], std::memory_order_relaxed);
    }
    mBass.store(mBassS, std::memory_order_relaxed);
    mMid.store(mMidS, std::memory_order_relaxed);
    mTreble.store(mTrebS, std::memory_order_relaxed);
    mLevel.store(mLvlS, std::memory_order_relaxed);
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
