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

    // Onset function: log-compressed spectral flux with high-frequency emphasis.
    // Log compression lets quieter beats register (not only the loudest sound) and
    // the HF weight makes percussive transients (kick/snare/hats) stand out - far
    // more reliable than raw-magnitude flux on a faint acoustic signal. mPrevMag
    // now holds the previous log-spectrum.
    float flux = 0.f;
    for (int k = 1; k < nb; ++k) {
        float lm = std::log1p(24.f * mMag[k]);
        float d = lm - mPrevMag[k];
        if (d > 0.f) flux += d * (0.4f + 0.6f * (float)k / nb);
        mPrevMag[k] = lm;
    }

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

    // Beat: onset flux over a fast adaptive threshold, with phase-expectation
    // gating (the locked tempo lowers the bar near a predicted beat, so we catch
    // it even when the onset is marginal) and a refractory period.
    mFluxAvg = 0.90f * mFluxAvg + 0.10f * flux;   // faster local adaptation
    double hopSec = (double)(mFftSize / 2) / mSampleRate;
    mTimeMs += hopSec * 1000.0;
    float curBeat = mBeat.load(std::memory_order_relaxed) * 0.82f;  // decay
    bool onsetFired = false;
    float sens = mSensitivity;
    float phNow = mBeatPhase - std::floor(mBeatPhase);
    if (phNow < 0.10f || phNow > 0.90f) sens *= 0.55f;   // a beat is expected here
    if (active && flux > mFluxAvg * sens + 0.015f && (mTimeMs - mLastBeatMs) > 150.0) {
        curBeat = 1.f; mLastBeatMs = mTimeMs; onsetFired = true;
    }
    mBeat.store(curBeat, std::memory_order_relaxed);

    if (onsetFired) mOnsetCount.fetch_add(1, std::memory_order_relaxed);
    // --- tempo tracking: autocorrelate the onset-strength history ---
    // feed the "novelty" (flux above its local average) so the impulse train is
    // sharp -> the autocorrelation peaks are crisp -> a more confident tempo lock
    mOnset[mOnsetHead] = active ? std::max(0.f, flux - mFluxAvg) : 0.f;
    mOnsetHead = (mOnsetHead + 1) % ONSET_N;
    if (++mTempoTick >= 24) {   // recompute ~3-4x/sec
        mTempoTick = 0;
        float lin[ONSET_N];
        for (int j = 0; j < ONSET_N; ++j) lin[j] = mOnset[(mOnsetHead + j) % ONSET_N];
        int lagMin = (int)std::floor(60.0 / (185.0 * hopSec));
        int lagMax = (int)std::ceil(60.0 / (60.0 * hopSec));
        if (lagMin < 2) lagMin = 2;
        if (lagMax > ONSET_N / 2) lagMax = ONSET_N / 2;
        float best = 0.f, bestLag = 0.f, total = 1e-6f;
        for (int lag = lagMin; lag <= lagMax; ++lag) {
            float ac = 0.f;
            for (int i = 0; i + lag < ONSET_N; ++i) ac += lin[i] * lin[i + lag];
            float bpm = (float)(60.0 / (lag * hopSec));
            float w = std::exp(-((bpm - 120.f) * (bpm - 120.f)) / (2.f * 55.f * 55.f));  // bias toward musical tempos
            float wac = ac * (0.4f + 0.6f * w);
            total += ac;
            if (wac > best) { best = wac; bestLag = (float)lag; }
        }
        if (bestLag > 0.f && active) {
            float newBpm = (float)(60.0 / (bestLag * hopSec));
            // octave-fold toward the current lock so it stays stable through jitter,
            // but accept a raw re-lock if the estimate is genuinely far from the lock
            float folded = newBpm;
            if (newBpm * 2.f <= 200.f && std::fabs(newBpm * 2.f - mTempoBpm) < std::fabs(folded - mTempoBpm)) folded = newBpm * 2.f;
            if (newBpm * 0.5f >= 50.f && std::fabs(newBpm * 0.5f - mTempoBpm) < std::fabs(folded - mTempoBpm)) folded = newBpm * 0.5f;
            float useBpm = (std::fabs(folded - mTempoBpm) < 0.20f * mTempoBpm) ? folded : newBpm;
            float conf = best / total;
            float a = 0.10f + 0.30f * std::min(1.f, conf * 8.f);  // lock faster when confident
            mTempoBpm = mTempoBpm * (1.f - a) + useBpm * a;
            if (mTempoBpm < 50.f) mTempoBpm = 50.f;
            if (mTempoBpm > 200.f) mTempoBpm = 200.f;
            mTempoConfA.store(std::min(1.f, conf * 8.f), std::memory_order_relaxed);
        }
    }
    mBpm.store(mTempoBpm, std::memory_order_relaxed);

    // --- beat-phase clock (PLL): advance at tempo, nudge to land on onsets ---
    mBeatPhase += (float)(hopSec * mTempoBpm / 60.0);
    if (onsetFired) {  // pull the phase so this onset sits on a beat (phase 0)
        float p = mBeatPhase - std::floor(mBeatPhase);
        float err = (p < 0.5f) ? p : (p - 1.f);
        mBeatPhase -= 0.12f * err;
    }
    if (mBeatPhase >= 1.f) { mBeatPhase -= std::floor(mBeatPhase); mBeatNum++; }
    if (mBeatPhase < 0.f) mBeatPhase += 1.f;
    mBeatPhaseA.store(mBeatPhase - std::floor(mBeatPhase), std::memory_order_relaxed);
    mBeatNumA.store(mBeatNum, std::memory_order_relaxed);
}
