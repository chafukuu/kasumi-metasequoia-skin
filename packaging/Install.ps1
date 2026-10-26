#Requires -Version 5.1
<#
.SYNOPSIS
    Install the portable Kasumi (v0.5.7) native skin and LiveSD package for Metasequoia IME 0.11.1.

.DESCRIPTION
    - Verifies release-manifest.json and every package file before touching the
      installed input method.
    - Refuses to replace an installed Server whose SHA-256 is not one of the
      explicitly supported hashes.
    - Backs up the installed Server, skin and config.toml under
      DataDirectory\kasumi-backups\<timestamp>.
    - Merges only the required config keys; comments, line endings, BOM and all
      other settings are preserved.
    - Renames a loaded Server to *.previous-<timestamp> and copies the package
      image. No process is force-killed. Restart is requested through the IME's
      own Aux named pipe (UTF-16LE "RestartServer"), and the official Watchdog is
      allowed to restart the Server.
    - If an old KasumiBandOverlay process is present, it is identified by file
      name and --skin-dir, then asked to leave through its documented stop event.
    - Performs a 30-second health check (same PID, named-pipe owner, no new
      Application Error event). On failure, restores the matching original
      Server, skin and config.
    - Never downloads anything or modifies dictionaries or user input history.
      Matching x86/x64 TSF DLL updates are backed up and queued for the next Windows
      restart; the old companion helper is never started.

    Supports -WhatIf. -WhatIf is read-only and does not require elevation; it
    also skips live process checks. A real install must run elevated.

.PARAMETER ServerDirectory
    Directory that contains MetasequoiaImeServer.exe. If omitted, the registry
    ServerPath is used when present; otherwise %ProgramFiles%\metasequoiaime\server.

.PARAMETER DataDirectory
    Metasequoia user data directory (config.toml and skins). If omitted, the
    registry DataDir is used; otherwise %LOCALAPPDATA%\metasequoiaime.

.EXAMPLE
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Install.ps1 -WhatIf

.EXAMPLE
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Install.ps1

.NOTES
    Run Install.cmd for the normal UAC flow. This file is intentionally
    self-contained and uses paths relative to $PSScriptRoot.
