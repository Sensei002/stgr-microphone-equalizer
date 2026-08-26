# Performance

## Targets (from the master specification)

| Metric | Target | Implementation |
|---|---|---|
| APO idle CPU | ~0% when no audio | the APO only runs when a capture stream is active |
| APO working set | minimal (well under 5 MB baseline) | no GUI, no plugin GUI, no config DBs; only the DSP chain + config watcher |
| Built-in DSP latency | ~0 samples | minimum-phase IIR, no lookahead |
| Plugin path latency | 1 block + plugin latency | shared-memory bridge, single-block pipeline |
| GUI memory | excluded from the background footprint | GUI is a separate process, closed by default |

## What actually consumes memory (distinct components)

1. **stgr_apo.dll** — loaded in `audiodg.exe`: DSP chain buffers (fixed
   small allocations) + config watcher. The chain allocates no per-block
   memory.
2. **STGRAudioServer.exe** — plugin host: scratch buffers + the loaded
   third-party plugins (their memory is their own).
3. **STGRTray.exe** — small icon + menu resources.
4. **Windows audio subsystem** — inherent to using WASAPI/APO.

The spec's "<5 MB" claim applies to STGR's own background code, measured by
the benchmark harness; third-party plugin memory is separate and reported
by the server in Diagnostics.

## Benchmark

`STGRBench.exe` (run in CI) measures a typical voice chain
(HP 80 Hz + 10-band EQ with 4 bands + compressor + limiter) at 48 kHz:

- per-block processing time (µs)
- CPU % of one core
- mono vs stereo

Results are uploaded as CI artifacts (`benchmark-report`).

## Idle behavior

- No polling loops in the server (event-driven with a 1 s wake for config
  polling).
- The APO watcher sleeps on the config-change event (1 s timeout).
- When the microphone is unused, Windows does not run the APO at all.
