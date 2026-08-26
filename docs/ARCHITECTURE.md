# STGR Microphone Equalizer - Architecture

## 1. Design goals

- Attach real-time microphone processing to the **existing physical
  microphone endpoint**; never create a virtual microphone.
- Processing must survive GUI close, logoff/logon and reboot: the audio
  engine itself must host the processing.
- Near-zero added latency for built-in DSP; plugin processing isolated in a
  separate process with crash containment.
- Tiny footprint: the background components (APO + server) stay under a few
  MB of working set and ~0% CPU when idle.

## 2. The audio path

```
USB/wired microphone
   ↓  (WASAPI capture stream, 44.1/48/88.2/96 kHz, mono/stereo float32)
Windows audio engine (audiodg.exe)
   ↓  SFX (Stream Effects) APO  ← stgr_apo.dll
      ├─ config hot-reload (watcher thread)
      ├─ built-in DSP chain (gain → filters → EQ → dynamics → limiter)
      └─ plugin stages → shared-memory bridge → STGRAudioServer.exe
                          (VST2/VST3 host, out-of-process)
   ↓
same microphone endpoint (unchanged ID)
   ↓
Discord / OBS / Teams / Zoom / browsers / games
```

### APO type selection (researched, not guessed)

From the Windows driver documentation
(*Audio Processing Object Architecture*, *Implementing Audio Processing
Objects*) there are three APO positions:

| APO | Placement | Notes for capture |
|---|---|---|
| SFX (stream effects) | after the tee on capture; one instance per stream | **chosen**; matches the Equalizer-APO-style behavior; not loaded in RAW mode (documented limitation) |
| MFX (mode effects) | before the tee on capture; one instance per mode | loaded even in RAW mode |
| EFX (endpoint effects) | always, even for raw streams | not a supported software position for capture streaming |

SFX is the correct, supported position for per-stream microphone effects and
is what the SYSVAD SwapAPO sample implements as its SFX class.

### APO lifecycle

1. The audio engine instantiates `stgr_apo.dll` (COM class
   `{6F3D2C1E-9A84-4B5A-8D6B-0C1E2F3A4B5C}`) when a stream opens on an
   endpoint whose FX property store lists our `PKEY_FX_StreamEffectClsid`.
2. `Initialize()` receives `APOInitSystemEffects` / `2` / `3`; we read the
   endpoint id from `pDeviceCollection` (last device) and the processing
   mode. RAW mode is bypassed.
3. `LockForProcess()` caches the stream format (float32); `APOProcess()`
   runs the chain.
4. A watcher thread (started at `Initialize`) reloads the endpoint's JSON
   config whenever it changes and swaps the DSP chain safely.

### Persistence

The GUI writes JSON config + signals the named event `Local\STGR_CfgChanged`.
The APO polls the config file (mtime + event) and rebuilds its chain
outside the real-time path. Closing the GUI does nothing to the APO. After a
reboot, the audio engine loads the APO from the registered endpoint FX
store and the watcher picks up the same config file.

## 3. Component map

```
src/
  common/    version, paths, logging (GUI/server/tray only)
  dsp/       biquads, EQ, dynamics, limiter, gain, engine (pure C++)
  config/    JSON parser, schema (v1), presets, config manager
  devices/   MMDevice capture endpoint enumeration
  bridge/    shared-memory SPSC rings + protocol (APO ↔ server)
  apo/       stgr_apo.dll (COM APO) + registration helpers
  plugins/   VST2 host (own ABI impl), VST3 host (VST3 SDK headers),
             plugin cache/scanning
  server/    STGRAudioServer.exe (plugin bridge host)
  gui/       STGRMicrophoneEqualizer.exe (Win32 config GUI)
  tray/      STGRTray.exe
  tools/     STGRAdmin.exe (elevated), STGRScan.exe (isolated scan)
tests/       unit tests + benchmark
installer/   Inno Setup script
```

## 4. Real-time safety

- The APO path performs **no** allocation, I/O, locking, registry or GUI
  calls. All buffers are preallocated.
- Config swaps use a pointer exchange plus deferred deletion (retired
  configs are freed after two audio callbacks, tracked by a sequence
  counter incremented in `APOProcess`).
- Denormals are flushed to zero (SSE `FTZ|DAZ`) on the audio thread.
- The plugin bridge is lock-free (SPSC rings with monotonic frame counters
  and per-block sequence tags).

## 5. The plugin bridge (one-block pipeline)

```
APO (real-time)                        STGRAudioServer.exe
─────────────────                      ────────────────────
push block N (seq)  ────────────────▶  pull block N
pull block N-1 result ◀───────────────  run VST/VST3 chain
                                        push result (seq N)
```

- Rings: `Local\STGR_<endpoint>` shared section, capacity 8 blocks.
- Block sequence tags make both sides detect resynchronization after server
  restart or format change; mismatches reset the rings and bypass one block.
- The server marks `serverState` (absent/starting/running/error) and reports
  plugin latency (µs) + meters in the section header. The GUI reads them
  read-only.
- If the server is absent or stalled, the APO **bypasses plugin stages** and
  the built-in chain keeps running — the microphone never goes dead.

## 6. Why no virtual microphone

A virtual endpoint would change the device users must select in Discord/OBS
and would require a signed kernel driver. The APO approach keeps the
original endpoint, needs no driver, and works at the audio engine level so
every application benefits automatically.

## 7. Known limitations (documented honestly)

- **RAW processing mode** streams bypass SFX APOs on Windows 10 (per
  Microsoft's documented behavior). Applications that opt into RAW mode do
  not get STGR processing. This is a platform behavior, not a defect.
- **APO signing**: Windows 10 1903+ requires a signed APO. Unsigned CI
  builds work for development only. See INSTALLATION.md.
- The plugin path adds one callback block (~10 ms at 480 frames) of bridge
  latency on top of plugin latency; this is the isolation cost and is
  reported in the GUI latency display.
- VST3 plugins that use a separate edit-controller object receive parameter
  changes through the processing path (inputParameterChanges); live
  parameter automation through a disconnected controller is not supported.