#>
[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'High')]
param(
    [ValidateNotNullOrEmpty()]
    [string]$ServerDirectory,

    [ValidateNotNullOrEmpty()]
    [string]$DataDirectory
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$script:PackageRoot = [IO.Path]::GetFullPath($PSScriptRoot)
$script:PackageId = 'kasumi-metasequoia-skin'
$script:SkinId = 'kasumi-adaptive'
$script:PackageVersion = '0.5.7'
$script:ImeVersion = '0.11.1'
$script:CompatibleImeVersions = @('0.11.1', '0.11.2')
$script:ServerFileName = 'MetasequoiaImeServer.exe'
$script:WatchdogFileName = 'MetasequoiaImeWatchdog.exe'
$script:AuxPipeName = 'FanyImeAuxNamedPipe'
$script:OverlayStopEventName = 'Local\KasumiBandOverlay-stop-v1'
$script:FinalServerSha256 = '082A48E4A48A4FE7F723FDE8F62C97C4C5DC386EB8F38B6F38195336B397EA2F'
$script:FinalTsfX64Sha256 = 'D95EA046ECC903ADD6401FF12B39381CCCE6230D915DD5895306DFC96199D5C9'
$script:FinalTsfX86Sha256 = '66DA662603E35184F514C7E3865F5F73EC29C32E071C4DC24394E5C005AD5FE5'
$script:OfficialServerSha256 = '448264BABB98ECEB916094239A7613C3354BADE6E6897E2C7B9E6FC6606913D3'
$script:LegacyDevServerSha256 = 'C36003A3E57AD38C2FED41E0E00DDEAECF548D44D25997C4CED7D9C591317EBA'
$script:PreviousV053V054ServerSha256 = 'F29F375CB2127C4FFAEE0C18E3377D52CB0E433981E8E60248E0C26A1F95CA0C'
$script:PreviousV055ServerSha256 = '8843586A53876C6F626995014D8045DCB800329CBEB1F23A5DDC3DE9FF3FF5DC'
$script:PreviousV056ServerSha256 = '143A5433C46C1D22FFE79CA55AFAB4D688C013CE79969F7DB55AD79D5A6E4B3E'
$script:KnownLiveSDServerSha256 = 'CCDC1552B1690CF56101F92D390C9E37D8DD3E6B261B286F31CB7B4222EA5133'
$script:AllowedInstalledServerHashes = @(
    $script:OfficialServerSha256,
    $script:LegacyDevServerSha256,
    $script:PreviousV053V054ServerSha256,
    $script:PreviousV055ServerSha256,
    $script:PreviousV056ServerSha256,
    $script:KnownLiveSDServerSha256,
    $script:FinalServerSha256
)
$script:CriticalPackageFiles = @(
    'native/MetasequoiaImeServer.exe',
    'native/MetasequoiaImeTsf-x64.dll',
    'native/MetasequoiaImeTsf-x86.dll',
    'skin/kasumi-adaptive/skin.toml',
    'skin/kasumi-adaptive/assets/native-band-background.png',
    'skin/kasumi-adaptive/assets/native-band-foreground.png',
    'skin/kasumi-adaptive/assets/source-longest.png',
    'skin/kasumi-adaptive/assets/guitar-swing.png',
    'skin/kasumi-adaptive/assets/star_b.png',
    'skin/kasumi-adaptive/assets/star_m.png',
    'skin/kasumi-adaptive/assets/star_y.png',
    'skin/kasumi-adaptive/assets/poppinparty-heads.png',
    'skin/kasumi-adaptive/assets/livesd/click-reaction.gif',
    'skin/kasumi-adaptive/assets/livesd/sd001001/anim001.png',
    'skin/kasumi-adaptive/assets/livesd/sd001001/anim002.png',
    'skin/kasumi-adaptive/assets/livesd/sd001001/anim003.png',
    'skin/kasumi-adaptive/assets/livesd/sd001001/anim004.png'
)
$script:Utf8Strict = [Text.UTF8Encoding]::new($false, $true)
$script:Utf8NoBom = [Text.UTF8Encoding]::new($false, $false)
$script:ConfigSpecs = @(
    [pscustomobject]@{ Section = 'appearance'; Key = 'candidate_skin'; Value = '"kasumi-adaptive"'; Kind = 'string' },
    [pscustomobject]@{ Section = 'appearance'; Key = 'candidate_window_fit_content'; Value = 'true'; Kind = 'bool' },
    [pscustomobject]@{ Section = 'appearance'; Key = 'candidate_window_layout'; Value = '"horizontal"'; Kind = 'string' },
    [pscustomobject]@{ Section = 'appearance'; Key = 'candidate_window_single_line'; Value = 'true'; Kind = 'bool' },
    [pscustomobject]@{ Section = 'appearance'; Key = 'candidate_window_preedit_style'; Value = '"empty"'; Kind = 'string' },
    [pscustomobject]@{ Section = 'appearance'; Key = 'font_size'; Value = '15'; Kind = 'integer' },
    [pscustomobject]@{ Section = 'appearance'; Key = 'page_size'; Value = '7'; Kind = 'integer' }
)

function Write-Step {
    param([Parameter(Mandatory = $true)][string]$Message)
    Write-Host ('[kasumi] ' + $Message)
}

function Remove-TrailingSeparator {
    param([Parameter(Mandatory = $true)][string]$Path)
    $full = [IO.Path]::GetFullPath($Path)
    $root = [IO.Path]::GetPathRoot($full)
    if ($full.Length -le $root.Length) { return $full }
    return $full.TrimEnd([char[]]@([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar))
}

function Test-PathInside {
    param(
        [Parameter(Mandatory = $true)][string]$ChildPath,
        [Parameter(Mandatory = $true)][string]$ParentPath,
        [switch]$AllowEqual
    )
    $child = Remove-TrailingSeparator -Path $ChildPath
    $parent = Remove-TrailingSeparator -Path $ParentPath
    $comparison = [StringComparison]::OrdinalIgnoreCase
    if ([string]::Equals($child, $parent, $comparison)) { return $AllowEqual.IsPresent }
    if (-not $parent.EndsWith([string][IO.Path]::DirectorySeparatorChar, [StringComparison]::Ordinal)) {
        $parent += [string][IO.Path]::DirectorySeparatorChar
    }
    return $child.StartsWith($parent, $comparison)
}

function Get-RelativePathUnder {
    param(
        [Parameter(Mandatory = $true)][string]$BasePath,
        [Parameter(Mandatory = $true)][string]$Path
    )
    $base = Remove-TrailingSeparator -Path $BasePath
    $full = Remove-TrailingSeparator -Path $Path
    if (-not (Test-PathInside -ChildPath $full -ParentPath $base -AllowEqual)) {
        throw "Path is not inside the expected root: $full"
    }
    if ([string]::Equals($full, $base, [StringComparison]::OrdinalIgnoreCase)) { return '' }
    return $full.Substring($base.Length + 1).Replace('\', '/')
}

function Get-ChildFilesSafely {
    param([Parameter(Mandatory = $true)][string]$Root)
    Assert-NoReparsePoint -Path $Root -Label "Package directory $Root"
    $files = New-Object 'System.Collections.Generic.List[string]'
    $directories = New-Object 'System.Collections.Generic.Stack[string]'
    $directories.Push($Root)
    while ($directories.Count -gt 0) {
        $directory = $directories.Pop()
        foreach ($entry in [IO.Directory]::EnumerateFileSystemEntries($directory)) {
            $attributes = [IO.File]::GetAttributes($entry)
            if (($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Package tree contains a reparse point, refusing to use it: $entry"
            }
            if (($attributes -band [IO.FileAttributes]::Directory) -ne 0) {
                $directories.Push($entry)
            }
            else { $files.Add($entry) }
        }
    }
    return @($files.ToArray())
}

function Normalize-PackageRelativePath {
    param([Parameter(Mandatory = $true)][string]$Path)
    $normalized = $Path.Trim().Replace('\', '/')
    while ($normalized.StartsWith('./', [StringComparison]::Ordinal)) {
        $normalized = $normalized.Substring(2)
    }
    if ([string]::IsNullOrWhiteSpace($normalized) -or
        $normalized.StartsWith('/', [StringComparison]::Ordinal) -or
        $normalized.Contains(':')) {
        throw "Unsafe package-relative path: $Path"
    }
    $segments = $normalized.Split([char]'/')
    foreach ($segment in $segments) {
        if ([string]::IsNullOrWhiteSpace($segment) -or $segment -eq '.' -or $segment -eq '..') {
            throw "Unsafe package-relative path: $Path"
        }
    }
    return $normalized
}

function Resolve-PackagePath {
    param([Parameter(Mandatory = $true)][string]$RelativePath)
    $normalized = Normalize-PackageRelativePath -Path $RelativePath
    $full = [IO.Path]::GetFullPath((Join-Path $script:PackageRoot $normalized.Replace('/', '\')))
    if (-not (Test-PathInside -ChildPath $full -ParentPath $script:PackageRoot)) {
        throw "Package path escapes the package root: $RelativePath"
    }
    return $full
}

function Assert-NoReparsePoint {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Label
    )
    if ([IO.File]::Exists($Path) -or [IO.Directory]::Exists($Path)) {
        $attributes = [IO.File]::GetAttributes($Path)
        if (($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "$Label is a reparse point, refusing to use it: $Path"
        }
    }
}

function Get-Sha256Bytes {
    param([Parameter(Mandatory = $true)][byte[]]$Bytes)
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($sha.ComputeHash($Bytes))).Replace('-', '')
    }
    finally { $sha.Dispose() }
}

function Get-Sha256File {
    param([Parameter(Mandatory = $true)][string]$Path)
    $sha = [Security.Cryptography.SHA256]::Create()
    $stream = $null
    try {
        $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
        return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '')
    }
    finally {
        if ($null -ne $stream) { $stream.Dispose() }
        $sha.Dispose()
    }
}

function Test-HashEqual {
    param([string]$Left, [string]$Right)
    return [string]::Equals($Left, $Right, [StringComparison]::OrdinalIgnoreCase)
}

function Test-IsAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    try {
        $principal = [Security.Principal.WindowsPrincipal]::new($identity)
        return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    }
    finally { $identity.Dispose() }
}

function Get-RequiredPropertyValue {
    param($Object, [Parameter(Mandatory = $true)][string]$Name)
    if ($null -eq $Object) { throw "Manifest field '$Name' is missing." }
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property -or $null -eq $property.Value) {
        throw "Manifest field '$Name' is missing."
    }
    return $property.Value
}

function Get-MetasequoiaRegistryInfo {
    $info = [pscustomobject]@{
        VersionDir = $null
        DataDir = $null
        ServerPath = $null
        UninstallVersion = $null
    }
    $base = $null
    try {
        $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
            [Microsoft.Win32.RegistryHive]::LocalMachine,
            [Microsoft.Win32.RegistryView]::Registry64)
        $key = $base.OpenSubKey('SOFTWARE\Metasequoia\MetasequoiaIME', $false)
        if ($null -ne $key) {
            try {
                $info.VersionDir = [string]$key.GetValue('VersionDir', '')
                $info.DataDir = [string]$key.GetValue('DataDir', '')
                $info.ServerPath = [string]$key.GetValue('ServerPath', '')
            }
            finally { $key.Dispose() }
        }
        $uninstall = $base.OpenSubKey('SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{A7C3E91F-4B2D-4E8A-9F1C-6D5E8B0A2C4D}_is1', $false)
        if ($null -ne $uninstall) {
            try { $info.UninstallVersion = [string]$uninstall.GetValue('DisplayVersion', '') }
            finally { $uninstall.Dispose() }
        }
    }
    finally {
        if ($null -ne $base) { $base.Dispose() }
    }
    return $info
}

function Get-DetectedImeVersion {
    param($RegistryInfo)
    if ($RegistryInfo.VersionDir -match '^msime_v(?<version>[0-9]+(?:\.[0-9]+){1,3})$') {
        return $Matches['version']
    }
    if (-not [string]::IsNullOrWhiteSpace($RegistryInfo.UninstallVersion)) {
        return $RegistryInfo.UninstallVersion.Trim()
    }
    return $null
}

function Read-ReleaseManifest {
    $manifestPath = Join-Path $script:PackageRoot 'release-manifest.json'
    if (-not [IO.File]::Exists($manifestPath)) {
        throw "release-manifest.json is missing next to Install.ps1: $manifestPath"
    }
    $length = (Get-Item -LiteralPath $manifestPath).Length
    if ($length -le 0 -or $length -gt 1048576) {
        throw "release-manifest.json has an unexpected size: $length bytes"
    }
    $json = [IO.File]::ReadAllText($manifestPath, [Text.Encoding]::UTF8)
    try { $manifest = $json | ConvertFrom-Json }
    catch { throw "release-manifest.json is not valid JSON: $($_.Exception.Message)" }

    if ([int](Get-RequiredPropertyValue -Object $manifest -Name 'schemaVersion') -ne 1) {
        throw 'Unsupported release-manifest.json schemaVersion.'
    }
    if ([string](Get-RequiredPropertyValue -Object $manifest -Name 'package') -ne $script:PackageId) {
        throw 'release-manifest.json package id mismatch.'
    }
    if ([string](Get-RequiredPropertyValue -Object $manifest -Name 'version') -ne $script:PackageVersion) {
        throw 'release-manifest.json version mismatch.'
    }
    $minimumIme = [string](Get-RequiredPropertyValue -Object $manifest -Name 'minimumImeVersion')
    if ($minimumIme -ne $script:ImeVersion) {
        throw "This package requires Metasequoia IME $($script:ImeVersion)."
    }
    $manifestCompatibleVersions = @(Get-RequiredPropertyValue -Object $manifest -Name 'compatibleImeVersions' | ForEach-Object { [string]$_ })
    if (@(Compare-Object ($manifestCompatibleVersions | Sort-Object -Unique) ($script:CompatibleImeVersions | Sort-Object -Unique)).Count -ne 0) {
        throw 'release-manifest.json compatibleImeVersions does not match the installer support list.'
    }

    $nativeRelative = Normalize-PackageRelativePath -Path ([string](Get-RequiredPropertyValue -Object $manifest.native -Name 'path'))
    $nativeSha = [string](Get-RequiredPropertyValue -Object $manifest.native -Name 'sha256')
    if ($nativeRelative -ne 'native/MetasequoiaImeServer.exe' -or
        -not (Test-HashEqual $nativeSha $script:FinalServerSha256)) {
        throw 'release-manifest.json does not describe the expected final native image.'
    }

    $skinId = [string](Get-RequiredPropertyValue -Object $manifest.skin -Name 'id')
    $skinVersion = [string](Get-RequiredPropertyValue -Object $manifest.skin -Name 'version')
    $skinRelative = Normalize-PackageRelativePath -Path ([string](Get-RequiredPropertyValue -Object $manifest.skin -Name 'directory'))
    if ($skinId -ne $script:SkinId -or $skinVersion -ne $script:PackageVersion -or
        $skinRelative -ne "skin/$($script:SkinId)") {
        throw 'release-manifest.json does not describe the expected skin.'
    }

    $rawFiles = @(Get-RequiredPropertyValue -Object $manifest -Name 'files')
    if ($rawFiles.Count -eq 0) { throw 'release-manifest.json files[] is empty.' }
    $files = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in $rawFiles) {
        $relative = Normalize-PackageRelativePath -Path ([string](Get-RequiredPropertyValue -Object $entry -Name 'path'))
        if (-not ($relative.StartsWith('native/', [StringComparison]::OrdinalIgnoreCase) -or
                 $relative.StartsWith("$skinRelative/", [StringComparison]::OrdinalIgnoreCase))) {
            throw "release-manifest.json lists a file that is not a deployable resource: $relative"
        }
        if ($relative.StartsWith('native/', [StringComparison]::OrdinalIgnoreCase) -and
            $relative -notin @('native/MetasequoiaImeServer.exe', 'native/MetasequoiaImeTsf-x64.dll', 'native/MetasequoiaImeTsf-x86.dll')) {
            throw "release-manifest.json contains an unexpected native payload: $relative"
        }
        if ($files.ContainsKey($relative)) {
            throw "release-manifest.json lists a duplicate path: $relative"
        }
        $expectedSize = [long](Get-RequiredPropertyValue -Object $entry -Name 'size')
        $expectedSha = [string](Get-RequiredPropertyValue -Object $entry -Name 'sha256')
        if ($expectedSize -lt 0 -or $expectedSha -notmatch '^[a-fA-F0-9]{64}$') {
            throw "release-manifest.json has invalid size/hash for $relative"
        }
        $fullPath = Resolve-PackagePath -RelativePath $relative
        if (-not [IO.File]::Exists($fullPath)) { throw "Package file is missing: $relative" }
        Assert-NoReparsePoint -Path $fullPath -Label "Package file $relative"
        $actualSize = (Get-Item -LiteralPath $fullPath).Length
        if ($actualSize -ne $expectedSize) {
            throw "Package file size mismatch: $relative (expected $expectedSize, got $actualSize)"
        }
        $actualSha = Get-Sha256File -Path $fullPath
        if (-not (Test-HashEqual $actualSha $expectedSha)) {
            throw "Package file SHA-256 mismatch: $relative"
        }
        $files.Add($relative, [pscustomobject]@{
            RelativePath = $relative
            FullPath = $fullPath
            Size = $actualSize
            Sha256 = $actualSha
        })
    }

    foreach ($critical in $script:CriticalPackageFiles) {
        if (-not $files.ContainsKey($critical)) {
            throw "release-manifest.json is missing a critical resource: $critical"
        }
    }
    foreach ($expectedTsf in @(
            [pscustomobject]@{ Path = 'native/MetasequoiaImeTsf-x64.dll'; Sha256 = $script:FinalTsfX64Sha256 },
            [pscustomobject]@{ Path = 'native/MetasequoiaImeTsf-x86.dll'; Sha256 = $script:FinalTsfX86Sha256 }
        )) {
        if (-not (Test-HashEqual $files[$expectedTsf.Path].Sha256 $expectedTsf.Sha256)) {
            throw "release-manifest.json does not describe the expected $($expectedTsf.Path) payload."
        }
    }
    $nativeTsfEntries = @(Get-RequiredPropertyValue -Object $manifest.native -Name 'tsf')
    if ($nativeTsfEntries.Count -ne 2) { throw 'release-manifest.json must declare exactly two TSF architecture payloads.' }
    foreach ($expectedTsf in @(
            [pscustomobject]@{ Architecture = 'x64'; Path = 'native/MetasequoiaImeTsf-x64.dll' },
            [pscustomobject]@{ Architecture = 'x86'; Path = 'native/MetasequoiaImeTsf-x86.dll' }
        )) {
        $matches = @($nativeTsfEntries | Where-Object { [string]$_.architecture -eq $expectedTsf.Architecture })
        if ($matches.Count -ne 1 -or [string]$matches[0].path -ne $expectedTsf.Path -or
            -not (Test-HashEqual ([string]$matches[0].sha256) $files[$expectedTsf.Path].Sha256) -or
            [long]$matches[0].size -ne $files[$expectedTsf.Path].Size -or
            [string]$matches[0].update -ne 'delayed-until-windows-restart-if-needed') {
            throw "release-manifest.json has an invalid $($expectedTsf.Architecture) TSF declaration."
        }
    }

    $diskFiles = New-Object 'System.Collections.Generic.List[string]'
    foreach ($subdir in @('native', $skinRelative)) {
        $diskRoot = Resolve-PackagePath -RelativePath $subdir
        if (-not [IO.Directory]::Exists($diskRoot)) { throw "Package directory is missing: $subdir" }
        foreach ($diskFile in @(Get-ChildFilesSafely -Root $diskRoot)) {
            $relativeDisk = Normalize-PackageRelativePath -Path (Get-RelativePathUnder -BasePath $script:PackageRoot -Path $diskFile)
            $diskFiles.Add($relativeDisk)
            if (-not $files.ContainsKey($relativeDisk)) {
                throw "Package contains a file absent from release-manifest.json: $relativeDisk"
            }
        }
    }
    if ($diskFiles.Count -ne $files.Count) {
        throw 'Package file set and release-manifest.json files[] do not match.'
    }

    $tomlPath = Resolve-PackagePath -RelativePath "skin/$($script:SkinId)/skin.toml"
    $tomlText = $script:Utf8Strict.GetString([IO.File]::ReadAllBytes($tomlPath))
    $idMatches = [regex]::Matches($tomlText, '(?m)^[ \t]*id[ \t]*=[ \t]*"(?<id>[^"]+)"[ \t]*(?:#.*)?\r?$')
    $versionMatches = [regex]::Matches($tomlText, '(?m)^[ \t]*version[ \t]*=[ \t]*"(?<version>[^"]+)"[ \t]*(?:#.*)?\r?$')
    if ($idMatches.Count -ne 1 -or $idMatches[0].Groups['id'].Value -ne $script:SkinId) {
        throw 'skin.toml does not declare the expected unique skin id.'
    }
    if ($versionMatches.Count -ne 1 -or $versionMatches[0].Groups['version'].Value -ne $script:PackageVersion) {
        throw 'skin.toml does not declare the expected unique skin version.'
    }
    foreach ($requiredText in @(
        '[candidate_window.band_artwork]',
        '[candidate_window.item_separators]',
        'assets/native-band-background.png',
        'assets/native-band-foreground.png',
        '[candidate_window.typed_stars]',
        'image = "assets/guitar-swing.png"',
        'yellow_image = "assets/star_y.png"',
        'pink_image = "assets/star_m.png"',
        'blue_image = "assets/star_b.png"',
        '[candidate_window.livesd]',
        'click_image = "assets/livesd/click-reaction.gif"',
        'idle_image = "assets/livesd/sd001001/anim001.png"',
        'idle2_image = "assets/livesd/sd001001/anim002.png"',
        'happy_image = "assets/livesd/sd001001/anim003.png"',
        'miss_image = "assets/livesd/sd001001/anim004.png"',
        'image = "assets/poppinparty-heads.png"'
    )) {
        if (-not $tomlText.Contains($requiredText)) {
            throw "skin.toml is missing required v0.5.7 content: $requiredText"
        }
    }
    if (-not [regex]::IsMatch($tomlText, '(?m)^[ \t]*single_line[ \t]*=[ \t]*true[ \t]*(?:#.*)?\r?$')) {
        throw 'skin.toml does not enable horizontal single_line.'
    }
    $decorationMatches = [regex]::Matches($tomlText, '(?m)^[ \t]*\[\[\s*candidate_window\s*\.\s*decorations\s*\]\]')
    if ($decorationMatches.Count -ne 2) {
        throw 'skin.toml must declare exactly one native guitar and one PoppinParty pendant decoration.'
    }

    return [pscustomobject]@{
        Path = $manifestPath
        Data = $manifest
        Files = $files
        SkinRelative = $skinRelative
        SkinToml = $tomlText
    }
}

function Find-TomlCommentIndex {
    param([Parameter(Mandatory = $true)][string]$Text)
    $quote = [char]0
    $escaped = $false
    for ($i = 0; $i -lt $Text.Length; $i++) {
        $character = $Text[$i]
        if ($quote -eq [char]0) {
            if ($character -eq '"' -or $character -eq "'") { $quote = $character }
            elseif ($character -eq '#') { return $i }
        }
        elseif ($quote -eq '"') {
            if ($escaped) { $escaped = $false }
            elseif ($character -eq '\') { $escaped = $true }
            elseif ($character -eq '"') { $quote = [char]0 }
        }
        elseif ($character -eq "'") { $quote = [char]0 }
    }
    return -1
}

function Test-TomlSingleLineString {
    param([Parameter(Mandatory = $true)][string]$Value)
    if ($Value.Length -lt 2) { return $false }
    $quote = $Value[0]
    if ($quote -ne '"' -and $quote -ne "'") { return $false }
    if ($Value[$Value.Length - 1] -ne $quote) { return $false }
    if ($quote -eq "'") {
        return -not $Value.Substring(1, $Value.Length - 2).Contains("'")
    }
    $escaped = $false
    for ($i = 1; $i -lt $Value.Length - 1; $i++) {
        $character = $Value[$i]
        if ($escaped) { $escaped = $false; continue }
        if ($character -eq '\') { $escaped = $true; continue }
        if ($character -eq '"') { return $false }
    }
    return -not $escaped
}

function Merge-KasumiConfig {
    param(
        [Parameter(Mandatory = $true)][byte[]]$Bytes,
        [Parameter(Mandatory = $true)][string]$ConfigPath
    )
    $bomLength = 0
    $hasBom = $false
    if ($Bytes.Length -ge 4 -and
        (($Bytes[0] -eq 0xFF -and $Bytes[1] -eq 0xFE) -or ($Bytes[0] -eq 0xFE -and $Bytes[1] -eq 0xFF))) {
        throw "config.toml is UTF-16/UTF-32; refusing to rewrite it: $ConfigPath"
    }
    if ($Bytes.Length -ge 3 -and $Bytes[0] -eq 0xEF -and $Bytes[1] -eq 0xBB -and $Bytes[2] -eq 0xBF) {
        $bomLength = 3
        $hasBom = $true
    }
    try { $text = $script:Utf8Strict.GetString($Bytes, $bomLength, $Bytes.Length - $bomLength) }
    catch { throw "config.toml is not valid UTF-8: $ConfigPath" }

    $specsByQualifiedName = @{}
    foreach ($spec in $script:ConfigSpecs) {
        $qualified = $spec.Section + '|' + $spec.Key
        if ($specsByQualifiedName.ContainsKey($qualified)) {
            throw 'Internal configuration specification is duplicated.'
        }
        $specsByQualifiedName[$qualified] = $spec
    }
    $counts = @{}
    foreach ($spec in $script:ConfigSpecs) { $counts[$spec.Section + '|' + $spec.Key] = 0 }
    $changes = New-Object 'System.Collections.Generic.List[string]'
    $currentSection = ''
    $builder = [Text.StringBuilder]::new()
    $matches = [regex]::Matches($text, '(?<body>[^\r\n]*)(?<eol>\r\n|\n|\r|$)')
    foreach ($match in $matches) {
        if ($match.Index -eq $text.Length -and $match.Length -eq 0) { continue }
        $body = $match.Groups['body'].Value
        $eol = $match.Groups['eol'].Value
        if ($body.TrimStart().StartsWith('[')) {
            $sectionMatch = [regex]::Match($body, '^[ \t]*\[(?<section>[A-Za-z0-9_.-]+)\][ \t]*(?:#.*)?$')
            $currentSection = if ($sectionMatch.Success) { $sectionMatch.Groups['section'].Value } else { '' }
            [void]$builder.Append($body).Append($eol)
            continue
        }
        $assignment = [regex]::Match($body, '^(?<indent>[ \t]*)(?<key>[A-Za-z0-9_]+)(?<separator>[ \t]*=[ \t]*)(?<rest>.*)$')
        if (-not $assignment.Success) {
            [void]$builder.Append($body).Append($eol)
            continue
        }
        $qualified = $currentSection + '|' + $assignment.Groups['key'].Value
        if (-not $specsByQualifiedName.ContainsKey($qualified)) {
            [void]$builder.Append($body).Append($eol)
            continue
        }
        $spec = $specsByQualifiedName[$qualified]
        $counts[$qualified]++
        $rest = $assignment.Groups['rest'].Value
        $commentIndex = Find-TomlCommentIndex -Text $rest
        if ($commentIndex -ge 0) {
            $beforeComment = $rest.Substring(0, $commentIndex)
            $comment = $rest.Substring($commentIndex)
        }
        else {
            $beforeComment = $rest
            $comment = ''
        }
        $currentValue = $beforeComment.Trim()
        if ([string]::IsNullOrWhiteSpace($currentValue)) {
            throw "config.toml line for $($spec.Key) has no value: $ConfigPath"
        }
        $valid = $true
        switch ($spec.Kind) {
            'bool' { $valid = ($currentValue -ceq 'true' -or $currentValue -ceq 'false') }
            'integer' { $valid = [regex]::IsMatch($currentValue, '^[+-]?(?:0|[1-9](?:_?[0-9])*)$') }
            'string' { $valid = Test-TomlSingleLineString -Value $currentValue }
        }
        if (-not $valid) {
            throw "config.toml has an unsafe value for $($spec.Key) at section [$currentSection]: $ConfigPath"
        }
        $trailing = [regex]::Match($beforeComment, '(?<whitespace>[ \t]*)$').Groups['whitespace'].Value
        if ($currentValue -cne $spec.Value) {
            $changes.Add(($(if ($spec.Section) { $spec.Section + '.' } else { '' }) + $spec.Key))
        }
        [void]$builder.Append($assignment.Groups['indent'].Value)
        [void]$builder.Append($assignment.Groups['key'].Value)
        [void]$builder.Append($assignment.Groups['separator'].Value)
        [void]$builder.Append($spec.Value)
        [void]$builder.Append($trailing)
        [void]$builder.Append($comment)
        [void]$builder.Append($eol)
    }

    foreach ($spec in $script:ConfigSpecs) {
        $qualified = $spec.Section + '|' + $spec.Key
        if ($counts[$qualified] -eq 0) {
            throw "config.toml is missing required key '$($spec.Key)' in section [$($spec.Section)]: $ConfigPath"
        }
        if ($counts[$qualified] -gt 1) {
            throw "config.toml has duplicate key '$($spec.Key)' in section [$($spec.Section)]: $ConfigPath"
        }
    }
    $newText = $builder.ToString()
    $bodyBytes = $script:Utf8NoBom.GetBytes($newText)
    if ($hasBom) {
        $newBytes = New-Object 'byte[]' ($bodyBytes.Length + 3)
        $newBytes[0] = 0xEF; $newBytes[1] = 0xBB; $newBytes[2] = 0xBF
        [Array]::Copy($bodyBytes, 0, $newBytes, 3, $bodyBytes.Length)
    }
    else { $newBytes = $bodyBytes }
    return [pscustomobject]@{
        Bytes = $newBytes
        Sha256 = Get-Sha256Bytes -Bytes $newBytes
        ChangedKeys = @($changes.ToArray())
    }
}

function Get-InstallPreflight {
    param(
        [string]$ServerDirectoryOverride,
        [string]$DataDirectoryOverride
    )
    Write-Step 'Checking package integrity...'
    $manifest = Read-ReleaseManifest

    $registry = Get-MetasequoiaRegistryInfo
    $detectedImeVersion = Get-DetectedImeVersion -RegistryInfo $registry
    if ($detectedImeVersion -notin $script:CompatibleImeVersions) {
        $shown = if ([string]::IsNullOrWhiteSpace($detectedImeVersion)) { '<unknown>' } else { $detectedImeVersion }
        throw "This package supports Metasequoia IME $($script:CompatibleImeVersions -join ' or '); detected version: $shown"
    }

    if (-not [string]::IsNullOrWhiteSpace($ServerDirectoryOverride)) {
        $resolvedServerDirectory = Remove-TrailingSeparator -Path ([IO.Path]::GetFullPath($ServerDirectoryOverride))
    }
    elseif (-not [string]::IsNullOrWhiteSpace($registry.ServerPath) -and
            [IO.File]::Exists($registry.ServerPath)) {
        $resolvedServerDirectory = Remove-TrailingSeparator -Path (Split-Path -Parent $registry.ServerPath)
    }
    else {
        if ([string]::IsNullOrWhiteSpace($env:ProgramFiles)) {
            throw 'Cannot infer the Metasequoia Server directory: %ProgramFiles% is not set.'
        }
        $resolvedServerDirectory = Remove-TrailingSeparator -Path ([IO.Path]::GetFullPath((Join-Path $env:ProgramFiles 'metasequoiaime\server')))
    }

    if (-not [string]::IsNullOrWhiteSpace($DataDirectoryOverride)) {
        $resolvedDataDirectory = Remove-TrailingSeparator -Path ([IO.Path]::GetFullPath($DataDirectoryOverride))
    }
    elseif (-not [string]::IsNullOrWhiteSpace($registry.DataDir)) {
        $resolvedDataDirectory = Remove-TrailingSeparator -Path ([IO.Path]::GetFullPath($registry.DataDir))
    }
    else {
        if ([string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
            throw 'Cannot infer the Metasequoia data directory: %LOCALAPPDATA% is not set.'
        }
        $resolvedDataDirectory = Remove-TrailingSeparator -Path ([IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'metasequoiaime')))
    }

    if (-not [IO.Directory]::Exists($resolvedServerDirectory)) {
        throw "Metasequoia Server directory not found: $resolvedServerDirectory"
    }
    if (-not [IO.Directory]::Exists($resolvedDataDirectory)) {
        throw "Metasequoia data directory not found: $resolvedDataDirectory"
    }

    $serverPath = Join-Path $resolvedServerDirectory $script:ServerFileName
    $watchdogPath = Join-Path $resolvedServerDirectory $script:WatchdogFileName
    if ([string]::IsNullOrWhiteSpace($registry.VersionDir) -or $registry.VersionDir -notmatch '^msime_v[0-9]+(?:\.[0-9]+){1,3}$') {
        throw 'Cannot infer the installed TSF version directory from the Metasequoia registry.'
    }
    $tsfVersionRoot = Join-Path (Join-Path $env:WINDIR 'System32\IME\MetasequoiaIME') $registry.VersionDir
    $tsfX64Path = Join-Path $tsfVersionRoot 'MetasequoiaImeTsf.dll'
    $tsfX86Root = Join-Path (Join-Path $env:WINDIR 'SysWOW64\IME\MetasequoiaIME') $registry.VersionDir
    $tsfX86Path = Join-Path $tsfX86Root 'MetasequoiaImeTsf.dll'
    $configPath = Join-Path $resolvedDataDirectory 'config.toml'
    $skinsDirectory = Join-Path $resolvedDataDirectory 'skins'
    $skinTargetPath = Join-Path $skinsDirectory $script:SkinId
    $backupRoot = Join-Path $resolvedDataDirectory 'kasumi-backups'

    foreach ($required in @($serverPath, $watchdogPath, $configPath, $tsfX64Path, $tsfX86Path)) {
        if (-not [IO.File]::Exists($required)) { throw "Required installed file is missing: $required" }
        Assert-NoReparsePoint -Path $required -Label "Installed file $required"
    }
    if ([IO.File]::Exists($skinTargetPath)) {
        throw "Installed skin path is a file, not a directory: $skinTargetPath"
    }
    if ([IO.Directory]::Exists($skinTargetPath)) {
        Assert-NoReparsePoint -Path $skinTargetPath -Label "Installed skin directory $skinTargetPath"
    }
    if ([IO.File]::Exists($backupRoot)) {
        throw "Backup root is a file, not a directory: $backupRoot"
    }
    if ([IO.Directory]::Exists($backupRoot)) {
        Assert-NoReparsePoint -Path $backupRoot -Label "Backup root $backupRoot"
    }

    $installedServerSha = Get-Sha256File -Path $serverPath
    $allowed = $false
    foreach ($hash in $script:AllowedInstalledServerHashes) {
        if (Test-HashEqual $installedServerSha $hash) { $allowed = $true; break }
    }
    if (-not $allowed) {
        throw "Refusing to overwrite an unknown installed Server image. SHA-256: $installedServerSha"
    }

    $configBytes = [IO.File]::ReadAllBytes($configPath)
    $configBeforeSha = Get-Sha256Bytes -Bytes $configBytes
    $configMerge = Merge-KasumiConfig -Bytes $configBytes -ConfigPath $configPath
    $configChanged = -not (Test-HashEqual $configBeforeSha $configMerge.Sha256)

    $skinChanges = New-Object 'System.Collections.Generic.List[object]'
    foreach ($entry in $manifest.Files.Values) {
        if (-not $entry.RelativePath.StartsWith("$($manifest.SkinRelative)/", [StringComparison]::OrdinalIgnoreCase)) { continue }
        $relativeSkin = $entry.RelativePath.Substring($manifest.SkinRelative.Length + 1)
        $destination = Join-Path $skinTargetPath $relativeSkin.Replace('/', '\')
        if (-not (Test-PathInside -ChildPath $destination -ParentPath $skinTargetPath)) {
            throw "Skin destination escapes the installed skin directory: $destination"
        }
        if ([IO.Directory]::Exists($destination)) {
            throw "Installed skin contains a directory where a file is required: $destination"
        }
        $changed = $true
        if ([IO.File]::Exists($destination)) {
            Assert-NoReparsePoint -Path $destination -Label "Installed skin file $destination"
            $changed = -not (Test-HashEqual (Get-Sha256File -Path $destination) $entry.Sha256)
        }
        if ($changed) {
            $skinChanges.Add([pscustomobject]@{
                RelativePath = $relativeSkin
                Destination = $destination
                Source = $entry.FullPath
                Sha256 = $entry.Sha256
            })
        }
    }

    $tsfChanges = New-Object 'System.Collections.Generic.List[object]'
    foreach ($spec in @(
            [pscustomobject]@{ RelativePath = 'native/MetasequoiaImeTsf-x64.dll'; Destination = $tsfX64Path; Architecture = 'x64' },
            [pscustomobject]@{ RelativePath = 'native/MetasequoiaImeTsf-x86.dll'; Destination = $tsfX86Path; Architecture = 'x86' }
        )) {
        $entry = $manifest.Files[$spec.RelativePath]
        if ($null -eq $entry) { throw "Manifest is missing the TSF payload: $($spec.RelativePath)" }
        $changed = -not (Test-HashEqual (Get-Sha256File -Path $spec.Destination) $entry.Sha256)
        if ($changed) {
            $tsfChanges.Add([pscustomobject]@{
                    RelativePath = $spec.RelativePath
                    Destination = $spec.Destination
                    Source = $entry.FullPath
                    Sha256 = $entry.Sha256
                    Architecture = $spec.Architecture
                    BeforeSha256 = Get-Sha256File -Path $spec.Destination
                })
        }
    }

    $serverChanged = -not (Test-HashEqual $installedServerSha $script:FinalServerSha256)
    $noop = (-not $serverChanged) -and (-not $configChanged) -and ($skinChanges.Count -eq 0) -and ($tsfChanges.Count -eq 0)

    return [pscustomobject]@{
        Manifest = $manifest
        DetectedImeVersion = $detectedImeVersion
        ServerDirectory = $resolvedServerDirectory
        DataDirectory = $resolvedDataDirectory
        ServerPath = $serverPath
        WatchdogPath = $watchdogPath
        TsfX64Path = $tsfX64Path
        TsfX86Path = $tsfX86Path
        ConfigPath = $configPath
        SkinTargetPath = $skinTargetPath
        SkinsDirectory = $skinsDirectory
        BackupRoot = $backupRoot
        InstalledServerSha256 = $installedServerSha
        ConfigBytes = $configBytes
        ConfigBeforeSha256 = $configBeforeSha
        ConfigAfterBytes = $configMerge.Bytes
        ConfigAfterSha256 = $configMerge.Sha256
        ConfigChanged = $configChanged
        ConfigChangedKeys = @($configMerge.ChangedKeys)
        ServerChanged = $serverChanged
        SkinChanges = @($skinChanges.ToArray())
        TsfChanges = @($tsfChanges.ToArray())
        Noop = $noop
    }
}

function Get-ServerProcesses {
    param([Parameter(Mandatory = $true)][string]$ServerPath)
    $result = New-Object 'System.Collections.Generic.List[object]'
    foreach ($process in @(Get-Process -Name 'MetasequoiaImeServer' -ErrorAction SilentlyContinue)) {
        $path = $null
        try { $path = $process.Path } catch { $path = $null }
        if (-not [string]::IsNullOrWhiteSpace($path) -and
            [string]::Equals($path, $ServerPath, [StringComparison]::OrdinalIgnoreCase)) {
            $result.Add($process)
        }
    }
    return @($result.ToArray())
}

function Get-WatchdogProcesses {
    param([Parameter(Mandatory = $true)][string]$WatchdogPath)
    $result = New-Object 'System.Collections.Generic.List[object]'
    foreach ($process in @(Get-Process -Name 'MetasequoiaImeWatchdog' -ErrorAction SilentlyContinue)) {
        $path = $null
        try { $path = $process.Path } catch { $path = $null }
        if (-not [string]::IsNullOrWhiteSpace($path) -and
            [string]::Equals($path, $WatchdogPath, [StringComparison]::OrdinalIgnoreCase)) {
            $result.Add($process)
        }
    }
    return @($result.ToArray())
}

function Assert-WatchdogRunning {
    param($State)
    $watchdogs = @(Get-WatchdogProcesses -WatchdogPath $State.WatchdogPath)
    if ($watchdogs.Count -ne 1) {
        throw "Expected exactly one official Watchdog process at '$($State.WatchdogPath)'; found $($watchdogs.Count)."
    }
}

function Initialize-PipeOwnerType {
    if ('KasumiPackagePipeOwnerV1' -as [type]) { return }
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
public static class KasumiPackagePipeOwnerV1 {
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool GetNamedPipeServerProcessId(SafePipeHandle pipe, out uint id);
}
'@
}

function Initialize-DelayedFileMoveType {
    if ('KasumiInstallDelayedFileMoveV1' -as [type]) { return }
    Add-Type -TypeDefinition @'
using System.Runtime.InteropServices;
public static class KasumiInstallDelayedFileMoveV1 {
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern bool MoveFileEx(string existingFileName, string newFileName, int flags);
}
'@
}

function Get-AuxPipeOwner {
    $pipe = [IO.Pipes.NamedPipeClientStream]::new('.', $script:AuxPipeName, [IO.Pipes.PipeDirection]::Out)
    try {
        $pipe.Connect(2000)
        [uint32]$owner = 0
        if (-not [KasumiPackagePipeOwnerV1]::GetNamedPipeServerProcessId($pipe.SafePipeHandle, [ref]$owner)) {
            throw 'GetNamedPipeServerProcessId failed.'
        }
        return [int]$owner
    }
    finally { $pipe.Dispose() }
}

function Request-AuxRestart {
    $pipe = [IO.Pipes.NamedPipeClientStream]::new('.', $script:AuxPipeName, [IO.Pipes.PipeDirection]::Out)
    try {
        $pipe.Connect(3000)
        $bytes = [Text.Encoding]::Unicode.GetBytes('RestartServer')
        $pipe.Write($bytes, 0, $bytes.Length)
        $pipe.Flush()
    }
    finally { $pipe.Dispose() }
}

function Get-ValidatedLegacyOverlays {
    param($State)
    $processes = @(Get-Process -Name 'KasumiBandOverlay' -ErrorAction SilentlyContinue)
    if ($processes.Count -eq 0) { return @() }

    $cimByPid = @{}
    try {
        foreach ($item in @(Get-CimInstance -ClassName Win32_Process -Filter "Name = 'KasumiBandOverlay.exe'" -ErrorAction Stop)) {
            $cimByPid[[int]$item.ProcessId] = $item
        }
    }
    catch {
        throw "KasumiBandOverlay is running but its command line cannot be inspected; no stop event was sent: $($_.Exception.Message)"
    }

    $result = New-Object 'System.Collections.Generic.List[object]'
    foreach ($process in $processes) {
        $processPath = $null
        try { $processPath = $process.Path } catch { $processPath = $null }
        if ([string]::IsNullOrWhiteSpace($processPath)) {
            throw "KasumiBandOverlay PID $($process.Id) has no readable image path; no stop event was sent."
        }
        if (-not [string]::Equals([IO.Path]::GetFileName($processPath), 'KasumiBandOverlay.exe', [StringComparison]::OrdinalIgnoreCase)) {
            throw "Unexpected overlay image for PID $($process.Id): $processPath"
        }
        if (-not $cimByPid.ContainsKey([int]$process.Id)) {
            throw "KasumiBandOverlay PID $($process.Id) has no readable command line; no stop event was sent."
        }
        $commandLine = [string]$cimByPid[[int]$process.Id].CommandLine
        $match = [regex]::Match($commandLine, '(?i)(?:^|\s)--skin-dir\s+(?:"(?<quoted>[^"]+)"|(?<plain>\S+))')
        if (-not $match.Success) {
            throw "KasumiBandOverlay PID $($process.Id) has no --skin-dir; no stop event was sent."
        }
        $skinArgument = if ($match.Groups['quoted'].Success) { $match.Groups['quoted'].Value } else { $match.Groups['plain'].Value }
        $overlaySkin = Remove-TrailingSeparator -Path ([IO.Path]::GetFullPath($skinArgument))
        if (-not [string]::Equals($overlaySkin, $State.SkinTargetPath, [StringComparison]::OrdinalIgnoreCase)) {
            throw "KasumiBandOverlay PID $($process.Id) targets '$overlaySkin', not '$($State.SkinTargetPath)'; no stop event was sent."
        }
        $result.Add([pscustomobject]@{ Process = $process; ImagePath = $processPath; SkinDirectory = $overlaySkin })
    }
    return @($result.ToArray())
}

function Stop-ValidatedLegacyOverlays {
    param($State)
    $overlays = @(Get-ValidatedLegacyOverlays -State $State)
    if ($overlays.Count -eq 0) { return 0 }

    Write-Step "Asking $($overlays.Count) old KasumiBandOverlay process(es) to exit through their normal stop event..."
    $signal = $null
    try {
        $signal = [Threading.EventWaitHandle]::OpenExisting($script:OverlayStopEventName)
        if (-not $signal.Set()) { throw 'The overlay stop event was not accepted.' }
    }
    finally {
        if ($null -ne $signal) { $signal.Dispose() }
    }
    foreach ($overlay in $overlays) {
        if (-not $overlay.Process.WaitForExit(10000)) {
            throw "KasumiBandOverlay PID $($overlay.Process.Id) did not exit after its normal stop event; no force-kill was attempted."
        }
    }
    return $overlays.Count
}

function Assert-LiveInstallState {
    param($State)
    $serverProcesses = @(Get-ServerProcesses -ServerPath $State.ServerPath)
    if ($serverProcesses.Count -ne 1) {
        throw "Expected exactly one running Server at '$($State.ServerPath)' before install; found $($serverProcesses.Count)."
    }
    Assert-WatchdogRunning -State $State
    Initialize-PipeOwnerType
    $owner = Get-AuxPipeOwner
    if ($owner -ne $serverProcesses[0].Id) {
        throw "The Aux pipe is not owned by the expected Server PID $($serverProcesses[0].Id) (owner PID: $owner)."
    }
    $overlays = @(Get-ValidatedLegacyOverlays -State $State)
    return [pscustomobject]@{
        ServerProcess = $serverProcesses[0]
        OverlayCount = $overlays.Count
    }
}

function Write-JsonAtomic {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)]$Object
    )
    $json = ($Object | ConvertTo-Json -Depth 12) + [Environment]::NewLine
    $temp = $Path + '.tmp-' + [Guid]::NewGuid().ToString('N')
    try {
        [IO.File]::WriteAllText($temp, $json, $script:Utf8NoBom)
        if ([IO.File]::Exists($Path)) { [IO.File]::Replace($temp, $Path, $null) }
        else { [IO.File]::Move($temp, $Path) }
    }
    finally {
        if ([IO.File]::Exists($temp)) { [IO.File]::Delete($temp) }
    }
}

function Write-BytesAtomic {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][byte[]]$Bytes
    )
    $temp = $Path + '.kasumi-new-' + [Guid]::NewGuid().ToString('N')
    try {
        [IO.File]::WriteAllBytes($temp, $Bytes)
        if ([IO.File]::Exists($Path)) { [IO.File]::Replace($temp, $Path, $null) }
        else { [IO.File]::Move($temp, $Path) }
    }
    finally {
        if ([IO.File]::Exists($temp)) { [IO.File]::Delete($temp) }
    }
}

