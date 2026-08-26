// STGR DSP performance benchmark (headless).
// Measures processing time for a typical chain on this machine and prints
// results; used by CI to report numbers in artifacts.
#include "../src/dsp/engine.h"
#include <windows.h>
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>

using namespace stgr::dsp;

static double now_ms()
{
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return c.QuadPart * 1000.0 / f.QuadPart;
}

int main()
{
    // Build a typical voice chain: HP -> EQ10 (4 bands) -> Compressor -> Limiter.
    std::vector<StageParams> chain;
    {
        StageParams hp;
        hp.type = StageType::HighPass;
        hp.enabled = true;
        hp.freq = 80.0f;
        chain.push_back(hp);

        StageParams eq;
        eq.type = StageType::Eq10;
        eq.enabled = true;
        eq.bands[0] = {true, FilterType::Peaking, 200, -2, 0.707};
        eq.bands[1] = {true, FilterType::Peaking, 1000, 2, 0.707};
        eq.bands[2] = {true, FilterType::Peaking, 4000, 3, 0.707};
        eq.bands[3] = {true, FilterType::Peaking, 12000, 1, 0.707};
        chain.push_back(eq);

        StageParams comp;
        comp.type = StageType::Compressor;
        comp.enabled = true;
        comp.thresholdDb = -20.0f;
        comp.ratio = 3.0f;
        chain.push_back(comp);

        StageParams lim;
        lim.type = StageType::Limiter;
        lim.enabled = true;
        lim.ceilingDb = -1.0f;
        chain.push_back(lim);
    }

    ProcessingEngine engine;
    auto cfg = build_config(chain, 48000.0, 1, nullptr);
    engine.set_config(std::move(cfg), 0);

    constexpr int kBlockFrames = 480;
    constexpr int kBlocks = 10000;
    std::vector<float> buf(kBlockFrames);

    // Warmup.
    for (int i = 0; i < 100; ++i) engine.process(buf.data(), kBlockFrames);

    const double t0 = now_ms();
    for (int i = 0; i < kBlocks; ++i)
        engine.process(buf.data(), kBlockFrames);
    const double t1 = now_ms();

    const double totalMs = t1 - t0;
    const double audioSec = kBlocks * kBlockFrames / 48000.0;
    const double percentCpu = totalMs / (audioSec * 1000.0) * 100.0;
    const double perBlockUs = totalMs * 1000.0 / kBlocks;

    printf("=== STGR DSP benchmark ===\n");
    printf("Chain: HP80 + EQ10(4 bands) + Compressor + Limiter\n");
    printf("Format: 48 kHz, mono, 480-frame blocks\n");
    printf("Blocks: %d (%.2f s audio)\n", kBlocks, audioSec);
    printf("Total processing: %.2f ms\n", totalMs);
    printf("Per block: %.2f us\n", perBlockUs);
    printf("CPU usage (single core, %.2f%% of one core)\n", percentCpu);

    // Mono -> stereo comparison.
    auto cfgS = build_config(chain, 48000.0, 2, nullptr);
    engine.set_config(std::move(cfgS), 0);
    std::vector<float> bufS(kBlockFrames * 2);
    for (int i = 0; i < 100; ++i) engine.process(bufS.data(), kBlockFrames);
    const double t2 = now_ms();
    for (int i = 0; i < kBlocks; ++i)
        engine.process(bufS.data(), kBlockFrames);
    const double t3 = now_ms();
    printf("Stereo per block: %.2f us\n", (t3 - t2) * 1000.0 / kBlocks);

    // Memory footprint of the engine.
    printf("Engine buffer: %zu bytes\n", sizeof(ProcessingEngine));
    printf("(APO working set is measured separately in the release notes)\n");
    return 0;
}
