# Troubleshooting

## Microphone has no STGR processing (applications hear raw audio)

1. **APO not attached** — open STGR, select the microphone, click
   **Attach STGR**, then **Restart Audio Service** when asked.
2. **Unsigned APO on Win10 1903+** — see docs/INSTALLATION.md; the audio
   engine will not load an unsigned APO. CI binaries are development
   artifacts.
3. **Application uses RAW processing mode** — SFX APOs are skipped for RAW
   streams (Windows platform behavior). Games using WASAPI raw mode bypass
   all SFX effects by design.
4. **Check the FX registry store**:
   `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Capture\<id>\FxProperties`
   must contain `{D04E05A6-594B-4FB6-A80D-01AF5EED7D1D},5` = our CLSID.
5. **Repeated APO load failures** disable effects: Windows sets
   `PKEY_Endpoint_Disable_SysFx` (value 1) in the endpoint's `Properties`
   store after ~10 failures. Delete it and restart the audio service.

## Plugin not producing sound

- Check the **Chain** tab: the plugin must be enabled and not bypassed.
- Check the bridge state in **Diagnostics**: `Plugin bridge: running` and
  `Active plugins` count.
- If the bridge shows `absent`, start `STGRAudioServer.exe` (or restart the
  tray; it relaunches on demand from the GUI Apply).
- A plugin that crashed is skipped automatically — remove and re-add it, or
  check the plugin cache status in the Add Plugin dialog (`failed scan`).
- Plugins that report high latency show it in the GUI latency display; very
  high latency plugins may make the bridge ring overflow (plugins bypassed
  until it recovers).

## Audio stutters / pops

- Built-in DSP is zero-latency; stutter is almost always from the plugin
  path or the device driver. Reduce the number of plugins, or use
  zero-latency plugins.
- The bridge adds one block (~10 ms); this is displayed in the GUI.

## "Restart audio service" needed after attach/detach

Endpoint FX association changes are only applied when the audio engine
rebuilds the endpoint chain. The GUI offers the restart; alternatively log
out/in.

## Where are the logs?

`%ProgramData%\STGR\logs\stgr-{server,gui,tray,scan,admin}.log`.

## Diagnostics export

**Diagnostics** tab → **Export Diagnostics** writes a text file with
version, Windows build, CPU/RAM, endpoint id, config path, bridge state —
no audio data, nothing uploaded.

## Config reset

- Per-device: STGR GUI → Reset (clears the chain).
- Full reset: delete `%ProgramData%\STGR\config\` (elevated) and
  `%AppData%\STGR\`.