function Get-BackupRelativePath {
    param(
        [Parameter(Mandatory = $true)][string]$BackupDirectory,
        [Parameter(Mandatory = $true)][string]$Path
    )
    return Get-RelativePathUnder -BasePath $BackupDirectory -Path $Path
}

function Copy-BackupTree {
    param(
        [Parameter(Mandatory = $true)][string]$SourceDirectory,
        [Parameter(Mandatory = $true)][string]$DestinationDirectory,
        [Parameter(Mandatory = $true)][string]$BackupDirectory,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][Collections.Generic.List[object]]$BackupFiles
    )
    Assert-NoReparsePoint -Path $SourceDirectory -Label "Backup source $SourceDirectory"
    [void][IO.Directory]::CreateDirectory($DestinationDirectory)
    foreach ($entry in [IO.Directory]::EnumerateFileSystemEntries($SourceDirectory)) {
        Assert-NoReparsePoint -Path $entry -Label "Backup source $entry"
        $name = [IO.Path]::GetFileName($entry)
        $destination = Join-Path $DestinationDirectory $name
        if ([IO.Directory]::Exists($entry)) {
            Copy-BackupTree -SourceDirectory $entry -DestinationDirectory $destination `
                -BackupDirectory $BackupDirectory -BackupFiles $BackupFiles
        }
        elseif ([IO.File]::Exists($entry)) {
            [IO.File]::Copy($entry, $destination, $false)
            $sourceHash = Get-Sha256File -Path $entry
            $destinationHash = Get-Sha256File -Path $destination
            if (-not (Test-HashEqual $sourceHash $destinationHash)) {
                throw "Backup copy hash mismatch: $destination"
            }
            if (Test-PathInside -ChildPath $destination -ParentPath $BackupDirectory) {
                $BackupFiles.Add([pscustomobject]@{
                    relativePath = Get-BackupRelativePath -BackupDirectory $BackupDirectory -Path $destination
                    size = (Get-Item -LiteralPath $destination).Length
                    sha256 = $destinationHash
                })
            }
        }
    }
}

function New-InstallBackup {
    param($State)
    if ([IO.File]::Exists($State.BackupRoot)) {
        throw "Backup root is a file, not a directory: $($State.BackupRoot)"
    }
    [void][IO.Directory]::CreateDirectory($State.BackupRoot)
    $timestamp = (Get-Date).ToString('yyyyMMdd_HHmmss_fff', [Globalization.CultureInfo]::InvariantCulture)
    $backupDirectory = Join-Path $State.BackupRoot $timestamp
    $suffix = 1
    while ([IO.Directory]::Exists($backupDirectory) -or [IO.File]::Exists($backupDirectory)) {
        if ($suffix -gt 1000) { throw "Cannot allocate a unique backup directory: $($State.BackupRoot)" }
        $backupDirectory = Join-Path $State.BackupRoot ($timestamp + '_' + $suffix)
        $suffix++
    }
    [void][IO.Directory]::CreateDirectory($backupDirectory)

    $backupFiles = New-Object 'System.Collections.Generic.List[object]'
    $serverBackupPath = Join-Path (Join-Path $backupDirectory 'server') $script:ServerFileName
    [void][IO.Directory]::CreateDirectory((Split-Path -Parent $serverBackupPath))
    [IO.File]::Copy($State.ServerPath, $serverBackupPath, $false)
    $serverBackupHash = Get-Sha256File -Path $serverBackupPath
    if (-not (Test-HashEqual $serverBackupHash $State.InstalledServerSha256)) {
        throw 'Server backup hash does not match the audited installed image.'
    }
    $backupFiles.Add([pscustomobject]@{
        relativePath = Get-BackupRelativePath -BackupDirectory $backupDirectory -Path $serverBackupPath
        size = (Get-Item -LiteralPath $serverBackupPath).Length
        sha256 = $serverBackupHash
    })

    foreach ($tsf in @(
            [pscustomobject]@{ Source = $State.TsfX64Path; Relative = 'tsf/MetasequoiaImeTsf-x64.dll' },
            [pscustomobject]@{ Source = $State.TsfX86Path; Relative = 'tsf/MetasequoiaImeTsf-x86.dll' }
        )) {
        $destination = Join-Path $backupDirectory $tsf.Relative.Replace('/', '\')
        [void][IO.Directory]::CreateDirectory((Split-Path -Parent $destination))
        [IO.File]::Copy($tsf.Source, $destination, $false)
        $sourceHash = Get-Sha256File -Path $tsf.Source
        $backupHash = Get-Sha256File -Path $destination
        if (-not (Test-HashEqual $sourceHash $backupHash)) {
            throw "TSF backup hash mismatch: $($tsf.Source)"
        }
        $backupFiles.Add([pscustomobject]@{
            relativePath = Get-BackupRelativePath -BackupDirectory $backupDirectory -Path $destination
            size = (Get-Item -LiteralPath $destination).Length
            sha256 = $backupHash
        })
    }

    $configBackupPath = Join-Path $backupDirectory 'config.toml.before'
    [IO.File]::WriteAllBytes($configBackupPath, $State.ConfigBytes)
    $configBackupHash = Get-Sha256File -Path $configBackupPath
    if (-not (Test-HashEqual $configBackupHash $State.ConfigBeforeSha256)) {
        throw 'config.toml backup hash does not match the audited original.'
    }
    $backupFiles.Add([pscustomobject]@{
        relativePath = Get-BackupRelativePath -BackupDirectory $backupDirectory -Path $configBackupPath
        size = (Get-Item -LiteralPath $configBackupPath).Length
        sha256 = $configBackupHash
    })

    $skinExisted = [IO.Directory]::Exists($State.SkinTargetPath)
    if ($skinExisted) {
        $skinBackupDirectory = Join-Path (Join-Path $backupDirectory 'skin') $script:SkinId
        Copy-BackupTree -SourceDirectory $State.SkinTargetPath -DestinationDirectory $skinBackupDirectory `
            -BackupDirectory $backupDirectory -BackupFiles $backupFiles
    }

    $timestampUtc = [DateTime]::UtcNow.ToString('o')
    $rollback = [ordered]@{
        schemaVersion = 1
        generator = 'Install.ps1'
        package = $script:PackageId
        packageVersion = $script:PackageVersion
        imeVersion = $script:ImeVersion
        createdUtc = $timestampUtc
        status = 'in-progress'
        dataDirectory = $State.DataDirectory
        serverDirectory = $State.ServerDirectory
        serverPath = $State.ServerPath
        serverBeforeSha256 = $State.InstalledServerSha256
        serverAfterSha256 = $script:FinalServerSha256
        serverBackupRelativePath = 'server/MetasequoiaImeServer.exe'
        serverWasReplaced = $false
        previousServerPath = $null
        tsfWasReplaced = $false
        tsfReplacementScheduled = $false
        tsfPaths = @($State.TsfX64Path, $State.TsfX86Path)
        tsfBackupRelativePaths = @('tsf/MetasequoiaImeTsf-x64.dll', 'tsf/MetasequoiaImeTsf-x86.dll')
        tsfChanges = @()
        skinPath = $State.SkinTargetPath
        skinExisted = $skinExisted
        skinBackupRelativeDirectory = $(if ($skinExisted) { "skin/$($script:SkinId)" } else { $null })
        configPath = $State.ConfigPath
        configBeforeSha256 = $State.ConfigBeforeSha256
        configAfterSha256 = $State.ConfigAfterSha256
        configBackupRelativePath = 'config.toml.before'
        changedKeys = @($State.ConfigChangedKeys)
        backupFiles = @($backupFiles.ToArray())
        completedUtc = $null
        failureReason = $null
    }
    $rollbackPath = Join-Path $backupDirectory 'rollback.json'
    Write-JsonAtomic -Path $rollbackPath -Object $rollback
    return [pscustomobject]@{
        Directory = $backupDirectory
        RollbackPath = $rollbackPath
        Rollback = $rollback
        Files = $backupFiles
        SkinExisted = $skinExisted
        Timestamp = $timestamp
    }
}

