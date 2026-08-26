# VST (VST2) hosting

## Scope

VST2 (VST 2.4 ABI) plugins are hosted **out-of-process** in
`STGRAudioServer.exe`, never inside the APO. The audio server loads the
plugin DLL, runs `processReplacing`, and returns the processed audio to the
APO through the shared-memory bridge.

## ABI implementation

The `src/plugins/vst2_host.cpp` file declares the minimal published VST 2.4
ABI (the `VstEffect` struct layout and the opcode enum). This is a
functional interface definition required to interoperate with third-party
plugins; **no Steinberg SDK source code is copied into this repository**.
VST 2.4 SDK licensing (Steinberg's GPL/commercial dual license) applies to
plugin developers; hosting against the published ABI is a standard practice
(for details see THIRD_PARTY_LICENSES.md).

## Features

- Loading: `LoadLibrary`, `VSTPluginMain`/`main` entry.
- Processing: `processReplacing` (with `process` fallback), planar
  conversion from the interleaved bridge format, mono mixdown output.
- Parameters: count, names, get/set (normalized 0..1).
- State: `effGetChunk`/`effSetChunk` when the plugin supports program
  chunks, otherwise raw parameter values — stored with the chain config.
- Latency: `effGetInitialDelay` reported in the GUI latency display.

## Safety

- Scanning happens in `STGRScan.exe` (separate process); a crashing plugin
  never takes down the GUI.
- Plugins that fail to init are skipped and the rest of the chain keeps
  processing.
- The server restarting is transparent: the APO bypasses plugin stages
  until the server is back.

## Scan locations (VST2)

```
C:\Program Files\Steinberg\VstPlugins
C:\Program Files\Common Files\VST2
%APPDATA%\VST
C:\Program Files\VSTPlugins
+ custom paths from global.json
```

Results are cached in `%ProgramData%\STGR\config\plugin-cache.json`.
