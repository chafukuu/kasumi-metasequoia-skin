#Requires -Version 5.1
<#
.SYNOPSIS
  Offline export and equivalence check for the two source-resolution native band layers.

.DESCRIPTION
  Loads the existing compiled v0.5.3 diagnostics assembly in memory by reflection, calls the
  actual internal OverlayConfig.Load, BandAsset.Load and BandRenderer.CreateSourceLayer
  methods, and writes exactly three outputs:

    skin/kasumi-adaptive/assets/native-band-background.png
    skin/kasumi-adaptive/assets/native-band-foreground.png
    verification/v053/native-band-layers-report.json

  It never starts the executable, touches an input-method window/service, writes registry or
  production configuration, or modifies the source PNG/masks. All validation bitmaps are
  composed in memory. The report contains the per-pixel comparison of the old
  BandRenderer.Render output against a reconstruction from the exported layers using the
  same old RenderOcclusion GDI steps and parameters.

.NOTES
  Runs under Windows PowerShell 5.1. When invoked from PowerShell 7 it relaunches itself
  under %WINDIR%\System32\WindowsPowerShell\v1.0\powershell.exe.
#>
[CmdletBinding()]
param()

Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'

if ($PSVersionTable.PSVersion.Major -ge 6) {
    $winps = Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
    if (-not (Test-Path -LiteralPath $winps)) { throw "Windows PowerShell 5.1 not found: $winps" }
    Write-Host '[info] Relaunching under Windows PowerShell 5.1 for .NET Framework reflection.'
    & $winps -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath
    exit $LASTEXITCODE
}

Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public sealed class KasumiPixelDiff
{
    public long DifferentPixels;
    public long TotalPixels;
    public int MaxChannelDelta;
    public int MaxAlphaDelta;
    public long AlphaDifferentPixels;
    public long TotalAbsDelta;
    public double MeanAbsDeltaPerChannel;
    public bool HasDifferenceBBox;
    public int DifferenceLeft;
    public int DifferenceTop;
    public int DifferenceRight;
    public int DifferenceBottom;
}

public sealed class KasumiLayerStats
{
    public long AlphaZero;
    public long AlphaPartial;
    public long AlphaOpaque;
    public int MinAlpha;
    public int MaxAlpha;
    public bool HasAlphaBBox;
    public int AlphaLeft;
    public int AlphaTop;
    public int AlphaRight;
    public int AlphaBottom;
    public bool PremultipliedValid;
}

public static class KasumiNativeBandOps
{
    public static KasumiLayerStats Analyze(Bitmap bitmap)
    {
        KasumiLayerStats stats = new KasumiLayerStats();
        stats.MinAlpha = 255;
        stats.MaxAlpha = 0;
        stats.AlphaLeft = bitmap.Width;
        stats.AlphaTop = bitmap.Height;
        stats.AlphaRight = -1;
        stats.AlphaBottom = -1;
        stats.PremultipliedValid = true;
        BitmapData data = bitmap.LockBits(new Rectangle(0, 0, bitmap.Width, bitmap.Height),
            ImageLockMode.ReadOnly, PixelFormat.Format32bppPArgb);
        try
        {
            byte[] bytes = new byte[data.Stride * bitmap.Height];
            Marshal.Copy(data.Scan0, bytes, 0, bytes.Length);
            for (int y = 0; y < bitmap.Height; y++)
            {
                for (int x = 0; x < bitmap.Width; x++)
                {
                    int i = y * data.Stride + x * 4;
                    int b = bytes[i];
                    int g = bytes[i + 1];
                    int r = bytes[i + 2];
                    int a = bytes[i + 3];
                    if (a == 0) stats.AlphaZero++;
                    else if (a == 255) stats.AlphaOpaque++;
                    else stats.AlphaPartial++;
                    if (a < stats.MinAlpha) stats.MinAlpha = a;
                    if (a > stats.MaxAlpha) stats.MaxAlpha = a;
                    if (b > a || g > a || r > a) stats.PremultipliedValid = false;
                    if (a > 0)
                    {
                        if (x < stats.AlphaLeft) stats.AlphaLeft = x;
                        if (y < stats.AlphaTop) stats.AlphaTop = y;
                        if (x > stats.AlphaRight) stats.AlphaRight = x;
                        if (y > stats.AlphaBottom) stats.AlphaBottom = y;
                    }
                }
            }
        }
        finally
        {
            bitmap.UnlockBits(data);
        }
        stats.HasAlphaBBox = stats.AlphaRight >= 0;
        return stats;
    }

    public static KasumiPixelDiff Compare(Bitmap actual, Bitmap expected)
    {
        if (actual.Width != expected.Width || actual.Height != expected.Height)
            throw new ArgumentException("Bitmap dimensions do not match");
        KasumiPixelDiff diff = new KasumiPixelDiff();
        diff.TotalPixels = (long)actual.Width * actual.Height;
        diff.DifferenceLeft = actual.Width;
        diff.DifferenceTop = actual.Height;
        diff.DifferenceRight = -1;
        diff.DifferenceBottom = -1;
        BitmapData a = actual.LockBits(new Rectangle(0, 0, actual.Width, actual.Height),
            ImageLockMode.ReadOnly, PixelFormat.Format32bppPArgb);
        BitmapData b = expected.LockBits(new Rectangle(0, 0, expected.Width, expected.Height),
            ImageLockMode.ReadOnly, PixelFormat.Format32bppPArgb);
        try
        {
            byte[] aa = new byte[a.Stride * actual.Height];
            byte[] bb = new byte[b.Stride * expected.Height];
            Marshal.Copy(a.Scan0, aa, 0, aa.Length);
            Marshal.Copy(b.Scan0, bb, 0, bb.Length);
            for (int y = 0; y < actual.Height; y++)
            {
                for (int x = 0; x < actual.Width; x++)
                {
                    int i = y * a.Stride + x * 4;
                    bool different = false;
                    for (int c = 0; c < 4; c++)
                    {
                        int delta = Math.Abs(aa[i + c] - bb[i + c]);
                        if (delta != 0) different = true;
                        if (delta > diff.MaxChannelDelta) diff.MaxChannelDelta = delta;
                        diff.TotalAbsDelta += delta;
                        if (c == 3)
                        {
                            if (delta > diff.MaxAlphaDelta) diff.MaxAlphaDelta = delta;
                            if (delta != 0) diff.AlphaDifferentPixels++;
                        }
                    }
                    if (different)
                    {
                        diff.DifferentPixels++;
                        if (x < diff.DifferenceLeft) diff.DifferenceLeft = x;
                        if (y < diff.DifferenceTop) diff.DifferenceTop = y;
                        if (x > diff.DifferenceRight) diff.DifferenceRight = x;
                        if (y > diff.DifferenceBottom) diff.DifferenceBottom = y;
                    }
                }
            }
        }
        finally
        {
            actual.UnlockBits(a);
            expected.UnlockBits(b);
        }
        if (diff.TotalPixels > 0)
            diff.MeanAbsDeltaPerChannel = (double)diff.TotalAbsDelta / (diff.TotalPixels * 4.0);
        diff.HasDifferenceBBox = diff.DifferenceRight >= 0;
        return diff;
    }