function Copy-SkinChanges {
    param($State, $Transaction)
    if ($State.SkinChanges.Count -eq 0) { return 0 }
    $Transaction.SkinTouched = $true
    $copied = 0
    foreach ($change in $State.SkinChanges) {
        $destination = $change.Destination
        $parent = Split-Path -Parent $destination
        [void][IO.Directory]::CreateDirectory($parent)
        Assert-NoReparsePoint -Path $parent -Label "Skin destination directory $parent"
        if ([IO.File]::Exists($destination)) {
            Assert-NoReparsePoint -Path $destination -Label "Skin destination file $destination"
            if (Test-HashEqual (Get-Sha256File -Path $destination) $change.Sha256) { continue }
        }
        $temp = $destination + '.kasumi-new-' + $Transaction.Timestamp
        try {
            [IO.File]::Copy($change.Source, $temp, $true)
            if (-not (Test-HashEqual (Get-Sha256File -Path $temp) $change.Sha256)) {
                throw "Staged skin asset hash mismatch: $($change.RelativePath)"
            }
            if ([IO.File]::Exists($destination)) { [IO.File]::Replace($temp, $destination, $null) }
            else { [IO.File]::Move($temp, $destination) }
            if (-not (Test-HashEqual (Get-Sha256File -Path $destination) $change.Sha256)) {
                throw "Installed skin asset hash mismatch: $($change.RelativePath)"
            }
            $copied++
        }
        finally {
            if ([IO.File]::Exists($temp)) { [IO.File]::Delete($temp) }
        }
    }
    return $copied
}

