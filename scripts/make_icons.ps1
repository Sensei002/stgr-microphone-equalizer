# Icon generation script for STGR Microphone Equalizer.
# Generates application icons, tray icons and installer images from the
# master branding logo (assets/branding/stgr-logo.png).
#
# The script makes the near-white background of the logo transparent
# (flood fill from the four corners) and renders the required sizes.
#
# Run from the repository root:
#   powershell -ExecutionPolicy Bypass -File scripts/make_icons.ps1
#
# Generated output (all committed to the repository):
#   assets/icons/app.ico          - multi-size .ico (16..256) for the GUI
#   assets/icons/tray.ico         - tray icon (16/24/32)
#   assets/icons/installer.ico    - installer icon
#   assets/icons/icon-{16,24,32,48,64,128,256}.png
#   assets/branding/installer-wizard.bmp   - Inno Setup wizard side image (164x314)
#   assets/branding/installer-small.bmp    - Inno Setup small wizard image (55x44)
#   assets/branding/setup-header.png       - 500x120 header strip

param(
    [string]$LogoPath = "assets\branding\stgr-logo.png"
)

Add-Type -AssemblyName System.Drawing

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$iconsDir = Join-Path $root "assets\icons"
$brandDir = Join-Path $root "assets\branding"
New-Item -ItemType Directory -Force -Path $iconsDir | Out-Null

$src = [System.Drawing.Bitmap]::FromFile((Join-Path $root $LogoPath))
Write-Host "Logo: $($src.Width)x$($src.Height)"

function Get-IsLightPixel([System.Drawing.Bitmap]$bmp, [int]$x, [int]$y) {
    $c = $bmp.GetPixel($x, $y)
    return ($c.R -ge 235 -and $c.G -ge 235 -and $c.B -ge 235)
}

function ConvertTo-Transparent([System.Drawing.Bitmap]$bmp) {
    # Flood fill from the four corners over near-white pixels.
    $w = $bmp.Width; $h = $bmp.Height
    $visited = New-Object 'bool[,]' $w, $h
    $stack = New-Object System.Collections.Generic.Stack[System.Drawing.Point]
    foreach ($p in @(@(0,0), @(($w - 1),0), @(0,($h - 1)), @(($w - 1),($h - 1)))) {
        $stack.Push((New-Object System.Drawing.Point $p[0], $p[1]))
    }
    while ($stack.Count -gt 0) {
        $pt = $stack.Pop()
        if ($pt.X -lt 0 -or $pt.Y -lt 0 -or $pt.X -ge $w -or $pt.Y -ge $h) { continue }
        if ($visited[$pt.X, $pt.Y]) { continue }
        $visited[$pt.X, $pt.Y] = $true
        if (Get-IsLightPixel $bmp $pt.X $pt.Y) {
            $bmp.SetPixel($pt.X, $pt.Y, [System.Drawing.Color]::Transparent)
            $stack.Push((New-Object System.Drawing.Point ($pt.X+1), $pt.Y))
            $stack.Push((New-Object System.Drawing.Point ($pt.X-1), $pt.Y))
            $stack.Push((New-Object System.Drawing.Point $pt.X), ($pt.Y+1))
            $stack.Push((New-Object System.Drawing.Point $pt.X), ($pt.Y-1))
        }
    }
    return $bmp
}

function New-Scaled([System.Drawing.Bitmap]$src, [int]$size) {
    $bmp = New-Object System.Drawing.Bitmap $size, $size
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
    $g.Clear([System.Drawing.Color]::Transparent)
    $g.DrawImage($src, 0, 0, $size, $size)
    $g.Dispose()
    return $bmp
}

function New-FromMaster([int]$size) {
    $t = ConvertTo-Transparent (New-Object System.Drawing.Bitmap $src)
    return New-Scaled $t $size
}

