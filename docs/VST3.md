# VST3 hosting

## Architecture

VST3 plugins are hosted **out-of-process** in `STGRAudioServer.exe` (same
bridge design as VST2). The host is implemented directly against the
official **VST3 SDK public interfaces** (`pluginterfaces`), which are
fetched at build time from https://github.com/steinbergmedia/vst3sdk
(GPL-3.0; see THIRD_PARTY_LICENSES.md). No SDK source is vendored in this
repository — CI fetches it, and builds without the SDK still work (VST3
support compiles to a stub).

## Host flow

1. `LoadLibrary` the `.vst3` module; resolve `GetPluginFactory`.
2. Enumerate classes; find the first `kVstAudioEffectClass` ("Audio Module
   Class").
3. `createInstance(classId, IID_IAudioProcessor)`; query `IComponent` and
   `IEditController` on the same object when available.
4. `setupProcessing` (realtime, float32, block ≤ 1024) and
   `setBusArrangements` (mono in / mono out).
5. Per block: fill `AudioBusBuffers` (planar scratch), call `process`, read
   the result, mix down to the bridge format.
6. Parameters via `IComponent::getParameterInfo` / `IEditController`
   `setParamNormalized`; persisted parameter values are also delivered
   through the processing path.
7. State via `IComponent::getState/setState` (stored with the chain).

## FUID plumbing

The SDK headers declare interface IIDs; `INIT_CLASS_IID` makes
`DECLARE_CLASS_IID` emit their definitions in the host translation unit,
and `FUnknownPrivate::atomicAdd` is provided (the SDK's base source
implements it identically; our host is header-only).

## Honest limitations (documented)

- No plugin editor GUI (headless hosting). Parameters are set from stored
  config values; plugin-own editors remain a roadmap item.
- Plugins exposing an edit controller as a *separate* object receive
  parameter values through `IParameterChanges` in `process`; controller-side
  automation not synchronized with the component is not supported.
- Plugin latency (getLatencySamples) is summed and displayed in the GUI.

## Scan locations (VST3)

```
C:\Program Files\Common Files\VST3
%APPDATA%\VST3
+ custom paths from global.json
```