function Replace-InstalledServer {
    param($State, $Transaction)
    $currentHash = Get-Sha256File -Path $State.ServerPath
    if (-not (Test-HashEqual $currentHash $State.InstalledServerSha256)) {
        throw 'The installed Server changed after preflight; refusing to replace it.'
    }
    $previousName = $script:ServerFileName + '.previous-' + $Transaction.Timestamp
    $previousPath = Join-Path $State.ServerDirectory $previousName
    if ([IO.File]::Exists($previousPath) -or [IO.Directory]::Exists($previousPath)) {
        throw "Server rollback path already exists: $previousPath"
    }
    $Transaction.ServerTouched = $true
    Rename-Item -LiteralPath $State.ServerPath -NewName $previousName
    $Transaction.ServerReplaced = $true
    $Transaction.PreviousServerPath = $previousPath
    [IO.File]::Copy($State.Manifest.Files['native/MetasequoiaImeServer.exe'].FullPath, $State.ServerPath, $false)
    $newHash = Get-Sha256File -Path $State.ServerPath
    if (-not (Test-HashEqual $newHash $script:FinalServerSha256)) {
        throw 'Installed Server image hash does not match the release manifest.'
    }
}

function Stage-TsfChanges {
    param($State, $Transaction)
    $staged = New-Object 'System.Collections.Generic.List[object]'
    foreach ($change in $State.TsfChanges) {
        if (-not (Test-HashEqual (Get-Sha256File -Path $change.Destination) $change.BeforeSha256)) {
            throw "Installed $($change.Architecture) TSF changed after preflight; refusing to queue its replacement."
        }
        $stagePath = $change.Destination + '.kasumi-' + $Transaction.Timestamp + '.new'
        if ([IO.File]::Exists($stagePath) -or [IO.Directory]::Exists($stagePath)) {
            throw "TSF staging path already exists: $stagePath"
        }
        [IO.File]::Copy($change.Source, $stagePath, $false)
        if (-not (Test-HashEqual (Get-Sha256File -Path $stagePath) $change.Sha256)) {
            throw "Staged $($change.Architecture) TSF hash mismatch."
        }
        $staged.Add([pscustomobject]@{
                RelativePath = $change.RelativePath
                Architecture = $change.Architecture
                Destination = $change.Destination
                Source = $stagePath
                BeforeSha256 = $change.BeforeSha256
                AfterSha256 = $change.Sha256
                BackupRelativePath = if ($change.Architecture -eq 'x64') { 'tsf/MetasequoiaImeTsf-x64.dll' } else { 'tsf/MetasequoiaImeTsf-x86.dll' }
            })
        $Transaction.TsfStaged = @($staged.ToArray())
    }
    return $Transaction.TsfStaged
}