function Write-Ico([string]$path, [int[]]$sizes) {
    # Write a PNG-compressed multi-size .ico (Vista+ format).
    $fs = [System.IO.File]::Create($path)
    $bw = New-Object System.IO.BinaryWriter $fs
    $bw.Write([uint16]0)      # reserved
    $bw.Write([uint16]1)      # type: icon
    $bw.Write([uint16]$sizes.Count)
    $entries = @()
    foreach ($s in $sizes) {
        $entry = New-Object System.IO.MemoryStream
        $png = New-FromMaster $s
        $png.Save($entry, [System.Drawing.Imaging.ImageFormat]::Png)
        $png.Dispose()
        $entries += ,@($s, $entry.ToArray())
        $entry.Dispose()
    }
    foreach ($e in $entries) {
        $s = $e[0]
        $data = $e[1]
        if ($s -ge 256) { $bw.Write([byte]0) } else { $bw.Write([byte]$s) }
        if ($s -ge 256) { $bw.Write([byte]0) } else { $bw.Write([byte]$s) }
        $bw.Write([byte]0)  # palette
        $bw.Write([byte]0)  # reserved
        $bw.Write([uint16]1)  # planes
        $bw.Write([uint16]32) # bpp
        $bw.Write([uint32]$data.Length)
        $bw.Write([uint32](6 + 16 * $sizes.Count))
    }
    $offset = 6 + 16 * $sizes.Count
    foreach ($e in $entries) {
        $data = $e[1]
        $bw.Write($data)
    }
    $bw.Flush()
    $fs.Close()
}

$sizes = @(16, 24, 32, 48, 64, 128, 256)
Write-Ico (Join-Path $iconsDir "app.ico") $sizes
Write-Ico (Join-Path $iconsDir "installer.ico") $sizes
Write-Ico (Join-Path $iconsDir "tray.ico") @(16, 24, 32)

foreach ($s in $sizes) {
    $png = New-FromMaster $s
    $png.Save((Join-Path $iconsDir "icon-$s.png"), [System.Drawing.Imaging.ImageFormat]::Png)
    $png.Dispose()
    Write-Host "  icon-$s.png"
}

# Inno Setup wizard side image (164x314). Build from a 1:1.914 crop of the logo
# scaled to 164x314 with the red STGR strip preserved.
$wiz = New-Object System.Drawing.Bitmap 164, 314
$g = [System.Drawing.Graphics]::FromImage($wiz)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$g.Clear([System.Drawing.Color]::FromArgb(15, 15, 17))
$t = ConvertTo-Transparent (New-Object System.Drawing.Bitmap $src)
$g.DrawImage($t, 32, 60, 100, 100)
$font = New-Object System.Drawing.Font "Segoe UI", 14, ([System.Drawing.FontStyle]::Bold)
$brush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(220, 40, 46))
$g.DrawString("STGR", $font, $brush, 41, 180)
$font2 = New-Object System.Drawing.Font "Segoe UI", 9
$brush2 = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(230, 230, 230))
$g.DrawString("Microphone", $font2, $brush2, 40, 212)
$g.DrawString("Equalizer", $font2, $brush2, 40, 226)
$g.Dispose()
$wiz.Save((Join-Path $brandDir "installer-wizard.bmp"), [System.Drawing.Imaging.ImageFormat]::Bmp)
$wiz.Dispose()

# Small wizard image 55x44
$small = New-Object System.Drawing.Bitmap 55, 44
$g = [System.Drawing.Graphics]::FromImage($small)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$g.Clear([System.Drawing.Color]::FromArgb(15, 15, 17))
$g.DrawImage($src, 4, 4, 36, 36)
$g.Dispose()
$small.Save((Join-Path $brandDir "installer-small.bmp"), [System.Drawing.Imaging.ImageFormat]::Bmp)
$small.Dispose()

# Header strip 500x120 for the GUI about page
$header = New-Object System.Drawing.Bitmap 500, 120
$g = [System.Drawing.Graphics]::FromImage($header)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$g.Clear([System.Drawing.Color]::FromArgb(18, 18, 20))
$g.DrawImage($src, 20, 10, 100, 100)
$font3 = New-Object System.Drawing.Font "Segoe UI", 26, ([System.Drawing.FontStyle]::Bold)
$brush3 = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(235, 235, 235))
$g.DrawString("STGR Microphone Equalizer", $font3, $brush3, 135, 30)
$g.Dispose()
$header.Save((Join-Path $brandDir "setup-header.png"), [System.Drawing.Imaging.ImageFormat]::Png)
$header.Dispose()

$src.Dispose()
Write-Host "Done."
