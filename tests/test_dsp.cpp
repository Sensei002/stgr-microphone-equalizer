// DSP engine unit tests.
#include "../src/dsp/engine.h"
#include "../src/dsp/biquad.h"
#include "../src/dsp/dynamics.h"
#include "../src/dsp/params.h"
#include "test_util.h"
#include <cmath>
#include <cstring>
#include <algorithm>

using namespace stgr::dsp;

// Magnitude response of a TDF2 biquad at normalized frequency w.
static double biquad_mag(const BiquadCoeffs& c, double w)
{
    const double cos1 = std::cos(-w), sin1 = std::sin(-w);
    const double cos2 = std::cos(-2.0 * w), sin2 = std::sin(-2.0 * w);
    const double nre = c.b0 + c.b1 * cos1 + c.b2 * cos2;
    const double nim = c.b1 * sin1 + c.b2 * sin2;
    const double dre = 1.0 + c.a1 * cos1 + c.a2 * cos2;
    const double dim = c.a1 * sin1 + c.a2 * sin2;
    return std::sqrt(nre * nre + nim * nim) / std::sqrt(dre * dre + dim * dim);
}

void test_biquad_identity()
{
    BiquadCoeffs c;
    CHECK(c.is_identity(), "identity coeffs");
    BiquadState s;
    float in[4] = {1.0f, -1.0f, 0.5f, -0.5f};
    for (int i = 0; i < 4; ++i) {
        float out = s.process(c, in[i]);
        CHECK_CLOSE(out, in[i], 1e-7f, "identity process");
    }
}

void test_biquad_design()
{
    BiquadCoeffs c = design_biquad(FilterType::HighPass, 100.0, 0.0, 0.707, 48000.0);
    CHECK(!c.is_identity(), "highpass designed");
    // DC should be zeroed (high pass).
    double dc = biquad_mag(c, 0.0);
    CHECK_CLOSE(dc, 0.0, 1e-6, "high pass DC");
    // Nyquist should pass.
    double ny = biquad_mag(c, 3.14159265358979);
    CHECK_CLOSE(ny, 1.0, 1e-4, "high pass nyquist");
}

void test_lowpass_steady()
{
    BiquadCoeffs c = design_biquad(FilterType::LowPass, 1000.0, 0.0, 0.707, 48000.0);
    BiquadState s;
    float in = 0.5f;
    float out = 0.0f;
    for (int i = 0; i < 1000; ++i) out = s.process(c, in);
    CHECK_CLOSE(out, 0.5f, 0.001f, "lowpass steady state");
}

void test_gain_smoothed()
{
    SmoothGain sg;
    sg.configure(48000.0, 1.0f, 1.0f);
    sg.reset(1.0f);
    float g = sg.tick(0.5f);
    CHECK(g < 1.0f && g > 0.5f, "gain smoothing in range");
}

void test_gate()
{
    Gate gate;
    gate.configure(48000.0, -40.0f, 0.1f, 50.0f, 0.0f, -60.0f);
    gate.reset();
    float buf[480] = {};
    // Silence stays silent.
    gate.process(buf, 480, 1, 0);
    CHECK_CLOSE(buf[0], 0.0f, 1e-7f, "gate silence");
    // A loud signal opens the gate.
    std::fill_n(buf, 480, 0.5f);
    gate.process(buf, 480, 1, 0);
    CHECK_CLOSE(buf[479], 0.5f, 0.1f, "gate opens");
    // Lower signal eventually closes.
    std::fill_n(buf, 480, 0.001f);
    gate.process(buf, 480, 1, 0);
    CHECK(buf[479] < 0.001f, "gate closed");
}

void test_compressor()
{
    Compressor comp;
    comp.configure(48000.0, -20.0f, 4.0f, 1.0f, 50.0f, 0.0f, 0.0f);
    comp.reset();
    float buf[480] = {};
    std::fill_n(buf, 480, 0.5f);
    comp.process(buf, 480, 1, 0);
    // Output should be compressed: less than input.
    CHECK(buf[479] < 0.5f, "compressor reduces gain");
    CHECK(buf[479] > 0.0f, "compressor positive");
    CHECK(!std::isnan(buf[479]), "no NaN");
    CHECK(!std::isinf(buf[479]), "no inf");
}