function Schedule-TsfChanges {
    param($Transaction)
    if ($Transaction.TsfStaged.Count -eq 0) { return @() }
    Initialize-DelayedFileMoveType
    $scheduled = New-Object 'System.Collections.Generic.List[object]'
    foreach ($change in $Transaction.TsfStaged) {
        if (-not (Test-HashEqual (Get-Sha256File -Path $change.Destination) $change.BeforeSha256)) {
            throw "Installed $($change.Architecture) TSF changed before scheduling; refusing to continue."
        }
        # TSF DLLs are commonly loaded by Explorer and other open applications.
        # Queue an atomic same-volume replacement for the next Windows start.
        $flags = 0x1 -bor 0x4
        if (-not [KasumiInstallDelayedFileMoveV1]::MoveFileEx($change.Source, $change.Destination, $flags)) {
            $code = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
            throw "Could not queue the $($change.Architecture) TSF replacement (Win32 $code)."
        }
        $Transaction.TsfScheduledCount++
        $scheduled.Add([pscustomobject]@{
                architecture = $change.Architecture
                path = $change.Destination
                stagePath = $change.Source
                beforeSha256 = $change.BeforeSha256
                afterSha256 = $change.AfterSha256
                backupRelativePath = $change.BackupRelativePath
            })
    }
    return @($scheduled.ToArray())
}

function Remove-UnscheduledTsfStages {
    param($Transaction)
    $items = @($Transaction.TsfStaged)
    for ($index = [int]$Transaction.TsfScheduledCount; $index -lt $items.Count; $index++) {
        $change = $items[$index]
        if ($change.Source -and [IO.File]::Exists($change.Source)) {
            try { [IO.File]::Delete($change.Source) } catch { }
        }
    }
}

function Wait-ForRestartedServer {
    param(
        [Parameter(Mandatory = $true)][string]$ServerPath,
        [int[]]$ExcludeProcessIds = @()
    )
    $deadline = [DateTime]::UtcNow.AddSeconds(45)
    do {
        $candidates = @(Get-ServerProcesses -ServerPath $ServerPath | Where-Object { $_.Id -notin $ExcludeProcessIds })
        if ($candidates.Count -gt 1) {
            throw "More than one Server appeared at '$ServerPath' after restart."
        }
        if ($candidates.Count -eq 1) {
            if (-not $candidates[0].WaitForExit(2000)) { return $candidates[0] }
        }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $deadline)
    return $null
}

