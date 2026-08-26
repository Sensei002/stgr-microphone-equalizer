# STGR Microphone Equalizer

Native Windows 10/11 x64 system-level microphone processing: EQ, filters, dynamics
and VST/VST3 plugin hosting — attached **directly to your existing physical
microphone** via a Windows Audio Processing Object (APO).

```
PHYSICAL MICROPHONE
        ↓
STGR Microphone Equalizer  (SFX APO inside the Windows audio engine)
        ↓
Windows microphone endpoint  (unchanged - still your normal mic)
        ↓
Discord / OBS / Teams / Zoom / Games / Browser  → processed audio
```

No virtual microphone. No STGR device to select. Discord/OBS keep using
`Microphone (USB Audio Device)` and receive processed audio.

## How it works

| Component | What it is | Runs where |
|---|---|---|
| `stgr_apo.dll` | SFX (stream effects) APO, attached per capture endpoint | inside `audiodg.exe` (Windows audio engine) |
| `STGRMicrophoneEqualizer.exe` | configuration GUI | only when you open it |
| `STGRAudioServer.exe` | plugin bridge (VST/VST3 host) | headless, persistent |
| `STGRTray.exe` | system tray app | headless, persistent |
| `STGRAdmin.exe` | elevated helper (registration, audio restart) | on demand |
| `STGRScan.exe` | isolated plugin scanner | on demand |

- **The APO processes your microphone** even when the GUI is closed and after
  Windows restarts. The GUI is a configuration frontend only.
- Built-in DSP (gain, filters, 10-band parametric EQ, gate, expander,
  compressor, limiter) runs **in the APO with ~zero added latency**.
- VST/VST3 plugins run in `STGRAudioServer.exe` (a separate process) and the
  APO streams audio to/from it through shared memory with a one-block
  pipeline. A crashing plugin kills only the server; the APO automatically
  bypasses plugins until it restarts.
- Everything is local: no telemetry, no cloud, no accounts, no audio ever
  leaves your PC.

## Installation

1. Download `STGR-Microphone-Equalizer-vX.Y.Z-x64.exe` from the Releases page.
2. Run the installer (administrator rights are needed to register the APO).
3. Open **STGR Microphone Equalizer**, select your microphone.
4. Add effects (EQ, compressor, limiter, VST/VST3 plugins).
5. Click **APPLY** — done. Close the GUI whenever you like; processing
   continues.

> **Signing note:** on Windows 10 1903+ the audio engine only loads APOs that
> are signed with a trusted code-signing certificate. CI builds produce
> **unsigned** binaries for development and testing. For production use,
> sign `stgr_apo.dll` (see [docs/INSTALLATION.md](docs/INSTALLATION.md)).

## Configuration

Per-microphone JSON configuration is stored in
`%ProgramData%\STGR\config\devices\<endpoint-id>.json` (versioned schema).
Each microphone can have its own chain:

```json
{
  "version": 1,
  "endpointId": "{0.0.1.00000000}.{...}",
  "enabled": true,
  "chain": [
    { "type": "HighPass",   "enabled": true,  "freq": 80 },
    { "type": "ParametricEQ","enabled": true, "bands": [ ... ] },
    { "type": "Compressor", "enabled": true,  "thresholdDb": -20, "ratio": 4 },
    { "type": "Limiter",    "enabled": true,  "ceilingDb": -1 }
  ]
}
```

See [docs/CONFIGURATION.md](docs/CONFIGURATION.md).

## Building

The project is built entirely by GitHub Actions — no local toolchain needed:

```text
commit → push → GitHub builds → tests run → installer packaged → release
```

CI instructions: [docs/BUILD.md](docs/BUILD.md) and [docs/CI-CD.md](docs/CI-CD.md).

## Documentation

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — overall design
- [docs/APO.md](docs/APO.md) — APO architecture and registration
- [docs/DSP.md](docs/DSP.md) — DSP engine
- [docs/VST.md](docs/VST.md) — VST2 hosting
- [docs/VST3.md](docs/VST3.md) — VST3 hosting
- [docs/CONFIGURATION.md](docs/CONFIGURATION.md) — config files, presets, plugin cache
- [docs/INSTALLATION.md](docs/INSTALLATION.md) — installer, registration, signing
- [docs/BUILD.md](docs/BUILD.md) — building from source
- [docs/CI-CD.md](docs/CI-CD.md) — CI/CD pipeline and releases
- [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) — common issues
- [docs/PERFORMANCE.md](docs/PERFORMANCE.md) — resource usage and benchmarks

## License

GPL-3.0 — see [LICENSE](LICENSE) and [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).