void test_limiter()
{
    Limiter lim;
    lim.configure(48000.0, -1.0f, 0.5f, 80.0f);
    lim.reset();
    float buf[480] = {};
    std::fill_n(buf, 480, 2.0f); // way above 0 dBFS
    lim.process(buf, 480, 1, 0);
    CHECK(buf[479] <= 1.0f, "limiter clips to 1.0");
    CHECK(buf[479] >= -1.0f, "limiter clip below -1.0");
    CHECK(!std::isnan(buf[479]), "no NaN");
    CHECK(!std::isinf(buf[479]), "no inf");
}

void test_engine()
{
    ProcessingEngine engine;
    std::vector<StageParams> chain;
    StageParams gain;
    gain.type = StageType::Gain;
    gain.enabled = true;
    gain.gainDb = -6.0f; // 0.5 linear
    chain.push_back(gain);

    auto cfg = build_config(chain, 48000.0, 1, nullptr);
    engine.set_config(std::move(cfg), 0);

    float buf[480] = {};
    // The gain stage ramps over ~5 ms (anti-zipper); process three fresh
    // blocks (~30 ms) so the smoothed gain converges before we assert.
    float steady = 0.0f;
    for (int i = 0; i < 3; ++i) {
        std::fill_n(buf, 480, 1.0f);
        engine.process(buf, 480);
        steady = buf[479];
    }
    CHECK_CLOSE(steady, 0.5f, 0.01f, "engine -6 dB gain");
    CHECK(!std::isnan(buf[0]), "no NaN");
    CHECK(!std::isinf(buf[0]), "no inf");
}

void test_engine_multi_stage()
{
    ProcessingEngine engine;
    std::vector<StageParams> chain;
    StageParams hp;
    hp.type = StageType::HighPass;
    hp.enabled = true;
    hp.freq = 80.0f;
    hp.q = 0.707f;
    chain.push_back(hp);

    StageParams comp;
    comp.type = StageType::Compressor;
    comp.enabled = true;
    comp.thresholdDb = -20.0f;
    comp.ratio = 4.0f;
    chain.push_back(comp);

    StageParams lim;
    lim.type = StageType::Limiter;
    lim.enabled = true;
    lim.ceilingDb = -1.0f;
    chain.push_back(lim);

    auto cfg = build_config(chain, 48000.0, 1, nullptr);
    engine.set_config(std::move(cfg), 0);

    // Impulse test.
    float buf[480] = {};
    buf[0] = 0.8f;
    engine.process(buf, 480);
    // Should not explode.
    for (float s : buf) {
        CHECK(!std::isnan(s), "no NaN");
        CHECK(!std::isinf(s), "no inf");
        CHECK(s >= -1.0f && s <= 1.0f, "in range");
    }
}

void test_sine_through()
{
    // 1 kHz sine at 48 kHz, 512 samples, check no NaN/inf.
    ProcessingEngine engine;
    std::vector<StageParams> chain;
    StageParams eq;
    eq.type = StageType::Eq10;
    eq.enabled = true;
    eq.bands[0] = {true, FilterType::Peaking, 1000, 6, 0.707};
    chain.push_back(eq);

    StageParams lim;
    lim.type = StageType::Limiter;
    lim.enabled = true;
    chain.push_back(lim);

    auto cfg = build_config(chain, 48000.0, 1, nullptr);
    engine.set_config(std::move(cfg), 0);

    float buf[512] = {};
    for (int i = 0; i < 512; ++i)
        buf[i] = 0.3f * std::sin(2.0f * 3.14159265f * 1000.0f * i / 48000.0f);
    engine.process(buf, 512);
    for (float s : buf) {
        CHECK(!std::isnan(s), "sine no NaN");
        CHECK(!std::isinf(s), "sine no inf");
        CHECK(s >= -1.0f && s <= 1.0f, "sine in range");
    }
}

void test_denormal()
{
    // Very small values should not cause denormal stalls.
    float v = 1e-30f;
    BiquadCoeffs c = design_biquad(FilterType::LowPass, 1000.0, 0.0, 0.707, 48000.0);
    BiquadState s;
    float out = 0.0f;
    for (int i = 0; i < 100; ++i)
        out = s.process(c, 1e-30f);
    CHECK(!std::isnan(out), "denormal no NaN");
    CHECK(!std::isinf(out), "denormal no inf");
}

int main()
{
    printf("=== DSP tests ===\n");
    test_biquad_identity();
    test_biquad_design();
    test_lowpass_steady();
    test_gain_smoothed();
    test_gate();
    test_compressor();
    test_limiter();
    test_engine();
    test_engine_multi_stage();
    test_sine_through();
    test_denormal();
    return stgr::test::test_summary();
}