function Confirm-ServerHealth {
    param(
        [Parameter(Mandatory = $true)][int]$ServerPid,
        [Parameter(Mandatory = $true)][string]$ServerPath,
        [Parameter(Mandatory = $true)][datetime]$Since,
        [int]$Seconds = 30
    )
    Initialize-PipeOwnerType
    $deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
    $checks = 0
    $pipeChecks = 0
    $lastPipeError = ''
    do {
        $process = Get-Process -Id $ServerPid -ErrorAction Stop
        try {
            if ($process.HasExited) { throw "Expected Server PID $ServerPid exited during health check." }
            $processPath = $process.Path
            if (-not [string]::Equals($processPath, $ServerPath, [StringComparison]::OrdinalIgnoreCase)) {
                throw "Server PID $ServerPid now runs from '$processPath'."
            }
        }
        catch { throw "Expected Server PID $ServerPid is no longer healthy: $($_.Exception.Message)" }

        $eventErrors = @()
        $events = @(Get-WinEvent -FilterHashtable @{ LogName = 'Application'; ProviderName = 'Application Error'; StartTime = $Since } `
                -ErrorAction SilentlyContinue -ErrorVariable eventErrors)
        $realErrors = @($eventErrors | Where-Object { $_.FullyQualifiedErrorId -notlike 'NoMatchingEventsFound*' })
        if ($realErrors.Count -gt 0) {
            throw "Could not read the native crash-event health signal: $($realErrors[0].Exception.Message)"
        }
        foreach ($event in $events) {
            $fields = @{}
            foreach ($entry in ([xml]$event.ToXml()).Event.EventData.Data) {
                if ($entry.Name) { $fields[[string]$entry.Name] = [string]$entry.InnerText }
            }
            if ($fields.ContainsKey('AppName') -and $fields['AppName'] -ieq $script:ServerFileName) {
                throw "A new native crash event was recorded after deployment (PID $($fields['ProcessId']), exception $($fields['ExceptionCode']))."
            }
        }

        $owner = $null
        try { $owner = Get-AuxPipeOwner }
        catch { $lastPipeError = $_.Exception.Message }
        if ($null -ne $owner) {
            if ($owner -ne $ServerPid) {
                throw "Aux pipe owner is PID $owner, not expected PID $ServerPid."
            }
            $pipeChecks++
        }
        $checks++
        if ([DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 2000 }
    } while ([DateTime]::UtcNow -lt $deadline)

    if ($pipeChecks -lt 3) {
        throw "The Aux endpoint was not confirmed stable during the 30-second health window. Last error: $lastPipeError"
    }
    return [ordered]@{
        serverPid = $ServerPid
        observedSeconds = $Seconds
        processChecks = $checks
        endpointOwnerChecks = $pipeChecks
        crashEvents = 0
        healthy = $true
    }
}

function Invoke-RestartAndHealth {
    param(
        [Parameter(Mandatory = $true)][string]$ServerPath,
        [Parameter(Mandatory = $true)][string]$WatchdogPath,
        $OldProcess,
        [switch]$AllowNoOldProcess
    )
    Assert-WatchdogRunning -State ([pscustomobject]@{ WatchdogPath = $WatchdogPath })
    $since = Get-Date
    $exclude = @()
    if ($null -ne $OldProcess) {
        Request-AuxRestart
        if (-not $OldProcess.WaitForExit(15000)) {
            throw "Server PID $($OldProcess.Id) did not accept the normal restart request."
        }
        $exclude = @([int]$OldProcess.Id)
    }
    elseif (-not $AllowNoOldProcess) {
        throw 'A running Server is required to request a restart.'
    }

    $newServer = Wait-ForRestartedServer -ServerPath $ServerPath -ExcludeProcessIds $exclude
    if ($null -eq $newServer) {
        throw 'The official Watchdog did not start a stable new Server within 45 seconds.'
    }
    $health = Confirm-ServerHealth -ServerPid $newServer.Id -ServerPath $ServerPath -Since $since -Seconds 30
    return [pscustomobject]@{
        PreviousServerPid = if ($null -ne $OldProcess) { $OldProcess.Id } else { $null }
        ServerPid = $newServer.Id
        Health = $health
        RestartUtc = $since.ToUniversalTime().ToString('o')
    }
}

function Get-BackupFileMap {
    param($Rollback, [string]$BackupDirectory)
    $map = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in @($Rollback.backupFiles)) {
        $relative = [string]$entry.relativePath
        if ([string]::IsNullOrWhiteSpace($relative)) { throw 'rollback.json contains an empty backup file path.' }
        $full = [IO.Path]::GetFullPath((Join-Path $BackupDirectory $relative.Replace('/', '\')))
        if (-not (Test-PathInside -ChildPath $full -ParentPath $BackupDirectory)) {
            throw "Backup file escapes the backup directory: $relative"
        }
        if ($map.ContainsKey($relative)) { throw "rollback.json contains duplicate backup file: $relative" }
        if (-not [IO.File]::Exists($full)) { throw "Rollback artifact is missing: $full" }
        Assert-NoReparsePoint -Path $full -Label "Rollback artifact $full"
        $hash = Get-Sha256File -Path $full
        if (-not (Test-HashEqual $hash ([string]$entry.sha256))) {
            throw "Rollback artifact hash mismatch: $full"
        }
        $map.Add($relative, $full)
    }
    return $map
}

function Restore-TreeFromBackup {
    param(
        [Parameter(Mandatory = $true)][string]$BackupDirectory,
        [Parameter(Mandatory = $true)][string]$BackupRelativeDirectory,
        [Parameter(Mandatory = $true)][string]$DestinationDirectory
    )
    $source = [IO.Path]::GetFullPath((Join-Path $BackupDirectory $BackupRelativeDirectory.Replace('/', '\')))
    if (-not (Test-PathInside -ChildPath $source -ParentPath $BackupDirectory)) {
        throw "Backup skin directory escapes the backup directory: $BackupRelativeDirectory"
    }
    if (-not [IO.Directory]::Exists($source)) { throw "Backup skin directory is missing: $source" }
    Assert-NoReparsePoint -Path $source -Label "Backup skin directory $source"
    Copy-BackupTree -SourceDirectory $source -DestinationDirectory $DestinationDirectory `
        -BackupDirectory $BackupDirectory -BackupFiles (New-Object 'System.Collections.Generic.List[object]')
}

function Restore-SkinForRollback {
    param($State, $Transaction, $Backup)
    $tag = 'install-failed-' + (Get-Date).ToString('yyyyMMdd_HHmmss_fff', [Globalization.CultureInfo]::InvariantCulture)
    $aside = Join-Path $State.SkinsDirectory ($script:SkinId + '.' + $tag)
    if ([IO.Directory]::Exists($State.SkinTargetPath)) {
        if ([IO.Directory]::Exists($aside) -or [IO.File]::Exists($aside)) { throw "Rollback aside path already exists: $aside" }
        Rename-Item -LiteralPath $State.SkinTargetPath -NewName ($script:SkinId + '.' + $tag)
        $Transaction.FailedSkinPath = $aside
    }
    if ($Backup.SkinExisted) {
        [void][IO.Directory]::CreateDirectory($State.SkinTargetPath)
        Restore-TreeFromBackup -BackupDirectory $Backup.Directory `
            -BackupRelativeDirectory $Backup.Rollback.skinBackupRelativeDirectory `
            -DestinationDirectory $State.SkinTargetPath
    }
}

function Restore-ConfigForRollback {
    param($State, $Transaction, $Backup)
    $backupPath = Join-Path $Backup.Directory 'config.toml.before'
    $expected = $State.ConfigBeforeSha256
    $actual = Get-Sha256File -Path $backupPath
    if (-not (Test-HashEqual $actual $expected)) { throw 'Config backup hash mismatch during rollback.' }
    if ([IO.File]::Exists($State.ConfigPath)) {
        if (Test-HashEqual (Get-Sha256File -Path $State.ConfigPath) $expected) { return }
        $aside = $State.ConfigPath + '.install-failed-' + $Transaction.Timestamp
        if ([IO.File]::Exists($aside) -or [IO.Directory]::Exists($aside)) { throw "Config rollback aside path already exists: $aside" }
        Rename-Item -LiteralPath $State.ConfigPath -NewName ([IO.Path]::GetFileName($aside))
        $Transaction.FailedConfigPath = $aside
    }
    [IO.File]::Copy($backupPath, $State.ConfigPath, $false)
    if (-not (Test-HashEqual (Get-Sha256File -Path $State.ConfigPath) $expected)) {
        throw 'Config restore hash mismatch during rollback.'
    }
}

function Restore-ServerForRollback {
    param($State, $Transaction, $Backup)
    $backupPath = Join-Path $Backup.Directory 'server\MetasequoiaImeServer.exe'
    if (-not (Test-HashEqual (Get-Sha256File -Path $backupPath) $State.InstalledServerSha256)) {
        throw 'Server backup hash mismatch during rollback.'
    }
    if ([IO.File]::Exists($State.ServerPath) -and
        -not (Test-HashEqual (Get-Sha256File -Path $State.ServerPath) $State.InstalledServerSha256)) {
        $aside = $State.ServerPath + '.install-failed-' + $Transaction.Timestamp
        if ([IO.File]::Exists($aside) -or [IO.Directory]::Exists($aside)) { throw "Server rollback aside path already exists: $aside" }
        Rename-Item -LiteralPath $State.ServerPath -NewName ([IO.Path]::GetFileName($aside))
        $Transaction.FailedServerPath = $aside
    }
    if (-not [IO.File]::Exists($State.ServerPath)) {
        [IO.File]::Copy($backupPath, $State.ServerPath, $false)
    }
    if (-not (Test-HashEqual (Get-Sha256File -Path $State.ServerPath) $State.InstalledServerSha256)) {
        throw 'Server restore hash mismatch during rollback.'
    }
}

function Invoke-AutoRollback {
    param($State, $Transaction, $Backup, [string]$Reason)
    $errors = New-Object 'System.Collections.Generic.List[string]'
    $restartResult = $null
    Write-Step 'Install failed; restoring the matching original Server, skin and config...'
    if ($Transaction.SkinTouched) {
        try { Restore-SkinForRollback -State $State -Transaction $Transaction -Backup $Backup }
        catch { $errors.Add("Skin restore failed: $($_.Exception.Message)") }
    }
    if ($Transaction.ConfigTouched) {
        try { Restore-ConfigForRollback -State $State -Transaction $Transaction -Backup $Backup }
        catch { $errors.Add("Config restore failed: $($_.Exception.Message)") }
    }
    if ($Transaction.ServerReplaced) {
        try { Restore-ServerForRollback -State $State -Transaction $Transaction -Backup $Backup }
        catch { $errors.Add("Server restore failed: $($_.Exception.Message)") }
    }
    if ($Transaction.SkinTouched -or $Transaction.ConfigTouched -or $Transaction.ServerReplaced) {
        try {
            $processes = @(Get-ServerProcesses -ServerPath $State.ServerPath)
            $old = if ($processes.Count -eq 1) { $processes[0] } else { $null }
            if ($processes.Count -gt 1) { throw 'Multiple Server processes were found while restarting the rollback.' }
            $restartResult = Invoke-RestartAndHealth -ServerPath $State.ServerPath -WatchdogPath $State.WatchdogPath `
                -OldProcess $old -AllowNoOldProcess
        }
        catch { $errors.Add("Rollback restart failed: $($_.Exception.Message)") }
    }
    $rollback = $Backup.Rollback
    $rollback.status = if ($errors.Count -eq 0) { 'rolled-back' } else { 'rollback-failed' }
    $rollback.serverWasReplaced = $Transaction.ServerReplaced
    $rollback.previousServerPath = $Transaction.PreviousServerPath
    $rollback.failureReason = $Reason
    if ($null -ne $restartResult) { $rollback['rollbackRestart'] = $restartResult }
    Write-JsonAtomic -Path $Backup.RollbackPath -Object $rollback
    $failure = [ordered]@{
        status = 'failed'
        failedUtc = [DateTime]::UtcNow.ToString('o')
        reason = $Reason
        backup = $Backup.Directory
        autoRollbackAttempted = $true
        autoRollbackErrors = @($errors.ToArray())
        preservedSkinPath = $Transaction.FailedSkinPath
        preservedConfigPath = $Transaction.FailedConfigPath
        preservedServerPath = $Transaction.FailedServerPath
    }
    Write-JsonAtomic -Path (Join-Path $Backup.Directory 'failure.json') -Object $failure

    $message = "Install failed: $Reason. Backup: $($Backup.Directory)."
    if ($errors.Count -gt 0) {
        $message += ' Automatic recovery reported: ' + ($errors -join ' | ')
    }
    else {
        $message += ' The original Server, skin and config were restored and the IME was restarted.'
    }
    throw $message
}

function Invoke-InstallTransaction {
    param($State, $Live)
    $backup = New-InstallBackup -State $State
    $transaction = [pscustomobject]@{
        Timestamp = $backup.Timestamp
        BackupDirectory = $backup.Directory
        ServerTouched = $false
        ServerReplaced = $false
        SkinTouched = $false
        ConfigTouched = $false
        PreviousServerPath = $null
        FailedSkinPath = $null
        FailedConfigPath = $null
        FailedServerPath = $null
        TsfStaged = @()
        TsfScheduledCount = 0
    }
    try {
        [void](Get-BackupFileMap -Rollback $backup.Rollback -BackupDirectory $backup.Directory)
        # Recheck the exact bytes that were backed up before any target is changed.
        if (-not (Test-HashEqual (Get-Sha256File -Path $State.ConfigPath) $State.ConfigBeforeSha256) -or
            -not (Test-HashEqual (Get-Sha256File -Path $State.ServerPath) $State.InstalledServerSha256)) {
            throw 'The installed config or Server changed during backup; refusing to continue.'
        }

        Assert-WatchdogRunning -State $State
        $overlaysStopped = Stop-ValidatedLegacyOverlays -State $State
        [void](Stage-TsfChanges -State $State -Transaction $transaction)

        if ($State.SkinChanges.Count -gt 0) {
            Write-Step "Installing $($State.SkinChanges.Count) skin file(s)..."
            $copied = Copy-SkinChanges -State $State -Transaction $transaction
            Write-Step "Skin files installed: $copied."
        }
        if ($State.ConfigChanged) {
            Write-Step "Updating config.toml ($($State.ConfigChangedKeys -join ', '))..."
            $transaction.ConfigTouched = $true
            Write-BytesAtomic -Path $State.ConfigPath -Bytes $State.ConfigAfterBytes
            if (-not (Test-HashEqual (Get-Sha256File -Path $State.ConfigPath) $State.ConfigAfterSha256)) {
                throw 'config.toml hash did not match the planned result after writing.'
            }
        }
        else {
            Write-Step 'Config already contains the required Kasumi settings; leaving config.toml untouched.'
        }

        if ($State.ServerChanged) {
            Write-Step 'Renaming the loaded Server and installing the verified native image...'
            Replace-InstalledServer -State $State -Transaction $transaction
        }
        else {
            Write-Step 'Server image already matches the release manifest; keeping it.'
        }

        $backup.Rollback.serverWasReplaced = $transaction.ServerReplaced
        $backup.Rollback.previousServerPath = $transaction.PreviousServerPath
        $backup.Rollback.status = 'files-installed'
        $backup.Rollback.tsfReplacementScheduled = $false
        $backup.Rollback.tsfStagePaths = @($transaction.TsfStaged | ForEach-Object { $_.Source })
        $backup.Rollback.tsfChanges = @($transaction.TsfStaged | ForEach-Object {
                [pscustomobject]@{
                    architecture = $_.Architecture
                    path = $_.Destination
                    stagePath = $_.Source
                    beforeSha256 = $_.BeforeSha256
                    afterSha256 = $_.AfterSha256
                    backupRelativePath = $_.BackupRelativePath
                }
            })
        Write-JsonAtomic -Path $backup.RollbackPath -Object $backup.Rollback

        Write-Step 'Requesting the IME normal restart through the Aux pipe...'
        $oldProcess = $Live.ServerProcess
        $restart = Invoke-RestartAndHealth -ServerPath $State.ServerPath -WatchdogPath $State.WatchdogPath -OldProcess $oldProcess
        Write-Step "Server restarted as PID $($restart.ServerPid); 30-second health check passed."

        $scheduledTsfChanges = @(Schedule-TsfChanges -Transaction $transaction)
        if ($scheduledTsfChanges.Count -gt 0) {
            Write-Step 'The TSF updates are queued for the next Windows restart; the current input session remains usable.'
        }
        $backup.Rollback.tsfReplacementScheduled = ($scheduledTsfChanges.Count -gt 0)
        $backup.Rollback.tsfChanges = $scheduledTsfChanges

        $backup.Rollback.status = 'completed'
        $backup.Rollback.serverWasReplaced = $transaction.ServerReplaced
        $backup.Rollback.previousServerPath = $transaction.PreviousServerPath
        $backup.Rollback.tsfReplacementScheduled = ($scheduledTsfChanges.Count -gt 0)
        $backup.Rollback.tsfStagePaths = @($transaction.TsfStaged | ForEach-Object { $_.Source })
        $backup.Rollback.tsfChanges = $scheduledTsfChanges
        $backup.Rollback.completedUtc = [DateTime]::UtcNow.ToString('o')
        $backup.Rollback['health'] = $restart.Health
        Write-JsonAtomic -Path $backup.RollbackPath -Object $backup.Rollback

        $result = [ordered]@{
            status = 'installed'
            installed = $true
            restarted = $true
            packageVersion = $script:PackageVersion
            serverPid = $restart.ServerPid
            previousServerPid = $restart.PreviousServerPid
            backup = $backup.Directory
            rollback = $backup.RollbackPath
            health = $restart.Health
            serverReplaced = $transaction.ServerReplaced
            tsfReplacementScheduled = ($scheduledTsfChanges.Count -gt 0)
            rebootRequired = ($scheduledTsfChanges.Count -gt 0)
            tsfChanges = $scheduledTsfChanges
            configChangedKeys = @($State.ConfigChangedKeys)
            skinFilesCopied = $State.SkinChanges.Count
            legacyOverlaysStopped = $overlaysStopped
            helperStarted = $false
            completedUtc = [DateTime]::UtcNow.ToString('o')
        }
        Write-JsonAtomic -Path (Join-Path $backup.Directory 'install-result.json') -Object $result
        return [pscustomobject]$result
    }
    catch {
        $reason = $_.Exception.Message
        if ($transaction.TsfScheduledCount -gt 0) {
            foreach ($change in @($transaction.TsfStaged | Select-Object -First $transaction.TsfScheduledCount)) {
                $backupEntry = $backup.Rollback.backupFiles | Where-Object { $_.relativePath -eq $change.BackupRelativePath } | Select-Object -First 1
                if ($null -eq $backupEntry) { throw "Scheduled TSF update could not be rolled back because its backup is missing: $($change.BackupRelativePath)" }
                $backupPath = Join-Path $backup.Directory $backupEntry.relativePath.Replace('/', '\')
                [IO.File]::Copy($backupPath, $change.Source, $true)
                if (-not (Test-HashEqual (Get-Sha256File -Path $change.Source) $change.BeforeSha256)) {
                    throw "Scheduled TSF update could not be reset to its backup: $($change.Architecture)"
                }
            }
            $backup.Rollback.tsfReplacementScheduled = $true
            $backup.Rollback.tsfChanges = @($transaction.TsfStaged | Select-Object -First $transaction.TsfScheduledCount | ForEach-Object {
                    [pscustomobject]@{
                        architecture = $_.Architecture
                        path = $_.Destination
                        stagePath = $_.Source
                        beforeSha256 = $_.BeforeSha256
                        afterSha256 = $_.BeforeSha256
                        backupRelativePath = $_.BackupRelativePath
                    }
                })
            $reason += ' A queued TSF replacement was reset to its backed-up original for the next Windows restart.'
        }
        if ($transaction.SkinTouched -or $transaction.ConfigTouched -or $transaction.ServerReplaced) {
            Invoke-AutoRollback -State $State -Transaction $transaction -Backup $backup -Reason $reason
        }
        else {
            $rollback = $backup.Rollback
            $rollback.status = 'failed'
            $rollback.failureReason = $reason
            Write-JsonAtomic -Path $backup.RollbackPath -Object $rollback
            $failure = [ordered]@{
                status = 'failed'
                failedUtc = [DateTime]::UtcNow.ToString('o')
                reason = $reason
                backup = $backup.Directory
                autoRollbackAttempted = $false
                installedFilesChanged = $false
            }
            Write-JsonAtomic -Path (Join-Path $backup.Directory 'failure.json') -Object $failure
            throw
        }
    }
    finally {
        if ($transaction.TsfScheduledCount -eq 0) { Remove-UnscheduledTsfStages -Transaction $transaction }
    }
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
try {
    $state = Get-InstallPreflight -ServerDirectoryOverride $ServerDirectory -DataDirectoryOverride $DataDirectory

    if ($state.Noop) {
        $result = [ordered]@{
            status = 'noop'
            installed = $true
            changed = $false
            packageVersion = $script:PackageVersion
            serverDirectory = $state.ServerDirectory
            dataDirectory = $state.DataDirectory
            serverSha256 = $state.InstalledServerSha256
            reason = 'The installed Server, skin files and required config values already match this release.'
            helperStarted = $false
        }
        if ($WhatIfPreference) {
            $result.whatIf = $true
            $result.liveProcessChecksSkipped = $true
            Write-Step 'WhatIf: package files and required config values already match; live process checks were skipped.'
            [pscustomobject]$result | ConvertTo-Json -Depth 6
            return
        }
        # A conventional reinstall is a no-op, but a verified legacy overlay must not
        # keep painting over the native skin. This only uses its documented stop event.
        $legacyOverlays = @(Get-ValidatedLegacyOverlays -State $state)
        if ($legacyOverlays.Count -gt 0) {
            if (-not (Test-IsAdministrator)) {
                throw 'A verified legacy overlay is running. Administrator rights are required to stop it through its normal stop event.'
            }
            if (-not $PSCmdlet.ShouldProcess($state.SkinTargetPath, 'Ask the verified legacy KasumiBandOverlay process to exit through its normal stop event')) {
                $result.status = 'noop-overlay-kept'
                [pscustomobject]$result | ConvertTo-Json -Depth 6
                return
            }
            $result.legacyOverlaysStopped = Stop-ValidatedLegacyOverlays -State $state
        }
        else { $result.legacyOverlaysStopped = 0 }
        [pscustomobject]$result | ConvertTo-Json -Depth 6
        return
    }

    $plan = [ordered]@{
        status = 'plan'
        packageVersion = $script:PackageVersion
        serverDirectory = $state.ServerDirectory
        serverPath = $state.ServerPath
        dataDirectory = $state.DataDirectory
        installedServerSha256 = $state.InstalledServerSha256
        targetServerSha256 = $script:FinalServerSha256
        detectedImeVersion = $state.DetectedImeVersion
        serverWillBeReplaced = $state.ServerChanged
        configChangedKeys = @($state.ConfigChangedKeys)
        skinFilesToWrite = @($state.SkinChanges | ForEach-Object { $_.RelativePath })
        tsfFilesToWrite = @($state.TsfChanges | ForEach-Object { $_.RelativePath })
        tsfReplacementScheduled = [bool]($state.TsfChanges.Count -gt 0)
        rebootRequired = [bool]($state.TsfChanges.Count -gt 0)
        backupRoot = $state.BackupRoot
        helperStarted = $false
    }

    if ($WhatIfPreference) {
        $plan.status = 'planned'
        $plan.whatIf = $true
        $plan.liveProcessChecksSkipped = $true
        Write-Step 'WhatIf: no backup, file, process, registry or service change was made.'
        [pscustomobject]$plan | ConvertTo-Json -Depth 6
        return
    }

    if (-not (Test-IsAdministrator)) {
        throw 'Administrator rights are required for installation. Run Install.cmd (normal UAC prompt) or an elevated PowerShell session.'
    }

    $live = Assert-LiveInstallState -State $state
    $planStatusTarget = $state.ServerDirectory + ' | ' + $state.DataDirectory
    if (-not $PSCmdlet.ShouldProcess($planStatusTarget, 'Back up and install the Kasumi native skin package, then request a normal IME restart')) {
        $plan.status = 'cancelled'
        [pscustomobject]$plan | ConvertTo-Json -Depth 6
        return
    }

    $result = Invoke-InstallTransaction -State $state -Live $live
    [pscustomobject]$result | ConvertTo-Json -Depth 8
}
catch {
    Write-Host ('[kasumi] ERROR: ' + $_.Exception.Message) -ForegroundColor Red
    throw
}
