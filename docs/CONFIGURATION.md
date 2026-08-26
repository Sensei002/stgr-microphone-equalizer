# Configuration

All configuration is JSON, versioned, machine-wide under
`%ProgramData%\STGR\config\`.

## Per-device config

`devices\<endpoint-id>.json` — the endpoint id is the stable Windows MMDevice
id, so a renamed microphone keeps its config. Schema version 1:

```json
{
  "version": 1,
  "endpointId": "{0.0.1.00000000}.{b8d6...}",
  "endpointName": "Microphone (USB Audio Device)",
  "enabled": true,
  "chain": [
    {
      "type": "HighPass",
      "enabled": true,
      "freq": 80.0,
      "q": 0.707
    },
    {
      "type": "ParametricEQ",
      "enabled": true,
      "bands": [
        { "enabled": true,  "type": "Peaking",  "freq": 200,   "gainDb": -2.0, "q": 0.707 },
        { "enabled": false, "type": "Peaking",  "freq": 1000,  "gainDb": 2.0,  "q": 0.707 }
      ]
    },
    {
      "type": "Plugin",
      "enabled": true,
      "pluginInstanceId": "C:\\Program Files\\Common Files\\VST3\\ReaComp.vst3",
      "pluginName": "ReaComp",
      "pluginPath": "C:\\Program Files\\Common Files\\VST3\\ReaComp.vst3",
      "pluginFormat": 3,
      "pluginBypassed": false,
      "pluginParams": [ { "name": "0", "value": 0.5 } ]
    },
    { "type": "Compressor", "enabled": true, "thresholdDb": -20.0, "ratio": 4.0 },
    { "type": "Limiter",    "enabled": true, "ceilingDb": -1.0 }
  ]
}
```

### Stage types

`Gain`, `HighPass`, `LowPass`, `LowShelf`, `HighShelf`, `Peaking`, `Notch`,
`ParametricEQ`, `Gate`, `Expander`, `Compressor`, `Limiter`, `Plugin`.

### Band filter types

`Peaking`, `LowShelf`, `HighShelf`, `HighPass`, `LowPass`, `Notch`.

## Global config

`global.json`:

```json
{
  "version": 1,
  "processingEnabled": true,
  "customVstPaths": [],
  "startWithWindows": false,
  "autoApply": true,
  "bridgeLatencyFrames": 480
}
```

`processingEnabled` is the master switch (tray menu). The APO combines it
with the per-device `enabled` flag.

## Presets

`config\presets\<name>.json`, portable (no endpoint id inside):

```json
{
  "name": "Voice",
  "chain": [ ... ]
}
```

Built-in presets: Flat, Voice, Streaming, Discord, Podcast, Clean Voice.

## Plugin cache

`config\plugin-cache.json` — written by `STGRScan.exe`, read by the GUI:

```json
{
  "version": 1,
  "plugins": [
    {
      "path": "C:\\Program Files\\Common Files\\VST3\\ReaComp.vst3",
      "name": "ReaComp",
      "vendor": "Cockos",
      "format": 3,
      "version": 6,
      "uid": "",
      "status": 0,
      "lastScan": 1730000000
    }
  ]
}
```

`status`: 0 = ok, 1 = blacklisted, 2 = scan failed (plugin crashes/refuses
to load).

## Reload semantics

The APO polls the device config + global config (mtime) every second and
rebuilds the DSP chain when either changes; the GUI/server/tray also signal
the named event `Local\STGR_CfgChanged` after writing so the APO applies
changes without waiting for the poll interval. Chain changes do not require
an audio service restart.