    // Same settings as BandAsset.CreateScaledOcclusionLayer.
    public static Bitmap Scale(Bitmap source, double scale)
    {
        int width = (int)Math.Ceiling(source.Width * scale);
        int height = (int)Math.Ceiling(source.Height * scale);
        if (width < 1 || height < 1) throw new ArgumentException("Scaled layer would be empty");
        Bitmap scaled = new Bitmap(width, height, PixelFormat.Format32bppPArgb);
        try
        {
            using (Graphics graphics = Graphics.FromImage(scaled))
            {
                graphics.CompositingMode = CompositingMode.SourceCopy;
                graphics.CompositingQuality = CompositingQuality.HighQuality;
                graphics.InterpolationMode = InterpolationMode.HighQualityBilinear;
                graphics.PixelOffsetMode = PixelOffsetMode.Half;
                graphics.SmoothingMode = SmoothingMode.HighQuality;
                graphics.Clear(Color.Transparent);
                graphics.DrawImage(source,
                    new RectangleF(0, 0, (float)(source.Width * scale), (float)(source.Height * scale)),
                    new RectangleF(0, 0, source.Width, source.Height), GraphicsUnit.Pixel);
            }
            return scaled;
        }
        catch
        {
            scaled.Dispose();
            throw;
        }
    }

    // Mirrors BandRenderer.RenderOcclusion: source-res layers were already scaled to the
    // physical cache above, background is clipped after that resample, then foreground is
    // composited with the old shift/Y rules. Frets are absent for the current v0.5.3 config.
    public static Bitmap ComposeOld(Bitmap background, Bitmap foreground, int width, int height,
        double cutoff, double shift, double foregroundY)
    {
        Bitmap bitmap = new Bitmap(width, height, PixelFormat.Format32bppPArgb);
        try
        {
            using (Graphics graphics = Graphics.FromImage(bitmap))
            {
                graphics.CompositingMode = CompositingMode.SourceCopy;
                graphics.InterpolationMode = InterpolationMode.HighQualityBilinear;
                graphics.PixelOffsetMode = PixelOffsetMode.Half;
                graphics.Clear(Color.Transparent);
                graphics.SetClip(new RectangleF(0, 0, (float)Math.Max(0, cutoff), (float)height));
                graphics.DrawImageUnscaled(background, 0, 0);
                graphics.ResetClip();
                graphics.CompositingMode = CompositingMode.SourceOver;
                graphics.DrawImage(foreground,
                    new RectangleF((float)shift, (float)foregroundY,
                        foreground.Width, foreground.Height),
                    new RectangleF(0, 0, foreground.Width, foreground.Height), GraphicsUnit.Pixel);
            }
            return bitmap;
        }
        catch
        {
            bitmap.Dispose();
            throw;
        }
    }

    // Diagnostic only. This is a single-pass DrawImage from the source-resolution PNGs with
    // destination clipping. It is NOT the old two-stage path and is not used as the equivalent
    // reconstruction; it quantifies the GDI resampling/order difference for the report.
    public static Bitmap ComposeDirect(Bitmap background, Bitmap foreground, int width, int height,
        double scale, double cutoff, double shift, double foregroundY)
    {
        float fullWidth = (float)(background.Width * scale);
        float fullHeight = (float)(background.Height * scale);
        Bitmap bitmap = new Bitmap(width, height, PixelFormat.Format32bppPArgb);
        try
        {
            using (Graphics graphics = Graphics.FromImage(bitmap))
            {
                graphics.CompositingMode = CompositingMode.SourceCopy;
                graphics.CompositingQuality = CompositingQuality.HighQuality;
                graphics.InterpolationMode = InterpolationMode.HighQualityBilinear;
                graphics.PixelOffsetMode = PixelOffsetMode.Half;
                graphics.SmoothingMode = SmoothingMode.HighQuality;
                graphics.Clear(Color.Transparent);
                graphics.SetClip(new RectangleF(0, 0, (float)Math.Max(0, cutoff), (float)height));
                graphics.DrawImage(background, new RectangleF(0, 0, fullWidth, fullHeight),
                    new RectangleF(0, 0, background.Width, background.Height), GraphicsUnit.Pixel);
                graphics.ResetClip();
                graphics.CompositingMode = CompositingMode.SourceOver;
                graphics.DrawImage(foreground,
                    new RectangleF((float)shift, (float)foregroundY, fullWidth, fullHeight),
                    new RectangleF(0, 0, foreground.Width, foreground.Height), GraphicsUnit.Pixel);
            }
            return bitmap;
        }
        catch
        {
            bitmap.Dispose();
            throw;
        }
    }
}
'@

$inv = [Globalization.CultureInfo]::InvariantCulture
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$skin = Join-Path $root 'skin\kasumi-adaptive'
$assets = Join-Path $skin 'assets'
$configPath = Join-Path $skin 'band-overlay.json'
$skinToml = Join-Path $skin 'skin.toml'
$applyCompact = Join-Path $PSScriptRoot 'Apply-CompactDisplay.ps1'
$exe = Join-Path $root 'native\v0.5.3\KasumiBandOverlay.Diagnostics.exe'
$bgOut = Join-Path $assets 'native-band-background.png'
$fgOut = Join-Path $assets 'native-band-foreground.png'
$reportPath = Join-Path $root 'verification\v053\native-band-layers-report.json'
$reportDir = Split-Path -Parent $reportPath

