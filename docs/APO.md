# APO design and registration

## 1. What the APO is

`stgr_apo.dll` is a user-mode COM Audio Processing Object (APO) implementing:

- `IAudioProcessingObject` (Initialize, format negotiation, latency,
  registration properties)
- `IAudioProcessingObjectConfiguration` (LockForProcess / UnlockForProcess)
- `IAudioProcessingObjectRT` (APOProcess, CalcInputFrames/CalcOutputFrames)
- `IAudioSystemEffects` (marker) + `IAudioSystemEffects2` (GetEffectsList)

It is placed in the **SFX (stream effects)** position of the capture path:
per-stream, after the tee, before applications receive the data.

## 2. Registration (registry, no driver)

Three pieces are written by the installer / `STGRAdmin --register-apo`
(elevated):

**a) COM class**
```
HKLM\SOFTWARE\Classes\CLSID\{6F3D2C1E-9A84-4B5A-8D6B-0C1E2F3A4B5C}
  (default) = "STGR Microphone Equalizer APO"
  InProcServer32
    (default) = <install>\stgr_apo.dll
    ThreadingModel = Both
```

**b) Audio engine APO registration**
```
HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\AudioEngine\AudioProcessingObjects\{6F3D2C1E-...}
  FriendlyName / Copyright / MajorVersion / MinorVersion / Flags
  MinInputConnections=1 MaxInputConnections=1
  MinOutputConnections=1 MaxOutputConnections=1
  MaxInstances=0xFFFFFFFF NumAPOInterfaces=1
  APOInterface0 = {FD7F2B29-24D0-4B5C-B177-592C39F9CA10}  (IAudioProcessingObject)
```

**c) Endpoint association (per selected microphone)**
```
HKLM\SYSTEM\CurrentControlSet\Control\MMDevices\Audio\Capture\{endpoint}\FxProperties
  (mirrored under HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\...)
  "{D04E05A6-594B-4FB6-A80D-01AF5EED7D1D},5"  (PKEY_FX_StreamEffectClsid)
        = "{6F3D2C1E-9A84-4B5A-8D6B-0C1E2F3A4B5C}"      (REG_SZ)
  "{D04E05A6-594B-4FB6-A80D-01AF5EED7D1D},0"  (PKEY_FX_Association)
        = "{00000000-0000-0000-0000-000000000000}"      (KSNODETYPE_ANY)
  "{D3993A3F-99C2-4402-B5EC-A92A0367664B},5"  (PKEY_SFX_ProcessingModes_Supported_For_Streaming)
        = "{C18E2F7E-933D-4965-B7D1-1EEF228D2AF3}"      (DEFAULT mode, REG_MULTI_SZ)
```

The GUI's **Attach/Detach** and the uninstaller modify (c) only; uninstall
also removes (a) and (b) and cleans every endpoint's FX store.

## 3. Applying changes

- Changing the **chain** only rewrites the JSON config + signals
  `Local\STGR_CfgChanged`; the APO reloads it within ~1 s. No audio restart.
- Changing the **endpoint association** requires the audio engine to rebuild
  the FX chain: `STGRAdmin --restart-audio` stops/starts `Audiosrv` (and
  `AudioEndpointBuilder`). The GUI asks before doing this.

## 4. Signing requirement (Windows 10 1903+)

Since Windows 10 1903, the audio engine only loads APOs whose signature
chains to a trusted root. Consequences:

- CI artifacts are **unsigned**: they load only when the signing check is
  satisfied (development/test signing, or a signed DLL you produce).
- For production, sign `stgr_apo.dll` with a code-signing certificate
  (e.g. from the Windows hardware dev center program) and ship the signed
  DLL; the installer copies and registers it unchanged.

Never commit private keys/certificates to the repository. The CI workflow
supports an optional signing step driven by secrets (`SIGNING_CERT_BASE64`,
`SIGNING_CERT_PASSWORD`).

## 5. Behavior notes

- The APO accepts **float32** formats only (the audio engine converts
  capture streams to float32 before SFX on supported configurations).
- RAW processing mode bypasses the APO entirely (platform behavior for SFX).
- The APO never modifies the device or its topology; it only processes the
  buffers handed to `APOProcess`.
