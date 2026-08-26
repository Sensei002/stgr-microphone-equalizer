# Packages the installer with Inno Setup 6 (iscc).
# Requires a successful Release build in build\bin\Release.
param(
    [string]$Version = "1.0.0"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

# Inno Setup 6 is preinstalled on GitHub windows-latest runners.
$iscc = "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
if (-not (Test-Path $iscc)) {
    $candidates = Get-ChildItem "C:\Program Files*\Inno Setup*\ISCC.exe" -ErrorAction SilentlyContinue
    if ($candidates) { $iscc = $candidates[0].FullName }
}
if (-not (Test-Path $iscc)) {
    throw "Inno Setup 6 (ISCC.exe) not found"
}

Write-Host "ISCC: $iscc"

# Inno Setup needs the installer script in its own directory context; the
# script references files with relative paths from the repo root.
Push-Location $root
try {
    & $iscc "/DMyAppVersion=$Version" "installer\STGR.iss"
    if ($LASTEXITCODE -ne 0) { throw "ISCC failed with exit code $LASTEXITCODE" }
} finally {
    Pop-Location
}

$dist = Join-Path $root "installer\dist"
Write-Host "Installer output:"
Get-ChildItem $dist | ForEach-Object { Write-Host "  $($_.Name) ($($_.Length) bytes)" }