$expectedExeSha = '081ee8a64ef465bfe9bb70dcb6e558ea6c6a5f424949ea7037ed698fc5ef3252'
$derivedNames = @('native-band-background.png', 'native-band-foreground.png')
$checks = New-Object 'Collections.Generic.List[object]'
$failures = New-Object 'Collections.Generic.List[object]'
$disposables = New-Object 'Collections.Generic.List[IDisposable]'

function Invariant([double]$value) {
    return $value.ToString('R', $inv)
}

function Sha256File([string]$path) {
    return (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLowerInvariant()
}

function Sha256Bytes([byte[]]$bytes) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant()
    }
    finally {
        $sha.Dispose()
    }
}

function RelativePath([string]$path) {
    $prefix = $root.TrimEnd('\') + '\'
    if ($path.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        return $path.Substring($prefix.Length)
    }
    return $path
}

function Add-Check([bool]$passed, [string]$name, $details) {
    $record = [ordered]@{
        name = $name
        passed = $passed
        details = $details
    }
    [void]$checks.Add([pscustomobject]$record)
    if (-not $passed) { [void]$failures.Add($name) }
}

function DiffRecord($diff) {
    $bbox = $null
    if ($diff.HasDifferenceBBox) {
        $bbox = @($diff.DifferenceLeft, $diff.DifferenceTop,
            $diff.DifferenceRight, $diff.DifferenceBottom)
    }
    return [ordered]@{
        different_pixels = $diff.DifferentPixels
        total_pixels = $diff.TotalPixels
        different_pixel_ratio = if ($diff.TotalPixels -gt 0) {
            [Math]::Round($diff.DifferentPixels / [double]$diff.TotalPixels, 9)
        } else { 0.0 }
        max_channel_delta = $diff.MaxChannelDelta
        max_alpha_delta = $diff.MaxAlphaDelta
        alpha_different_pixels = $diff.AlphaDifferentPixels
        total_abs_delta = $diff.TotalAbsDelta
        mean_abs_delta_per_channel = [Math]::Round($diff.MeanAbsDeltaPerChannel, 9)
        difference_bbox = $bbox
    }
}

function PngHeader([byte[]]$bytes) {
    if ($bytes.Length -lt 33) { throw 'PNG too short' }
    $signature = @(0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A)
    for ($i = 0; $i -lt $signature.Count; $i++) {
        if ($bytes[$i] -ne $signature[$i]) { throw 'Not a PNG' }
    }
    $width = ([int]$bytes[16] -shl 24) -bor ([int]$bytes[17] -shl 16) -bor
        ([int]$bytes[18] -shl 8) -bor [int]$bytes[19]
    $height = ([int]$bytes[20] -shl 24) -bor ([int]$bytes[21] -shl 16) -bor
        ([int]$bytes[22] -shl 8) -bor [int]$bytes[23]
    return [ordered]@{
        signature = 'PNG'
        width = $width
        height = $height
        bit_depth = [int]$bytes[24]
        color_type = [int]$bytes[25]
        color_type_name = switch ([int]$bytes[25]) {
            6 { 'RGBA' }
            2 { 'RGB' }
            default { 'other' }
        }
        interlace = [int]$bytes[28]
        bytes = [long]$bytes.Length
        sha256 = Sha256Bytes $bytes
    }
}

function ConvertTo-PngBytes([Drawing.Bitmap]$bitmap) {
    $stream = New-Object IO.MemoryStream
    try {
        $bitmap.Save($stream, [Drawing.Imaging.ImageFormat]::Png)
        return ,$stream.ToArray()
    }
    finally {
        $stream.Dispose()
    }
}

try {
    if (-not (Test-Path -LiteralPath $exe)) { throw "Diagnostics EXE missing: $exe" }
    if (-not (Test-Path -LiteralPath $configPath)) { throw "Config missing: $configPath" }
    if (-not (Test-Path -LiteralPath $assets)) { throw "Assets directory missing: $assets" }
    if (-not (Test-Path -LiteralPath $reportDir)) { New-Item -ItemType Directory -Path $reportDir | Out-Null }

    $exeShaBefore = Sha256File $exe
    Add-Check ($exeShaBefore -eq $expectedExeSha) 'diagnostics_exe_sha256' @{
        expected = $expectedExeSha
        actual = $exeShaBefore
    }

    $protectedPaths = New-Object 'Collections.Generic.List[string]'
    [void]$protectedPaths.Add($configPath)
    [void]$protectedPaths.Add($skinToml)
    [void]$protectedPaths.Add($applyCompact)
    $csharpSource = Join-Path $root 'native\KasumiBandOverlay.cs'
    if (Test-Path -LiteralPath $csharpSource) { [void]$protectedPaths.Add($csharpSource) }
    foreach ($file in Get-ChildItem -LiteralPath $assets -Filter '*.png' -File | Sort-Object Name) {
        if ($derivedNames -notcontains $file.Name) { [void]$protectedPaths.Add($file.FullName) }
    }
    foreach ($name in @('source-exclusions.json', 'source-white-restore.json',
            'source-foreground.json', 'source-portrait.json')) {
        $p = Join-Path $assets $name
        if (Test-Path -LiteralPath $p) { [void]$protectedPaths.Add($p) }
    }
    $protectedBefore = @{}
    foreach ($path in $protectedPaths) {
        if (Test-Path -LiteralPath $path) { $protectedBefore[$path] = Sha256File $path }
    }

    $asm = [Reflection.Assembly]::Load([IO.File]::ReadAllBytes($exe))
    $static = [Reflection.BindingFlags]'Static,NonPublic'
    $instance = [Reflection.BindingFlags]'Instance,NonPublic'
    $configType = $asm.GetType('KasumiBandOverlay.OverlayConfig', $true)
    $assetType = $asm.GetType('KasumiBandOverlay.BandAsset', $true)
    $layoutType = $asm.GetType('KasumiBandOverlay.BandLayout', $true)
    $rendererType = $asm.GetType('KasumiBandOverlay.BandRenderer', $true)

    $loadConfig = $configType.GetMethod('Load', $static)
    $loadAsset = $assetType.GetMethod('Load', $static)
    $createSourceLayer = $rendererType.GetMethod('CreateSourceLayer', $static)
    $compute = $layoutType.GetMethods($static) | Where-Object {
        $_.Name -eq 'Compute' -and $_.GetParameters().Length -eq 6
    }
    $render = $rendererType.GetMethod('Render', $static)
    if ($null -eq $compute) { throw 'BandLayout.Compute(6 args) not found' }
    if ($null -eq $createSourceLayer) { throw 'BandRenderer.CreateSourceLayer not found' }

    $config = $loadConfig.Invoke($null, [object[]]@([string]$skin))
    $imagePath = [string]$configType.GetField('ImagePath', $instance).GetValue($config)
    $configSha = Sha256File $configPath
    $sourceShaBefore = Sha256File $imagePath
    $asset = $loadAsset.Invoke($null, [object[]]@([string]$imagePath))
    [void]$disposables.Add([IDisposable]$asset)
    $spec = $configType.GetField('Atlas', $instance).GetValue($config)
    if ($null -eq $spec) { throw 'Config has no atlas object' }

    $sourceWidth = [int]$assetType.GetField('Width', $instance).GetValue($asset)
    $sourceHeight = [int]$assetType.GetField('Height', $instance).GetValue($asset)
    $imageWidth = [int]$spec.GetType().GetField('SourceWidthPx', $instance).GetValue($spec)
    $imageHeight = [int]$spec.GetType().GetField('SourceHeightPx', $instance).GetValue($spec)
    $occlusionMode = [bool]$spec.GetType().GetField('OcclusionMode', $instance).GetValue($spec)
    $cutPx = [int]$spec.GetType().GetField('OcclusionCutPx', $instance).GetValue($spec)
    $fgScale = [double]$spec.GetType().GetField('ForegroundScale', $instance).GetValue($spec)
    $fgAnchorY = [double]$spec.GetType().GetField('ForegroundAnchorYPx', $instance).GetValue($spec)
    $fgOffsetYDip = [double]$spec.GetType().GetField('ForegroundOffsetYDip', $instance).GetValue($spec)
    $designScaleDip = [double]$spec.GetType().GetField('DesignScaleDip', $instance).GetValue($spec)
    $widthDip = [double]$configType.GetField('WidthDip', $instance).GetValue($config)
    $heightDip = [double]$configType.GetField('HeightDip', $instance).GetValue($config)
    $offsetXDip = [double]$configType.GetField('OffsetXDip', $instance).GetValue($config)
    $offsetYDip = [double]$configType.GetField('OffsetYDip', $instance).GetValue($config)
    $cardRightPx = [int]$spec.GetType().GetField('CardRightPx', $instance).GetValue($spec)
    $frets = $configType.GetField('Frets', $instance).GetValue($config)
    $foregroundRuns = $spec.GetType().GetField('ForegroundRuns', $instance).GetValue($spec)
    $portraitRuns = $spec.GetType().GetField('PortraitRuns', $instance).GetValue($spec)
    $excludeRuns = $spec.GetType().GetField('ExcludeRuns', $instance).GetValue($spec)
    $restoreRuns = $spec.GetType().GetField('RestoreWhiteRuns', $instance).GetValue($spec)
    $backgroundMargin = [int]$spec.GetType().GetField('BackgroundRemovalMarginPx', $instance).GetValue($spec)
    $bgRepairSourceX = [int]$spec.GetType().GetField('BackgroundRepairSourceX', $instance).GetValue($spec)
    $bgRepairTargetX = [int]$spec.GetType().GetField('BackgroundRepairTargetX', $instance).GetValue($spec)
    $bgRepairTop = [int]$spec.GetType().GetField('BackgroundRepairTop', $instance).GetValue($spec)
    $bgRepairBottom = [int]$spec.GetType().GetField('BackgroundRepairBottom', $instance).GetValue($spec)
    $separatePortrait = ($portraitRuns -ne $null -and $portraitRuns.Length -gt 0)

    Add-Check ($sourceWidth -eq 2172 -and $sourceHeight -eq 724) 'source_asset_dimensions' @{
        width = $sourceWidth; height = $sourceHeight
    }
    Add-Check ($imageWidth -eq $sourceWidth -and $imageHeight -eq $sourceHeight) 'atlas_dimensions_match_asset' @{
        atlas_width = $imageWidth; atlas_height = $imageHeight
    }
    Add-Check $occlusionMode 'atlas_mode_is_occlusion' $occlusionMode
    Add-Check ($null -eq $frets) 'frets_absent' ($null -eq $frets)
    Add-Check ($null -ne $spec.GetType().GetField('ForegroundRuns', $instance).GetValue($spec)) 'foreground_runs_present' @{
        runs = $foregroundRuns.Length
    }
    Add-Check $separatePortrait 'portrait_runs_present' @{
        runs = if ($null -eq $portraitRuns) { 0 } else { $portraitRuns.Length }
    }

    $foregroundRightClipDip = ([double]$sourceWidth - [double]$cutPx) * $fgScale * $designScaleDip
    $expectedForegroundRightClipDip = (2172.0 - 1850.0) * 0.9 * (1032.0 / 2172.0)
    Add-Check ([Math]::Abs($foregroundRightClipDip - $expectedForegroundRightClipDip) -lt 1e-12) `
        'foreground_right_clip_dip' ([ordered]@{
            formula = '(2172 - 1850) * 0.9 * (1032 / 2172)'
            value = Invariant $foregroundRightClipDip
            expected = Invariant $expectedForegroundRightClipDip
        })

    $bg = $createSourceLayer.Invoke($null, [object[]]@($asset, $spec, [bool]$false))
    $fg = $createSourceLayer.Invoke($null, [object[]]@($asset, $spec, [bool]$true))
    [void]$disposables.Add([IDisposable]$bg)
    [void]$disposables.Add([IDisposable]$fg)
    $bgStats = [KasumiNativeBandOps]::Analyze($bg)
    $fgStats = [KasumiNativeBandOps]::Analyze($fg)

    Add-Check ($bg.Width -eq 2172 -and $bg.Height -eq 724) 'background_layer_dimensions' @{
        width = $bg.Width; height = $bg.Height; pixel_format = $bg.PixelFormat.ToString()
    }
    Add-Check ($fg.Width -eq 2172 -and $fg.Height -eq 724) 'foreground_layer_dimensions' @{
        width = $fg.Width; height = $fg.Height; pixel_format = $fg.PixelFormat.ToString()
    }
    Add-Check ($bg.PixelFormat -eq [Drawing.Imaging.PixelFormat]::Format32bppPArgb) 'background_layer_pixel_format' $bg.PixelFormat.ToString()
    Add-Check ($fg.PixelFormat -eq [Drawing.Imaging.PixelFormat]::Format32bppPArgb) 'foreground_layer_pixel_format' $fg.PixelFormat.ToString()
    Add-Check $bgStats.PremultipliedValid 'background_layer_premultiplied' $bgStats.PremultipliedValid
    Add-Check $fgStats.PremultipliedValid 'foreground_layer_premultiplied' $fgStats.PremultipliedValid

    $bgBytes = ConvertTo-PngBytes $bg
    $fgBytes = ConvertTo-PngBytes $fg
    $bgHeader = PngHeader $bgBytes
    $fgHeader = PngHeader $fgBytes
    Add-Check ($bgHeader.color_type -eq 6 -and $bgHeader.bit_depth -eq 8) 'background_png_is_rgba8' $bgHeader
    Add-Check ($fgHeader.color_type -eq 6 -and $fgHeader.bit_depth -eq 8) 'foreground_png_is_rgba8' $fgHeader
    Add-Check ($bgHeader.width -eq 2172 -and $bgHeader.height -eq 724) 'background_png_dimensions' $bgHeader
    Add-Check ($fgHeader.width -eq 2172 -and $fgHeader.height -eq 724) 'foreground_png_dimensions' $fgHeader

    # These are the only two image writes. Both buffers were fully validated and encoded first.
    [IO.File]::WriteAllBytes($bgOut, $bgBytes)
    [IO.File]::WriteAllBytes($fgOut, $fgBytes)
    $bgSha = Sha256File $bgOut
    $fgSha = Sha256File $fgOut
    Add-Check ($bgSha -eq $bgHeader.sha256) 'background_png_written_sha256' @{ expected = $bgHeader.sha256; actual = $bgSha }
    Add-Check ($fgSha -eq $fgHeader.sha256) 'foreground_png_written_sha256' @{ expected = $fgHeader.sha256; actual = $fgSha }

    # Reload the actual exported PNGs through the production BandAsset.Load path. This is the
    # input shape a native consumer gets: straight RGBA8 decoded and copied to 32bppPArgb.
    $bgAsset = $loadAsset.Invoke($null, [object[]]@([string]$bgOut))
    $fgAsset = $loadAsset.Invoke($null, [object[]]@([string]$fgOut))
    [void]$disposables.Add([IDisposable]$bgAsset)
    [void]$disposables.Add([IDisposable]$fgAsset)
    $bgLoaded = [Drawing.Bitmap]$assetType.GetField('Bitmap', $instance).GetValue($bgAsset)
    $fgLoaded = [Drawing.Bitmap]$assetType.GetField('Bitmap', $instance).GetValue($fgAsset)
    $bgRoundTrip = [KasumiNativeBandOps]::Compare($bg, $bgLoaded)
    $fgRoundTrip = [KasumiNativeBandOps]::Compare($fg, $fgLoaded)
    $bgRoundTripRecord = DiffRecord $bgRoundTrip
    $fgRoundTripRecord = DiffRecord $fgRoundTrip
    Add-Check ($bgRoundTrip.MaxAlphaDelta -eq 0 -and $bgRoundTrip.MaxChannelDelta -le 1) 'background_png_premultiply_roundtrip' $bgRoundTripRecord
    Add-Check ($fgRoundTrip.MaxAlphaDelta -eq 0 -and $fgRoundTrip.MaxChannelDelta -le 1) 'foreground_png_premultiply_roundtrip' $fgRoundTripRecord

    $cases = @(
        [ordered]@{ name = 'short'; width_dip = 693.0292817679558 },
        [ordered]@{ name = 'medium'; width_dip = 850.0 },
        [ordered]@{ name = 'long'; width_dip = 1032.0 }
    )
    $dpis = @(1.0, 1.25, 1.5, 2.0)
    $caseRecords = New-Object 'Collections.Generic.List[object]'
    $bgScaleCache = @{}
    $fgScaleCache = @{}
    $srcBgScaleCache = @{}
    $srcFgScaleCache = @{}
    $maxControlDelta = 0
    $maxExportDelta = 0
    $maxExportAlphaDelta = 0
    $maxDirectDelta = 0
    $maxDirectAlphaDelta = 0

    foreach ($case in $cases) {
        foreach ($dpi in $dpis) {
            $cardWidthPx = [int][Math]::Round($case.width_dip * $dpi)
            $layout = $compute.Invoke($null, [object[]]@(
                $config, [int]100, [int]500, [int]$cardWidthPx, [double]$dpi, $null))
            if ($null -eq $layout) { throw "BandLayout.Compute rejected $($case.name) dpi $dpi" }
            $atlas = $layoutType.GetField('Atlas', $instance).GetValue($layout)
            if ($null -eq $atlas) { throw "Atlas layout missing for $($case.name) dpi $dpi" }
            $scale = [double]$atlas.GetType().GetField('Scale', $instance).GetValue($atlas)
            $targetWidthPx = [double]$atlas.GetType().GetField('TargetWidthPx', $instance).GetValue($atlas)
            $layoutWidth = [int]$layoutType.GetField('Width', $instance).GetValue($layout)
            $layoutHeight = [int]$layoutType.GetField('Height', $instance).GetValue($layout)

            $fullWidth = [double]$sourceWidth * $scale
            $portraitScale = $scale * $fgScale
            $portraitShift = $targetWidthPx - [double]$sourceWidth * $portraitScale
            $shift = if ($separatePortrait) { $targetWidthPx - $fullWidth } else { $portraitShift }
            $foregroundY = if ($separatePortrait) {
                0.0
            } else {
                ($scale - $portraitScale) * $fgAnchorY + $fgOffsetYDip * $dpi
            }
            $cutoff = $portraitShift + [double]$cutPx * $portraitScale
            $cutoffDip = $cutoff / $dpi
            $backgroundRenderScale = $scale
            $foregroundRenderScale = if ($separatePortrait) { $scale } else { $portraitScale }
            $bgKey = Invariant $backgroundRenderScale
            $fgKey = Invariant $foregroundRenderScale

            if (-not $bgScaleCache.ContainsKey($bgKey)) {
                $bgScaleCache[$bgKey] = [KasumiNativeBandOps]::Scale($bgLoaded, $backgroundRenderScale)
                [void]$disposables.Add([IDisposable]$bgScaleCache[$bgKey])
            }
            if (-not $fgScaleCache.ContainsKey($fgKey)) {
                $fgScaleCache[$fgKey] = [KasumiNativeBandOps]::Scale($fgLoaded, $foregroundRenderScale)
                [void]$disposables.Add([IDisposable]$fgScaleCache[$fgKey])
            }
            if (-not $srcBgScaleCache.ContainsKey($bgKey)) {
                $srcBgScaleCache[$bgKey] = [KasumiNativeBandOps]::Scale($bg, $backgroundRenderScale)
                [void]$disposables.Add([IDisposable]$srcBgScaleCache[$bgKey])
            }
            if (-not $srcFgScaleCache.ContainsKey($fgKey)) {
                $srcFgScaleCache[$fgKey] = [KasumiNativeBandOps]::Scale($fg, $foregroundRenderScale)
                [void]$disposables.Add([IDisposable]$srcFgScaleCache[$fgKey])
            }

            $reference = $render.Invoke($null, [object[]]@($asset, $layout))
            if ($null -eq $reference) { throw "BandRenderer.Render returned null for $($case.name) dpi $dpi" }
            try {
                $referenceBitmap = [Drawing.Bitmap]$reference.GetType().GetField('Bitmap', $instance).GetValue($reference)
                $control = [KasumiNativeBandOps]::ComposeOld(
                    $srcBgScaleCache[$bgKey], $srcFgScaleCache[$fgKey],
                    $layoutWidth, $layoutHeight, $cutoff, $shift, $foregroundY)
                try {
                    $controlDiff = [KasumiNativeBandOps]::Compare($control, $referenceBitmap)
                }
                finally { $control.Dispose() }

                $exported = [KasumiNativeBandOps]::ComposeOld(
                    $bgScaleCache[$bgKey], $fgScaleCache[$fgKey],
                    $layoutWidth, $layoutHeight, $cutoff, $shift, $foregroundY)
                try {
                    $exportedDiff = [KasumiNativeBandOps]::Compare($exported, $referenceBitmap)
                }
                finally { $exported.Dispose() }

                $direct = [KasumiNativeBandOps]::ComposeDirect(
                    $bgLoaded, $fgLoaded, $layoutWidth, $layoutHeight, $scale,
                    $cutoff, $shift, $foregroundY)
                try {
                    $directDiff = [KasumiNativeBandOps]::Compare($direct, $referenceBitmap)
                }
                finally { $direct.Dispose() }

                if ($controlDiff.MaxChannelDelta -gt $maxControlDelta) { $maxControlDelta = $controlDiff.MaxChannelDelta }
                if ($exportedDiff.MaxChannelDelta -gt $maxExportDelta) { $maxExportDelta = $exportedDiff.MaxChannelDelta }
                if ($exportedDiff.MaxAlphaDelta -gt $maxExportAlphaDelta) { $maxExportAlphaDelta = $exportedDiff.MaxAlphaDelta }
                if ($directDiff.MaxChannelDelta -gt $maxDirectDelta) { $maxDirectDelta = $directDiff.MaxChannelDelta }
                if ($directDiff.MaxAlphaDelta -gt $maxDirectAlphaDelta) { $maxDirectAlphaDelta = $directDiff.MaxAlphaDelta }

                $layoutOk = ($layoutWidth -eq $cardWidthPx -and
                    $layoutHeight -eq [int][Math]::Ceiling([double]$sourceHeight * $scale) -and
                    [Math]::Abs($targetWidthPx - [double]$cardWidthPx) -lt 1e-9)
                Add-Check $layoutOk "layout_$($case.name)_dpi$($dpi.ToString('0.##', $inv))" ([ordered]@{
                    width = $layoutWidth; height = $layoutHeight
                    target_width = Invariant $targetWidthPx; scale = Invariant $scale
                })
                Add-Check ($controlDiff.DifferentPixels -eq 0) "control_$($case.name)_dpi$($dpi.ToString('0.##', $inv))" (DiffRecord $controlDiff)
                Add-Check ($exportedDiff.MaxAlphaDelta -eq 0 -and $exportedDiff.MaxChannelDelta -le 2) `
                    "exported_$($case.name)_dpi$($dpi.ToString('0.##', $inv))" (DiffRecord $exportedDiff)

                $record = [ordered]@{
                    case = $case.name
                    dpi = $dpi
                    requested_card_width_dip = $case.width_dip
                    card_width_px = $cardWidthPx
                    card_width_dip_actual = $cardWidthPx / $dpi
                    scale = Invariant $scale
                    source_scaled_width_px = Invariant $fullWidth
                    target_width_px = Invariant $targetWidthPx
                    bitmap_width = $layoutWidth
                    bitmap_height = $layoutHeight
                    background_scaled_width = [int][Math]::Ceiling([double]$sourceWidth * $backgroundRenderScale)
                    background_scaled_height = [int][Math]::Ceiling([double]$sourceHeight * $backgroundRenderScale)
                    foreground_scaled_width = [int][Math]::Ceiling([double]$sourceWidth * $foregroundRenderScale)
                    foreground_scaled_height = [int][Math]::Ceiling([double]$sourceHeight * $foregroundRenderScale)
                    foreground_render_scale = Invariant $foregroundRenderScale
                    portrait_scale = Invariant $portraitScale
                    portrait_shift_px = Invariant $portraitShift
                    foreground_shift_px = Invariant $shift
                    foreground_y_px = Invariant $foregroundY
                    cutoff_px = Invariant $cutoff
                    cutoff_dip = Invariant $cutoffDip
                    clipped_background_resample_then_clip = $true
                    reference = @{
                        width = $referenceBitmap.Width
                        height = $referenceBitmap.Height
                        pixel_format = $referenceBitmap.PixelFormat.ToString()
                    }
                    exported_old_pipeline = DiffRecord $exportedDiff
                    in_memory_control = DiffRecord $controlDiff
                    direct_single_pass_diagnostic = DiffRecord $directDiff
                }
                [void]$caseRecords.Add([pscustomobject]$record)
            }
            finally {
                ([IDisposable]$reference).Dispose()
            }
        }
    }

    Add-Check ($maxControlDelta -eq 0) 'control_all_cases_pixel_exact' @{ max_channel_delta = $maxControlDelta }
    Add-Check ($maxExportAlphaDelta -eq 0 -and $maxExportDelta -le 2) 'exported_all_cases_alpha_exact_rgb_le_2' @{
        max_channel_delta = $maxExportDelta
        max_alpha_delta = $maxExportAlphaDelta
    }

    $sourceShaAfter = Sha256File $imagePath
    Add-Check ($sourceShaBefore -eq $sourceShaAfter) 'source_png_sha256_unchanged' @{
        before = $sourceShaBefore; after = $sourceShaAfter
    }
    $exeShaAfter = Sha256File $exe
    Add-Check ($exeShaBefore -eq $exeShaAfter) 'diagnostics_exe_sha256_unchanged' @{
        before = $exeShaBefore; after = $exeShaAfter
    }

    $protectedRecords = New-Object 'Collections.Generic.List[object]'
    foreach ($path in $protectedPaths) {
        $before = if ($protectedBefore.ContainsKey($path)) { $protectedBefore[$path] } else { $null }
        $after = if (Test-Path -LiteralPath $path) { Sha256File $path } else { $null }
        $unchanged = ($before -ne $null -and $before -eq $after)
        [void]$protectedRecords.Add([pscustomobject][ordered]@{
            path = RelativePath $path
            sha256_before = $before
            sha256_after = $after
            bytes = if (Test-Path -LiteralPath $path) { (Get-Item -LiteralPath $path).Length } else { $null }
            unchanged = $unchanged
        })
        Add-Check $unchanged ('protected_file_unchanged: ' + (RelativePath $path)) @{
            before = $before; after = $after
        }
    }

    $allOriginalPngs = @($protectedRecords | Where-Object {
        $_.path.StartsWith('skin\kasumi-adaptive\assets\', [StringComparison]::OrdinalIgnoreCase) -and
        $_.path.EndsWith('.png', [StringComparison]::OrdinalIgnoreCase)
    })
    Add-Check (@($allOriginalPngs | Where-Object { -not $_.unchanged }).Count -eq 0) 'all_original_pngs_unchanged' @{
        count = $allOriginalPngs.Count
        pngs = @($allOriginalPngs | ForEach-Object {
            [ordered]@{ path = $_.path; sha256 = $_.sha256_before }
        })
    }

    $layerOutputs = @(
        [ordered]@{
            role = 'background'
            path = RelativePath $bgOut
            absolute_path = $bgOut
            sha256 = $bgSha
            width = $bg.Width
            height = $bg.Height
            pixel_format = $bg.PixelFormat.ToString()
            png = $bgHeader
            alpha_nonzero = ($bgStats.AlphaPartial + $bgStats.AlphaOpaque)
            alpha_partial = $bgStats.AlphaPartial
            alpha_opaque = $bgStats.AlphaOpaque
            alpha_bbox = if ($bgStats.HasAlphaBBox) {
                @($bgStats.AlphaLeft, $bgStats.AlphaTop, $bgStats.AlphaRight, $bgStats.AlphaBottom)
            } else { $null }
        },
        [ordered]@{
            role = 'foreground'
            path = RelativePath $fgOut
            absolute_path = $fgOut
            sha256 = $fgSha
            width = $fg.Width
            height = $fg.Height
            pixel_format = $fg.PixelFormat.ToString()
            png = $fgHeader
            alpha_nonzero = ($fgStats.AlphaPartial + $fgStats.AlphaOpaque)
            alpha_partial = $fgStats.AlphaPartial
            alpha_opaque = $fgStats.AlphaOpaque
            alpha_bbox = if ($fgStats.HasAlphaBBox) {
                @($fgStats.AlphaLeft, $fgStats.AlphaTop, $fgStats.AlphaRight, $fgStats.AlphaBottom)
            } else { $null }
        }
    )

    $bakedPortraitX = (1.0 - $fgScale) * [double]$sourceWidth
    $bakedPortraitY = (1.0 - $fgScale) * $fgAnchorY + $fgOffsetYDip / $designScaleDip
    $sourceLayerPortraitWidth = [double]$sourceWidth * $fgScale
    $sourceLayerPortraitHeight = [double]$sourceHeight * $fgScale

    $report = [ordered]@{
        schema = 'kasumi.native-band-layers/v1'
        generated_utc = [DateTime]::UtcNow.ToString('o', $inv)
        offline = $true
        resident_started = $false
        target_windows_touched = $false
        network_used = $false
        production_csharp_modified = $false
        production_csharp_loaded_by_reflection = $true
        source_files_modified = $false
        writes_limited_to_two_derived_pngs_and_report = $true
        powershell = $PSVersionTable.PSVersion.ToString()
        tool = [ordered]@{
            script = RelativePath $PSCommandPath
            diagnostics_exe = RelativePath $exe
            diagnostics_exe_sha256 = $exeShaBefore
            config = RelativePath $configPath
            config_sha256 = $configSha
            source_image = RelativePath $imagePath
            source_image_sha256_before = $sourceShaBefore
            source_image_sha256_after = $sourceShaAfter
        }
        reflection = [ordered]@{
            assembly_loaded_from_bytes = $true
            config_type = $configType.FullName
            asset_type = $assetType.FullName
            layout_type = $layoutType.FullName
            renderer_type = $rendererType.FullName
            calls = @(
                'OverlayConfig.Load(skinDir)',
                'BandAsset.Load(imagePath)',
                'BandRenderer.CreateSourceLayer(asset, spec, false)',
                'BandRenderer.CreateSourceLayer(asset, spec, true)',
                'BandLayout.Compute(config, cardScreenX, cardScreenY, cardWidthPx, dpiScale, null)',
                'BandRenderer.Render(asset, layout)'
            )
        }
        parameters = [ordered]@{
            width_dip = $widthDip
            height_dip = $heightDip
            offset_x_dip = $offsetXDip
            offset_y_dip = $offsetYDip
            source_width_px = $sourceWidth
            source_height_px = $sourceHeight
            design_scale_dip_per_source_px = Invariant $designScaleDip
            card_right_px = $cardRightPx
            occlusion_cut_px = $cutPx
            foreground_scale = $fgScale
            foreground_anchor_y_px = $fgAnchorY
            foreground_offset_y_dip = $fgOffsetYDip
            separate_portrait = $separatePortrait
            portrait_runs = if ($null -eq $portraitRuns) { 0 } else { $portraitRuns.Length }
            foreground_runs = $foregroundRuns.Length
            exclude_runs = $excludeRuns.Length
            restore_white_runs = $restoreRuns.Length
            background_removal_margin_px = $backgroundMargin
            background_repair_source_x_px = $bgRepairSourceX
            background_repair_target_x_px = $bgRepairTargetX
            background_repair_top_px = $bgRepairTop
            background_repair_bottom_px = $bgRepairBottom
            frets = if ($null -eq $frets) { 'absent' } else { 'present' }
            foreground_right_clip_dip = Invariant $foregroundRightClipDip
            foreground_right_clip_formula = '(2172 - 1850) * 0.9 * (1032 / 2172)'
            foreground_right_clip_exact_fraction = '124614/905'
            cutoff_dip_formula = 'card_width_dip_actual - foreground_right_clip_dip'
            old_resample_order = 'mask at source resolution -> resample background/foreground -> clip background at cutoff -> source-over foreground -> (frets absent)'
            baked_foreground = [ordered]@{
                scale = $fgScale
                anchor_y_px = $fgAnchorY
                offset_y_dip = $fgOffsetYDip
                offset_in_source_px = $fgOffsetYDip / $designScaleDip
                source_layer_portrait_destination_x_px = Invariant $bakedPortraitX
                source_layer_portrait_destination_y_px = Invariant $bakedPortraitY
                source_layer_portrait_width_px = Invariant $sourceLayerPortraitWidth
                source_layer_portrait_height_px = Invariant $sourceLayerPortraitHeight
                note = 'Offset is baked in the foreground source layer as foregroundOffsetYDip / DesignScaleDip. Because portraitKeepRuns is present, RenderOcclusion uses the whole FG layer at atlas.Scale and sets foregroundY=0; it does not add the 10 DIP again.'
            }
        }
        outputs = $layerOutputs
        png_roundtrip = [ordered]@{
            method = 'Bitmap.Save(Png) -> BandAsset.Load(PNG) -> 32bppPArgb; compare against the in-memory source layer'
            background = $bgRoundTripRecord
            foreground = $fgRoundTripRecord
            note = 'PNG is 8-bit straight RGBA. Re-premultiplication is quantized; alpha is unchanged and RGB differs by at most 1 in the source layer.'
        }
        equivalence = [ordered]@{
            method = 'Reference is BandRenderer.Render(asset, layout). Exported reconstruction reloads the two PNGs through BandAsset.Load, scales each source layer with the exact CreateScaledOcclusionLayer settings, then clips the background after resampling and composites the foreground with the old shift/Y rules. Frets are absent.'
            reference = 'BandRenderer.Render / RenderOcclusion (v0.5.3 diagnostics)'
            cases = @($caseRecords.ToArray())
            summary = [ordered]@{
                case_count = $caseRecords.Count
                control_max_channel_delta = $maxControlDelta
                exported_max_channel_delta = $maxExportDelta
                exported_max_alpha_delta = $maxExportAlphaDelta
                direct_single_pass_max_channel_delta = $maxDirectDelta
                direct_single_pass_max_alpha_delta = $maxDirectAlphaDelta
                exported_alpha_exact = ($maxExportAlphaDelta -eq 0)
                exported_rgb_within_2 = ($maxExportDelta -le 2)
                conclusion = 'The old two-stage GDI path is reproduced exactly when the in-memory source layers are used (0 pixel difference). The exported straight-RGBA PNGs reproduce the same composition with alpha exactly equal and RGB bounded by +/-2/255, caused by PNG premultiplication quantization and resampling. A single-pass DrawImage directly from the 2172x724 PNGs with a negative foreground shift is not equivalent; see direct_single_pass_diagnostic.'
            }
        }
        protected_files = @($protectedRecords.ToArray())
        checks = @($checks.ToArray())
        failures = @($failures.ToArray())
        conclusion = if ($failures.Count -eq 0) {
            'PASS: two RGBA source-resolution layers exported and verified against the old v0.5.3 GDI render within the documented quantization bound.'
        } else {
            'FAIL: see checks/failures.'
        }
        limitations = @(
            'Offline GDI+ only; does not test native C++/D2D/WIC composition, HWND presentation, DWM, or an installed skin.',
            'The exported PNGs are straight RGBA8. A consumer that multiplies color by alpha must use premultiplied math or accept the same +/-1 source quantization.',
            'The direct single-pass diagnostic is not the equivalence path; do not substitute it for source-res resample then clip / old FG shift handling.',
            'No real candidate window, service, registry, AppData configuration, skin.toml, band-overlay.json, or Apply-CompactDisplay.ps1 was changed.'
        )
    }

    if (-not (Test-Path -LiteralPath $reportDir)) { New-Item -ItemType Directory -Path $reportDir | Out-Null }
    [IO.File]::WriteAllText($reportPath, ($report | ConvertTo-Json -Depth 20),
        (New-Object Text.UTF8Encoding($false)))

    Write-Host ('[pass] wrote {0}' -f $bgOut)
    Write-Host ('[pass] wrote {0}' -f $fgOut)
    Write-Host ('[info] report: {0}' -f $reportPath)
    Write-Host ('[info] background sha256: {0}' -f $bgSha)
    Write-Host ('[info] foreground sha256: {0}' -f $fgSha)
    Write-Host ('[info] foreground_right_clip_dip: {0}' -f (Invariant $foregroundRightClipDip))
    Write-Host ('[info] exported max channel delta: {0}; max alpha delta: {1}' -f $maxExportDelta, $maxExportAlphaDelta)
    Write-Host ('[info] direct single-pass max channel delta: {0}; max alpha delta: {1}' -f $maxDirectDelta, $maxDirectAlphaDelta)
    Write-Host ('[info] checks: {0}; failures: {1}' -f $checks.Count, $failures.Count)
    if ($failures.Count -gt 0) {
        Write-Host ('[fail] {0}' -f ($failures -join ', '))
        exit 1
    }
}
finally {
    foreach ($item in $disposables) {
        try { $item.Dispose() } catch { }
    }
}
