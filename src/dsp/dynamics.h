// Dynamics processors: noise gate, expander, compressor, limiter.
// All processors are per-channel and real-time safe (no allocation).
#pragma once
#include <cmath>
#include <vector>
#include <algorithm>

namespace stgr::dsp {

inline float db_to_linear(float db) { return std::pow(10.0f, db / 20.0f); }
inline float linear_to_db(float v)
{
    return (v <= 0.0f) ? -120.0f : 20.0f * std::log10(v);
}
inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

// Log-domain smoothing used for gain changes (no zipper noise).
class SmoothGain {
public:
    void configure(double sampleRate, float attackMs, float releaseMs)
    {
        const double fs = sampleRate > 0 ? sampleRate : 48000.0;
        attackCoef_ = attackMs > 0 ? std::exp(-1.0 / (fs * attackMs * 0.001)) : 0.0;
        releaseCoef_ = releaseMs > 0 ? std::exp(-1.0 / (fs * releaseMs * 0.001)) : 0.0;
    }
    void reset(float value = 1.0f) { current_ = value; }
    // Process one smoothed sample toward target. Returns applied gain.
    inline float tick(float target)
    {
        const float coef = (target < current_) ? releaseCoef_ : attackCoef_;
        current_ = coef * current_ + (1.0f - coef) * target;
        return current_;
    }
    float current() const { return current_; }

private:
    double attackCoef_ = 0.0;
    double releaseCoef_ = 0.0;
    float current_ = 1.0f;
};

// Per-sample envelope follower (peak, with log-domain attack/release).
class EnvelopeFollower {
public:
    void configure(double sampleRate, float attackMs, float releaseMs)
    {
        const double fs = sampleRate > 0 ? sampleRate : 48000.0;
        attackCoef_ = attackMs > 0 ? std::exp(-1.0 / (fs * attackMs * 0.001)) : 0.0;
        releaseCoef_ = releaseMs > 0 ? std::exp(-1.0 / (fs * releaseMs * 0.001)) : 0.0;
    }
    void reset() { env_ = 0.0; }
    inline float tick(float x)
    {
        const double absx = std::fabs((double)x);
        const double coef = (absx > env_) ? attackCoef_ : releaseCoef_;
        env_ = coef * env_ + (1.0 - coef) * absx;
        return (float)env_;
    }
    float value() const { return (float)env_; }

private:
    double attackCoef_ = 0.0;
    double releaseCoef_ = 0.0;
    double env_ = 0.0;
};

// Noise gate. Below threshold the gain falls to 'range' (dB).
class Gate {
public:
    void configure(double sampleRate, float thresholdDb, float attackMs, float releaseMs,
                   float holdMs, float rangeDb)
    {
        follower_.configure(sampleRate, attackMs, releaseMs);
        smooth_.configure(sampleRate, attackMs, releaseMs);
        threshold_ = db_to_linear(thresholdDb);
        rangeGain_ = db_to_linear(rangeDb);
        holdFrames_ = (int)(sampleRate * holdMs * 0.001);
        open_ = false;
    }
    void reset()
    {
        follower_.reset();
        smooth_.reset(1.0f);
        open_ = false;
        holdCount_ = 0;
    }
    inline void process(float* buf, int frames, int channels, int ch)
    {
        for (int f = 0; f < frames; ++f) {
            float* s = &buf[f * channels + ch];
            const float env = follower_.tick(*s);
            if (!open_) {
                if (env >= threshold_) { open_ = true; holdCount_ = holdFrames_; }
            } else {
                if (env < threshold_) {
                    if (holdCount_ > 0) --holdCount_;
                    else open_ = false;
                } else {
                    holdCount_ = holdFrames_;
                }
            }
            const float target = open_ ? 1.0f : rangeGain_;
            *s *= smooth_.tick(target);
        }
    }

private:
    EnvelopeFollower follower_;
    SmoothGain smooth_;
    float threshold_ = 0.001f;
    float rangeGain_ = 0.001f;
    int holdFrames_ = 0;
    int holdCount_ = 0;
    bool open_ = false;
};

// Expander: below threshold, gain reduces by (1 - 1/ratio) per dB under.
class Expander {
public:
    void configure(double sampleRate, float thresholdDb, float ratio, float attackMs,
                   float releaseMs, float rangeDb)
    {
        follower_.configure(sampleRate, attackMs, releaseMs);
        smooth_.configure(sampleRate, attackMs, releaseMs);
        threshold_ = db_to_linear(thresholdDb);
        slope_ = 1.0f - 1.0f / clampf(ratio, 1.0f, 50.0f);
        maxReduction_ = db_to_linear(rangeDb);
    }
    void reset()
    {
        follower_.reset();
        smooth_.reset(1.0f);
    }
    inline void process(float* buf, int frames, int channels, int ch)
    {
        for (int f = 0; f < frames; ++f) {
            float* s = &buf[f * channels + ch];
            const float env = follower_.tick(*s);
            float gain = 1.0f;
            if (env < threshold_) {
                const float below = linear_to_db(env / threshold_); // <= 0
                float red = below * slope_;
                if (red < linear_to_db(maxReduction_)) red = linear_to_db(maxReduction_);
                gain = db_to_linear(red);
            }
            *s *= smooth_.tick(gain);
        }
    }

private:
    EnvelopeFollower follower_;
    SmoothGain smooth_;
    float threshold_ = 0.01f;
    float slope_ = 0.5f;
    float maxReduction_ = 0.001f;
};

// Compressor: above threshold, gain reduces by (1 - 1/ratio) per dB over,
// with optional soft knee.
class Compressor {
public:
    void configure(double sampleRate, float thresholdDb, float ratio, float attackMs,
                   float releaseMs, float kneeDb, float makeupDb)
    {
        follower_.configure(sampleRate, attackMs, releaseMs);
        smooth_.configure(sampleRate, attackMs, releaseMs);
        threshold_ = db_to_linear(thresholdDb);
        knee_ = db_to_linear(kneeDb);
        kneeHalf_ = kneeDb > 0.0f ? kneeDb * 0.5f : 0.0f;
        slope_ = 1.0f - 1.0f / clampf(ratio, 1.0f, 50.0f);
        makeup_ = db_to_linear(makeupDb);
    }
    void reset()
    {
        follower_.reset();
        smooth_.reset(1.0f);
    }
    inline void process(float* buf, int frames, int channels, int ch)
    {
        for (int f = 0; f < frames; ++f) {
            float* s = &buf[f * channels + ch];
            const float env = follower_.tick(*s);
            float gain = 1.0f;
            const float envDb = linear_to_db(env);
            const float thrDb = linear_to_db(threshold_);
            if (envDb > thrDb - kneeHalf_ && kneeHalf_ > 0.0f) {
                // soft knee region
                const float over = envDb - (thrDb - kneeHalf_);
                float red = 0.0f;
                if (over > 0.0f && over < 2.0f * kneeHalf_) {
                    const float t = over / (2.0f * kneeHalf_);
                    red = slope_ * (over * over) / (4.0f * kneeHalf_); // parabola
                    (void)t;
                } else if (over >= 2.0f * kneeHalf_) {
                    red = slope_ * (over - kneeHalf_);
                }
                gain = db_to_linear(-red);
            } else if (kneeHalf_ == 0.0f && envDb > thrDb) {
                const float red = (envDb - thrDb) * slope_;
                gain = db_to_linear(-red);
            }
            *s *= smooth_.tick(gain) * makeup_;
        }
    }

private:
    EnvelopeFollower follower_;
    SmoothGain smooth_;
    float threshold_ = 0.5f;
    float knee_ = 1.0f;
    float kneeHalf_ = 0.0f;
    float slope_ = 0.667f;
    float makeup_ = 1.0f;
};

// Peak limiter: fast attack, slower release, gain reduction to keep the
// signal below the ceiling. Also contains a final hard safety clip at 0 dBFS.
class Limiter {
public:
    void configure(double sampleRate, float ceilingDb, float attackMs, float releaseMs)
    {
        follower_.configure(sampleRate, attackMs, releaseMs);
        smooth_.configure(sampleRate, attackMs, releaseMs);
        ceiling_ = db_to_linear(ceilingDb);
        ceilingDb_ = ceilingDb;
    }
    void reset()
    {
        follower_.reset();
        smooth_.reset(1.0f);
    }
    inline void process(float* buf, int frames, int channels, int ch)
    {
        for (int f = 0; f < frames; ++f) {
            float* s = &buf[f * channels + ch];
            const float env = follower_.tick(*s);
            float gain = 1.0f;
            if (env > ceiling_) {
                const float red = linear_to_db(env / ceiling_); // positive dB
                gain = db_to_linear(-red);
            }
            *s *= smooth_.tick(gain);
            // Safety clip (soft) at 0 dBFS.
            if (*s > 1.0f) *s = 1.0f;
            else if (*s < -1.0f) *s = -1.0f;
        }
    }
    float ceiling_db() const { return ceilingDb_; }

private:
    EnvelopeFollower follower_;
    SmoothGain smooth_;
    float ceiling_ = 0.891f;
    float ceilingDb_ = -1.0f;
};

} // namespace stgr::dsp
