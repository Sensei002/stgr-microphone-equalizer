# CI/CD

## Workflows

### `build.yml` — every push / PR

```
Checkout → fetch VST3 SDK → cmake configure (x64, Release)
→ build → ctest (DSP + config) → STGRBench → verify APO exports (dumpbin)
→ package installer (Inno Setup) → SHA256SUMS.txt → upload artifacts
```

Artifacts: `stgr-microphone-equalizer-x64` (binaries + installer + checksums)
and `benchmark-report`.

### `release.yml` — tag push `v1.0.0`

Same pipeline, plus a GitHub Release:

- `STGR-Microphone-Equalizer-v1.0.0-x64.exe`
- `SHA256SUMS.txt`
- benchmark report
- release notes (including the signing warning)

## Requirements on the runner

- `windows-latest` (Windows Server 2022, VS 2022, CMake, Inno Setup 6
  preinstalled).

## Signing (production)

CI builds are **unsigned** (never commit certificates or keys to the
repository). To produce signed releases:

1. Obtain a code-signing certificate (for the APO, the Windows hardware
   developer program certificate is recommended).
2. Store it in repository secrets:
   - `SIGNING_CERT_BASE64` — PFX encoded as base64
   - `SIGNING_CERT_PASSWORD` — PFX password
3. Extend `release.yml` with a signing step (e.g. `signtool`):

```yaml
- name: Sign binaries
  shell: pwsh
  run: |
    $pfx = Join-Path $env:RUNNER_TEMP "cert.pfx"
    [IO.File]::WriteAllBytes($pfx, [Convert]::FromBase64String($env:SIGNING_CERT_BASE64))
    & "C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\signtool.exe" sign `
      /f $pfx /p $env:SIGNING_CERT_PASSWORD /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 `
      build\bin\Release\stgr_apo.dll
  env:
    SIGNING_CERT_BASE64: ${{ secrets.SIGNING_CERT_BASE64 }}
    SIGNING_CERT_PASSWORD: ${{ secrets.SIGNING_CERT_PASSWORD }}
```

Sign `stgr_apo.dll` (the APO must be signed to load on Windows 10 1903+);
signing the installer `.exe` is recommended for SmartScreen.

## Release flow

```text
git tag v1.0.0
git push origin v1.0.0
→ GitHub Actions builds, tests, packages, publishes the release
```
