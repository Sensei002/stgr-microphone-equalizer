# DSP engine

Pure C++ (no Windows dependencies) real-time processing chain in `src/dsp/`.

## Chain order (configurable)

```
Input gain → filters → 10-band EQ → plugin slots → dynamics → limiter → output
```

Each stage is a `dsp::Processor`; the chain is built off-line into an
immutable `EngineConfig` and swapped atomically.

## Stages

| Stage | Parameters | Implementation |
|---|---|---|
| Gain | gainDb | smoothed (5 ms log-domain ramp, no zipper) |
| HighPass / LowPass / Peaking / Notch / LowShelf / HighShelf | freq, gainDb, Q | RBJ biquad, TDF2, double-precision coefficients, per-channel state |
| Parametric EQ (10 bands) | per-band enable/freq/gain/Q/type | cascaded biquads, serial band processing |
| Noise gate | threshold, attack, release, hold, range | peak envelope follower + smoothed gain |
| Expander | threshold, ratio, attack, release, range | slope (1 - 1/ratio) below threshold |
| Compressor | threshold, ratio, attack, release, knee, makeup | soft-knee option, log-domain smoothing |
| Limiter | ceiling, attack, release | fast-attack peak limiter + final 0 dBFS safety clip |
| Plugin | instanceId/path/params | forwarded to the plugin bridge (APO) or the VST host (server) |

## Real-time rules

- No allocation, I/O, locks, or system calls in the processing path.
- Denormals flushed (SSE `FTZ|DAZ`); `enable_flush_to_zero()` is applied on
  the audio thread.
- Sample rate is never hard-coded: every coefficient is designed from the
  actual stream sample rate (44.1/48/88.2/96 kHz).
- Mono and stereo both supported; per-channel state arrays.

## Latency

- All built-in stages are minimum-phase IIR: algorithmic latency is
  effectively 0 samples.
- The plugin bridge adds one audio block (~10 ms at 480-frame blocks)
  reported in the GUI and via `IAudioProcessingObject::GetLatency`.

## Tests

`tests/test_dsp.cpp` verifies: identity biquad, high-pass DC rejection,
low-pass steady state, gate open/close, compressor reduction, limiter
ceiling, engine gain, multi-stage stability (impulse), sine sanity (no
NaN/inf, bounded), denormal robustness.

`tests/bench/STGRBench.exe` measures per-block processing time for a
typical voice chain (HP + EQ + compressor + limiter) at 48 kHz mono/stereo;
results are uploaded in CI artifacts.
