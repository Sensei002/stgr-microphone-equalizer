# Installation and uninstallation

## Installer

The installer (`STGR-Microphone-Equalizer-vX.Y.Z-x64.exe`, Inno Setup) does:

1. Copies binaries to `C:\Program Files\STGR\`.
2. Registers the APO:
   - COM class (`DllRegisterServer` on `stgr_apo.dll`),
   - `AudioEngine\AudioProcessingObjects` registration,
   - (endpoint association happens later from the GUI "Attach").
3. Adds Start-menu (and optional desktop) shortcuts.
4. Optional "Start with Windows" (Run key entries for `STGRTray.exe` and
   `STGRAudioServer.exe`).

Administrator privileges are required **only** for installation/registration;
the GUI and tray run as the normal user.

## First run

1. Open **STGR Microphone Equalizer**.
2. Select your microphone in the list.
3. Click **Attach STGR** (elevation prompt; restarts the audio service so
   the FX chain is rebuilt).
4. Configure EQ / dynamics / plugins, click **APPLY**.
5. Close the GUI whenever you want — processing continues.

## Signing requirements (important)

On Windows 10 **1903 and newer**, the audio engine only loads APOs that are
signed with a certificate chaining to a trusted root. CI artifacts are
unsigned and will only load under the platform's development/test
conditions. For a production deployment:

- sign `stgr_apo.dll` (see docs/CI-CD.md), or
- install a certificate from the Windows hardware dev center program and
  re-run the installer with the signed DLL.

If the APO cannot load, Discord etc. simply receive unprocessed audio —
Windows reports the failure in the APO load counters
(`PKEY_Endpoint_Disable_SysFx` gets set after repeated failures; clear it in
`HKLM\...\MMDevices\Audio\Capture\<id>\Properties` if your device stops
processing entirely).

## Uninstallation

1. Windows Settings → Apps → **STGR Microphone Equalizer** → Uninstall.
2. The uninstaller asks whether to remove microphone processing; on "yes"
   it deregisters the APO and clears the endpoint FX association for every
   capture device, restoring normal microphone behavior.
3. Remaining user data (config, presets, logs) stays in
   `%ProgramData%\STGR\` and `%AppData%\STGR\` so a reinstall keeps your
   settings; delete those folders manually to fully reset.

## Manual registration (for troubleshooting)

```powershell
# Elevated PowerShell
& "C:\Program Files\STGR\STGRAdmin.exe" --register-apo
& "C:\Program Files\STGR\STGRAdmin.exe" --attach "<endpoint-id>"
& "C:\Program Files\STGR\STGRAdmin.exe" --restart-audio
```
