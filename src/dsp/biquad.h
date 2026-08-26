// Biquad filter with RBJ (Audio EQ Cookbook) coefficient design.
// Double-precision coefficient computation, single-precision processing
// with per-channel state (direct form 1 / TDF2).
#pragma once
#include "params.h"
#include <cmath>
#include <vector>

namespace stgr::dsp {

struct BiquadCoeffs {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0;
    double a1 = 0.0, a2 = 0.0;

    void identity()
    {
        b0 = 1.0; b1 = 0.0; b2 = 0.0;
        a1 = 0.0; a2 = 0.0;
    }

    bool is_identity() const
    {
        return b1 == 0.0 && b2 == 0.0 && a1 == 0.0 && a2 == 0.0 && b0 == 1.0;
    }
};

inline double clamp01(double v)
{
    return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
}

// Guard Q against values that would produce a divide-by-zero or unstable
// coefficients (very low Q), while preserving valid values above 1.
inline double clamp_q(double q)
{
    return q < 0.05 ? 0.05 : q;
}

// Design coefficients for the given filter type. Returns identity for an
// out-of-range frequency (above Nyquist or below 1 Hz).
inline BiquadCoeffs design_biquad(FilterType type, double freq, double gainDb, double q, double sampleRate)
{
    BiquadCoeffs c;
    if (sampleRate <= 0.0 || freq <= 1.0 || freq >= sampleRate * 0.5) {
        c.identity();
        return c;
    }
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * 3.14159265358979323846 * freq / sampleRate;
    const double cosw = std::cos(w0);
    const double sinw = std::sin(w0);
    const double alpha = sinw / (2.0 * clamp_q(q));

    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;

    switch (type) {
        case FilterType::Peaking:
            b0 = 1.0 + alpha * A;
            b1 = -2.0 * cosw;
            b2 = 1.0 - alpha * A;
            a0 = 1.0 + alpha / A;
            a1 = -2.0 * cosw;
            a2 = 1.0 - alpha / A;
            break;
        case FilterType::LowShelf: {
            const double sqA = 2.0 * std::sqrt(A) * alpha;
            b0 = A * ((A + 1.0) - (A - 1.0) * cosw + sqA);
            b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cosw);
            b2 = A * ((A + 1.0) - (A - 1.0) * cosw - sqA);
            a0 = (A + 1.0) + (A - 1.0) * cosw + sqA;
            a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cosw);
            a2 = (A + 1.0) + (A - 1.0) * cosw - sqA;
            break;
        }
        case FilterType::HighShelf: {
            const double sqA = 2.0 * std::sqrt(A) * alpha;
            b0 = A * ((A + 1.0) + (A - 1.0) * cosw + sqA);
            b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosw);
            b2 = A * ((A + 1.0) + (A - 1.0) * cosw - sqA);
            a0 = (A + 1.0) - (A - 1.0) * cosw + sqA;
            a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cosw);
            a2 = (A + 1.0) - (A - 1.0) * cosw - sqA;
            break;
        }
        case FilterType::HighPass:
            b0 = (1.0 + cosw) * 0.5;
            b1 = -(1.0 + cosw);
            b2 = (1.0 + cosw) * 0.5;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cosw;
            a2 = 1.0 - alpha;
            break;
        case FilterType::LowPass:
            b0 = (1.0 - cosw) * 0.5;
            b1 = 1.0 - cosw;
            b2 = (1.0 - cosw) * 0.5;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cosw;
            a2 = 1.0 - alpha;
            break;
        case FilterType::Notch:
            b0 = 1.0;
            b1 = -2.0 * cosw;
            b2 = 1.0;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cosw;
            a2 = 1.0 - alpha;
            break;
    }

    const double inv = 1.0 / a0;
    c.b0 = b0 * inv; c.b1 = b1 * inv; c.b2 = b2 * inv;
    c.a1 = a1 * inv; c.a2 = a2 * inv;
    return c;
}

// State for one channel. TDF2 (transposed direct form 2) with double
// accumulation to stay stable and quiet.
struct BiquadState {
    double z1 = 0.0, z2 = 0.0;

    void reset() { z1 = z2 = 0.0; }

    inline float process(const BiquadCoeffs& c, float x)
    {
        const double y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return static_cast<float>(y);
    }
};

// A complete multi-channel biquad instance.
class Biquad {
public:
    void configure(FilterType type, double freq, double gainDb, double q, double sampleRate, int channels)
    {
        coeffs_ = design_biquad(type, freq, gainDb, q, sampleRate);
        if (channels != channels_) {
            channels_ = channels;
            state_.assign(channels, BiquadState{});
        }
    }

    void reset()
    {
        for (auto& s : state_) s.reset();
    }

    inline void process(float* buf, int frames, int channels)
    {
        const BiquadCoeffs& c = coeffs_;
        if (c.is_identity()) return;
        for (int ch = 0; ch < channels; ++ch) {
            BiquadState& s = state_[ch];
            for (int f = 0; f < frames; ++f) {
                buf[f * channels + ch] = s.process(c, buf[f * channels + ch]);
            }
        }
    }

private:
    BiquadCoeffs coeffs_;
    int channels_ = 0;
    std::vector<BiquadState> state_;
};

} // namespace stgr::dsp
