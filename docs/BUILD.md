# Building from source

The project is designed to be built **only via GitHub Actions**; no local
toolchain is required. The commands below document what CI does, for
developers who want to reproduce it.

## Requirements

- Windows 10/11 x64
- Visual Studio 2019/2022 (MSVC, x64) + Windows SDK 10.0.19041+
- CMake ≥ 3.21
- Inno Setup 6 (packaging only)

## Reproduce the CI build

```powershell
# 1. Fetch the VST3 SDK (headers; used for VST3 hosting)
git clone --depth 1 https://github.com/steinbergmedia/vst3sdk.git third_party/vst3

# 2. Configure
cmake -S . -B build -A x64 -DSTGR_VST3_SDK_DIR="%CD%\third_party\vst3" -DSTGR_VST3_FETCH=OFF

# 3. Build (Release)
cmake --build build --config Release --parallel

# 4. Test
ctest --test-dir build -C Release --output-on-failure

# 5. Benchmark
.\build\bin\Release\STGRBench.exe

# 6. Package
.\scripts\package.ps1 -Version 1.0.0
```

## Options

| Option | Default | Meaning |
|---|---|---|
| `STGR_BUILD_APO` | ON | APO DLL + registration helpers |
| `STGR_BUILD_GUI` | ON | configuration GUI |
| `STGR_BUILD_TRAY` | ON | tray app |
| `STGR_BUILD_SERVER` | ON | audio server (plugin bridge) |
| `STGR_BUILD_TESTS` | ON | unit tests (`ctest`) |
| `STGR_BUILD_BENCH` | ON | benchmark |
| `STGR_ENABLE_VST2` | ON | VST2 hosting |
| `STGR_ENABLE_VST3` | ON | VST3 hosting (needs SDK) |
| `STGR_VST3_SDK_DIR` | empty | path to an existing VST3 SDK checkout |
| `STGR_VST3_FETCH` | ON | fetch the SDK automatically when not found |

## Outputs

`build\bin\Release\`:

```
STGRMicrophoneEqualizer.exe   GUI
STGRTray.exe                  tray
STGRAudioServer.exe           plugin bridge host
STGRAdmin.exe                 elevated helper (requireAdministrator manifest)
STGRScan.exe                  isolated plugin scanner
stgr_apo.dll                  the APO (static CRT, no manifest, COM exports)
STGRBench.exe                 benchmark
stgr_test_dsp.exe / stgr_test_config.exe
```

## Notes

- The APO DLL is compiled with `/MT` (static CRT) and no embedded manifest,
  per the Windows audio processing object requirements.
- `STGRAdmin.exe` carries a `requireAdministrator` manifest.
- x64 only; 32-bit builds are rejected by CMake.
