#Requires -Version 5.1
<#
.SYNOPSIS
    Manually roll back a Kasumi native-skin installation from a kasumi-backups entry.

.DESCRIPTION
    - Lists installations by reading rollback.json (the newest entry is clearly marked).
    - Validates every backup artifact against the SHA-256 values recorded at install time.
    - Renames the currently installed skin directory to
      <skin>.pre-restore-<timestamp> and copies the backed-up skin into place. New or
      user-added files are preserved in the renamed directory, not deleted.
    - Restores config.toml, the Server image and any TSF DLLs recorded by v0.5.7.
      Replaced current files are kept as
      *.pre-restore-<timestamp>; nothing is recursively deleted.
    - Requests the IME's normal restart through the Aux named pipe (UTF-16LE
      "RestartServer") and verifies the replacement for 30 seconds. No process is
      force-killed and no companion helper is started.
    - Supports -WhatIf. A real restore requires administrator rights.

.PARAMETER BackupPath
    A backup directory (containing rollback.json) or the rollback.json file itself.
    If omitted, available backups are listed for an explicit interactive choice.

.PARAMETER Latest
    Explicitly choose the newest valid backup without an interactive prompt.

.PARAMETER List
    Only list available backups; make no changes.

.PARAMETER DataDirectory
    Override the data directory to restore into. Defaults to the path recorded in
    rollback.json.

.PARAMETER ServerDirectory
    Override the Server directory to restore into. Defaults to the path recorded in
    rollback.json.

.EXAMPLE
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Restore.ps1 -List

.EXAMPLE
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Restore.ps1 -Latest -WhatIf

.EXAMPLE
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Restore.ps1 -BackupPath "$env:LOCALAPPDATA\metasequoiaime\kasumi-backups\20261021_170000_000"

.NOTES
    Run from an elevated PowerShell for a real restore. This script is standalone and
    uses paths relative to $PSScriptRoot only for its own location.
#>
[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'High')]
param(
    [string]$BackupPath,
    [switch]$Latest,
    [switch]$List,
    [string]$DataDirectory,
    [string]$ServerDirectory
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$script:SkinId = 'kasumi-adaptive'
$script:ImeVersion = '0.11.1'
$script:CompatibleImeVersions = @('0.11.1', '0.11.2')
$script:ServerFileName = 'MetasequoiaImeServer.exe'
$script:WatchdogFileName = 'MetasequoiaImeWatchdog.exe'
$script:AuxPipeName = 'FanyImeAuxNamedPipe'
$script:PackageId = 'kasumi-metasequoia-skin'
$script:FinalServerSha256 = '082A48E4A48A4FE7F723FDE8F62C97C4C5DC386EB8F38B6F38195336B397EA2F'
$script:PreviousV056ServerSha256 = '143A5433C46C1D22FFE79CA55AFAB4D688C013CE79969F7DB55AD79D5A6E4B3E'
$script:KnownLiveSDServerSha256 = 'CCDC1552B1690CF56101F92D390C9E37D8DD3E6B261B286F31CB7B4222EA5133'
$script:OfficialServerSha256 = '448264BABB98ECEB916094239A7613C3354BADE6E6897E2C7B9E6FC6606913D3'
$script:LegacyDevServerSha256 = 'C36003A3E57AD38C2FED41E0E00DDEAECF548D44D25997C4CED7D9C591317EBA'
$script:PreviousV053V054ServerSha256 = 'F29F375CB2127C4FFAEE0C18E3377D52CB0E433981E8E60248E0C26A1F95CA0C'
$script:PreviousV055ServerSha256 = '8843586A53876C6F626995014D8045DCB800329CBEB1F23A5DDC3DE9FF3FF5DC'
$script:AllowedServerHashes = @(
    $script:OfficialServerSha256,
    $script:LegacyDevServerSha256,
    $script:PreviousV053V054ServerSha256,
    $script:PreviousV055ServerSha256,
    $script:PreviousV056ServerSha256,
    $script:KnownLiveSDServerSha256,
    $script:FinalServerSha256
)
$script:Utf8NoBom = [Text.UTF8Encoding]::new($false, $false)

function Write-Step {
    param([Parameter(Mandatory = $true)][string]$Message)
    Write-Host ('[kasumi-restore] ' + $Message)
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
    if ($null -eq $Object) { throw "Field '$Name' is missing." }
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property -or $null -eq $property.Value) { throw "Field '$Name' is missing." }
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

function Get-DefaultDataRoot {
    param($RegistryInfo)
    if (-not [string]::IsNullOrWhiteSpace($DataDirectory)) {
        return Remove-TrailingSeparator -Path ([IO.Path]::GetFullPath($DataDirectory))
    }
    if (-not [string]::IsNullOrWhiteSpace($RegistryInfo.DataDir)) {
        return Remove-TrailingSeparator -Path ([IO.Path]::GetFullPath($RegistryInfo.DataDir))
    }
    if ([string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
        throw 'Cannot infer the Metasequoia data directory: %LOCALAPPDATA% is not set.'
    }
    return Remove-TrailingSeparator -Path ([IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'metasequoiaime')))
}

function Read-RollbackFile {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [switch]$Quiet
    )
    try {
        if (-not [IO.File]::Exists($Path)) { throw "rollback.json is missing: $Path" }
        $rollback = [IO.File]::ReadAllText($Path, [Text.Encoding]::UTF8) | ConvertFrom-Json
        if ([int](Get-RequiredPropertyValue -Object $rollback -Name 'schemaVersion') -ne 1) {
            throw 'Unsupported rollback.json schemaVersion.'
        }
        if ([string](Get-RequiredPropertyValue -Object $rollback -Name 'package') -ne $script:PackageId) {
            throw 'rollback.json belongs to a different package.'
        }
        return $rollback
    }
    catch {
        if ($Quiet) { return $null }
        throw
    }
}

function Get-BackupInventory {
    param([Parameter(Mandatory = $true)][string]$Root)
    $items = New-Object 'System.Collections.Generic.List[object]'
    if (-not [IO.Directory]::Exists($Root)) { return @() }
    foreach ($directory in @(Get-ChildItem -LiteralPath $Root -Directory -Force -ErrorAction Stop)) {
        $rollbackPath = Join-Path $directory.FullName 'rollback.json'
        if (-not [IO.File]::Exists($rollbackPath)) { continue }
        $rollback = $null
        $errorMessage = $null
        try { $rollback = Read-RollbackFile -Path $rollbackPath }
        catch { $errorMessage = $_.Exception.Message }
        $created = [DateTime]::MinValue
        if ($null -ne $rollback) {
            try { $created = [DateTime]::Parse([string]$rollback.createdUtc, [Globalization.CultureInfo]::InvariantCulture) }
            catch { $created = [DateTime]::MinValue }
        }
        $items.Add([pscustomobject]@{
            Directory = $directory.FullName
            RollbackPath = $rollbackPath
            Rollback = $rollback
            CreatedUtc = $created
            Error = $errorMessage
        })
    }
    return @($items | Sort-Object -Property @{ Expression = 'CreatedUtc'; Descending = $true }, @{ Expression = 'Directory'; Descending = $true })
}

function Show-BackupInventory {
    param([object[]]$Items)
    $valid = @($Items | Where-Object { $null -eq $_.Error })
    $latestDirectory = if ($valid.Count -gt 0) { $valid[0].Directory } else { $null }
    Write-Host 'Available Kasumi backups (newest first):'
    if ($Items.Count -eq 0) {
        Write-Host '  (none)'
        return
    }
    $index = 1
    foreach ($item in $Items) {
        $latest = if ($null -ne $latestDirectory -and
            [string]::Equals($item.Directory, $latestDirectory, [StringComparison]::OrdinalIgnoreCase)) { '  [latest]' } else { '' }
        if ($null -ne $item.Error) {
            Write-Host ("  [{0}] {1}  INVALID: {2}" -f $index, (Split-Path -Leaf $item.Directory), $item.Error)
        }
        else {
            $when = if ($item.CreatedUtc -eq [DateTime]::MinValue) { 'unknown time' } else { $item.CreatedUtc.ToLocalTime().ToString('yyyy-MM-dd HH:mm:ss') }
            $server = [string]$item.Rollback.serverBeforeSha256
            if ($server.Length -gt 12) { $server = $server.Substring(0, 12) }
            Write-Host ("  [{0}] {1}  {2}  status={3}  server={4}...{5}" -f
                $index, (Split-Path -Leaf $item.Directory), $when, $item.Rollback.status, $server, $latest)
        }
        $index++
    }
}

function Resolve-SelectedBackup {
    param([object[]]$Items)
    if (-not [string]::IsNullOrWhiteSpace($BackupPath) -and $Latest) {
        throw 'Use either -BackupPath or -Latest, not both.'
    }
    if (-not [string]::IsNullOrWhiteSpace($BackupPath)) {
        $candidate = Remove-TrailingSeparator -Path ([IO.Path]::GetFullPath($BackupPath))
        if ([IO.File]::Exists($candidate)) {
            if (-not [string]::Equals([IO.Path]::GetFileName($candidate), 'rollback.json', [StringComparison]::OrdinalIgnoreCase)) {
                throw '-BackupPath must point to a backup directory or rollback.json.'
            }
            $candidate = Split-Path -Parent $candidate
        }
        if (-not [IO.Directory]::Exists($candidate)) { throw "Backup directory does not exist: $candidate" }
        $rollbackPath = Join-Path $candidate 'rollback.json'
        $rollback = Read-RollbackFile -Path $rollbackPath
        return [pscustomobject]@{ Directory = $candidate; RollbackPath = $rollbackPath; Rollback = $rollback }
    }

    $valid = @($Items | Where-Object { $null -eq $_.Error })
    if ($valid.Count -eq 0) { throw 'No valid Kasumi backup was found.' }

    if ($Latest) {
        return [pscustomobject]@{ Directory = $valid[0].Directory; RollbackPath = $valid[0].RollbackPath; Rollback = $valid[0].Rollback }
    }

    Show-BackupInventory -Items $Items
    while ($true) {
        $choice = Read-Host 'Select a backup number (L = latest, Q = cancel)'
        if ([string]::IsNullOrWhiteSpace($choice)) { continue }
        if ($choice -ieq 'Q') { return $null }
        if ($choice -ieq 'L') {
            return [pscustomobject]@{ Directory = $valid[0].Directory; RollbackPath = $valid[0].RollbackPath; Rollback = $valid[0].Rollback }
        }
        $number = 0
        if ([int]::TryParse($choice, [ref]$number) -and $number -ge 1 -and $number -le $Items.Count) {
            $item = $Items[$number - 1]
            if ($null -ne $item.Error) { Write-Host "That entry is invalid: $($item.Error)" -ForegroundColor Yellow; continue }
            return [pscustomobject]@{ Directory = $item.Directory; RollbackPath = $item.RollbackPath; Rollback = $item.Rollback }
        }
        Write-Host 'Invalid choice.' -ForegroundColor Yellow
    }
}

function Get-BackupArtifactMap {
    param(
        [Parameter(Mandatory = $true)]$Rollback,
        [Parameter(Mandatory = $true)][string]$BackupDirectory
    )
    $map = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in @($Rollback.backupFiles)) {
        $relative = [string]$entry.relativePath
        if ([string]::IsNullOrWhiteSpace($relative)) { throw 'rollback.json contains an empty backup path.' }
        $normalized = $relative.Replace('\', '/')
        if ($normalized.StartsWith('/', [StringComparison]::Ordinal) -or $normalized.Contains(':') -or
            @($normalized.Split([char]'/') | Where-Object { $_ -eq '..' -or $_ -eq '.' -or [string]::IsNullOrWhiteSpace($_) }).Count -gt 0) {
            throw "Unsafe backup-relative path: $relative"
        }
        if ($map.ContainsKey($normalized)) { throw "Duplicate backup artifact: $normalized" }
        $full = [IO.Path]::GetFullPath((Join-Path $BackupDirectory $normalized.Replace('/', '\')))
        if (-not (Test-PathInside -ChildPath $full -ParentPath $BackupDirectory)) {
            throw "Backup artifact escapes the backup directory: $normalized"
        }
        if (-not [IO.File]::Exists($full)) { throw "Backup artifact is missing: $full" }
        Assert-NoReparsePoint -Path $full -Label "Backup artifact $full"
        $hash = Get-Sha256File -Path $full
        if (-not (Test-HashEqual $hash ([string]$entry.sha256))) {
            throw "Backup artifact hash mismatch: $full"
        }
        $map.Add($normalized, [pscustomobject]@{
            RelativePath = $normalized
            FullPath = $full
            Size = (Get-Item -LiteralPath $full).Length
            Sha256 = $hash
        })
    }
    return $map
}

function Get-UniqueSiblingPath {
    param(
        [Parameter(Mandatory = $true)][string]$Directory,
        [Parameter(Mandatory = $true)][string]$BaseName
    )
    $candidate = Join-Path $Directory $BaseName
    $suffix = 2
    while ([IO.File]::Exists($candidate) -or [IO.Directory]::Exists($candidate)) {
        if ($suffix -gt 1000) { throw "Cannot allocate a unique preserved path under: $Directory" }
        $candidate = Join-Path $Directory ($BaseName + '_' + $suffix)
        $suffix++
    }
    return $candidate
}

function Get-RestorePlan {
    param($Selected, $RegistryInfo)
    $detectedVersion = Get-DetectedImeVersion -RegistryInfo $RegistryInfo
    if ($detectedVersion -notin $script:CompatibleImeVersions) {
        $shown = if ([string]::IsNullOrWhiteSpace($detectedVersion)) { '<unknown>' } else { $detectedVersion }
        throw "This package supports Metasequoia IME $($script:CompatibleImeVersions -join ' or '); detected version: $shown"
    }

    $rollback = $Selected.Rollback
    if ([string]::IsNullOrWhiteSpace($rollback.status)) { throw 'rollback.json has no status.' }
    $serverBeforeSha = [string](Get-RequiredPropertyValue -Object $rollback -Name 'serverBeforeSha256')
    $configBeforeSha = [string](Get-RequiredPropertyValue -Object $rollback -Name 'configBeforeSha256')
    if (-not ($script:AllowedServerHashes | Where-Object { Test-HashEqual $_ $serverBeforeSha })) {
        throw "rollback.json records an unsupported Server hash: $serverBeforeSha"
    }

    if (-not [string]::IsNullOrWhiteSpace($DataDirectory)) {
        $targetDataDirectory = Remove-TrailingSeparator -Path ([IO.Path]::GetFullPath($DataDirectory))
    }
    else {
        $recordedDataDirectory = [string](Get-RequiredPropertyValue -Object $rollback -Name 'dataDirectory')
        if (-not [IO.Path]::IsPathRooted($recordedDataDirectory)) {
            throw "rollback.json has a non-absolute dataDirectory: $recordedDataDirectory"
        }
        $targetDataDirectory = Remove-TrailingSeparator -Path ([IO.Path]::GetFullPath($recordedDataDirectory))
    }
    if (-not [string]::IsNullOrWhiteSpace($ServerDirectory)) {
        $targetServerDirectory = Remove-TrailingSeparator -Path ([IO.Path]::GetFullPath($ServerDirectory))
    }
    else {
        $recordedServerDirectory = [string](Get-RequiredPropertyValue -Object $rollback -Name 'serverDirectory')
        if (-not [IO.Path]::IsPathRooted($recordedServerDirectory)) {
            throw "rollback.json has a non-absolute serverDirectory: $recordedServerDirectory"
        }
        $targetServerDirectory = Remove-TrailingSeparator -Path ([IO.Path]::GetFullPath($recordedServerDirectory))
    }
    if (-not [IO.Directory]::Exists($targetDataDirectory)) {
        throw "Target data directory does not exist: $targetDataDirectory"
    }
    if (-not [IO.Directory]::Exists($targetServerDirectory)) {
        throw "Target Server directory does not exist: $targetServerDirectory"
    }

    $targetConfig = Join-Path $targetDataDirectory 'config.toml'
    $targetSkins = Join-Path $targetDataDirectory 'skins'
    $targetSkin = Join-Path $targetSkins $script:SkinId
    $targetServer = Join-Path $targetServerDirectory $script:ServerFileName
    $targetWatchdog = Join-Path $targetServerDirectory $script:WatchdogFileName
    if (-not [IO.File]::Exists($targetWatchdog)) { throw "Official Watchdog is missing: $targetWatchdog" }
    Assert-NoReparsePoint -Path $targetWatchdog -Label 'Installed Watchdog'
    if ([IO.Directory]::Exists($targetConfig)) { throw "Target config path is a directory, not a file: $targetConfig" }
    if ([IO.Directory]::Exists($targetServer)) { throw "Target Server path is a directory, not a file: $targetServer" }
    if ([IO.File]::Exists($targetSkin)) { throw "Target skin path is a file, not a directory: $targetSkin" }
    if ([IO.Directory]::Exists($targetSkin)) { Assert-NoReparsePoint -Path $targetSkin -Label 'Target skin directory' }
    if ([IO.File]::Exists($targetConfig)) { Assert-NoReparsePoint -Path $targetConfig -Label 'Target config.toml' }
    if ([IO.File]::Exists($targetServer)) { Assert-NoReparsePoint -Path $targetServer -Label 'Target Server' }

    $artifacts = Get-BackupArtifactMap -Rollback $rollback -BackupDirectory $Selected.Directory
    $serverRelative = [string](Get-RequiredPropertyValue -Object $rollback -Name 'serverBackupRelativePath')
    $configRelative = [string](Get-RequiredPropertyValue -Object $rollback -Name 'configBackupRelativePath')
    if (-not $artifacts.ContainsKey($serverRelative) -or
        -not (Test-HashEqual $artifacts[$serverRelative].Sha256 $serverBeforeSha)) {
        throw 'Server backup artifact is missing or does not match serverBeforeSha256.'
    }
    if (-not $artifacts.ContainsKey($configRelative) -or
        -not (Test-HashEqual $artifacts[$configRelative].Sha256 $configBeforeSha)) {
        throw 'Config backup artifact is missing or does not match configBeforeSha256.'
    }

    $tsfRestoreChanges = New-Object 'System.Collections.Generic.List[object]'
    $tsfScheduledProperty = $rollback.PSObject.Properties['tsfReplacementScheduled']
    if ($null -ne $tsfScheduledProperty -and [bool]$tsfScheduledProperty.Value) {
        $versionDir = [string]$RegistryInfo.VersionDir
        if ([string]::IsNullOrWhiteSpace($versionDir) -or $versionDir -notmatch '^msime_v[0-9]+(?:\.[0-9]+){1,3}$') {
            throw 'Cannot infer the installed TSF version directory for rollback.'
        }
        $tsfX64Root = Join-Path (Join-Path $env:WINDIR 'System32\IME\MetasequoiaIME') $versionDir
        $tsfX86Root = Join-Path (Join-Path $env:WINDIR 'SysWOW64\IME\MetasequoiaIME') $versionDir
        foreach ($change in @($rollback.tsfChanges)) {
            $architecture = [string]$change.architecture
            $expectedPath = if ($architecture -eq 'x64') {
                Join-Path $tsfX64Root 'MetasequoiaImeTsf.dll'
            }
            elseif ($architecture -eq 'x86') {
                Join-Path $tsfX86Root 'MetasequoiaImeTsf.dll'
            }
            else { throw "rollback.json contains an unsupported TSF architecture: $architecture" }
            $recordedPath = [string]$change.path
            if (-not [string]::Equals([IO.Path]::GetFullPath($recordedPath), [IO.Path]::GetFullPath($expectedPath), [StringComparison]::OrdinalIgnoreCase)) {
                throw "rollback.json TSF path does not match the registered $architecture location."
            }
            $beforeSha = [string]$change.beforeSha256
            $afterSha = [string]$change.afterSha256
            if ($beforeSha -notmatch '^[a-fA-F0-9]{64}$' -or $afterSha -notmatch '^[a-fA-F0-9]{64}$') {
                throw "rollback.json has an invalid $architecture TSF hash."
            }
            $backupRelative = [string]$change.backupRelativePath
            $expectedBackup = if ($architecture -eq 'x64') { 'tsf/MetasequoiaImeTsf-x64.dll' } else { 'tsf/MetasequoiaImeTsf-x86.dll' }
            if ($backupRelative -ne $expectedBackup -or -not $artifacts.ContainsKey($backupRelative) -or
                -not (Test-HashEqual $artifacts[$backupRelative].Sha256 $beforeSha)) {
                throw "The $architecture TSF backup is missing or does not match its recorded hash."
            }
            if (Test-HashEqual $beforeSha $afterSha) { continue }
            if (-not [IO.File]::Exists($expectedPath)) { throw "Installed $architecture TSF is missing: $expectedPath" }
            Assert-NoReparsePoint -Path $expectedPath -Label "Installed $architecture TSF"
            $currentTsfSha = Get-Sha256File -Path $expectedPath
            $pendingStagePath = [string]$change.stagePath
            if ($pendingStagePath) {
                $pendingStagePath = [IO.Path]::GetFullPath($pendingStagePath)
                if (-not [string]::Equals([IO.Path]::GetDirectoryName($pendingStagePath), [IO.Path]::GetDirectoryName($expectedPath), [StringComparison]::OrdinalIgnoreCase) -or
                    -not $pendingStagePath.StartsWith($expectedPath + '.kasumi-', [StringComparison]::OrdinalIgnoreCase) -or
                    -not $pendingStagePath.EndsWith('.new', [StringComparison]::OrdinalIgnoreCase)) {
                    throw "rollback.json contains an unsafe $architecture TSF staging path."
                }
            }
            $pendingStageSha = $null
            if ($pendingStagePath -and [IO.File]::Exists($pendingStagePath)) {
                Assert-NoReparsePoint -Path $pendingStagePath -Label "Pending $architecture TSF"
                $pendingStageSha = Get-Sha256File -Path $pendingStagePath
                if (-not (Test-HashEqual $pendingStageSha $beforeSha) -and -not (Test-HashEqual $pendingStageSha $afterSha)) {
                    throw "Pending $architecture TSF does not match either recorded version."
                }
            }
            if (Test-HashEqual $currentTsfSha $beforeSha) {
                if ($pendingStageSha -and (Test-HashEqual $pendingStageSha $afterSha)) {
                    $tsfRestoreChanges.Add([pscustomobject]@{
                            Architecture = $architecture
                            Destination = $expectedPath
                            Source = $artifacts[$backupRelative].FullPath
                            BeforeSha256 = $currentTsfSha
                            RestoreSha256 = $beforeSha
                            PendingStagePath = $pendingStagePath
                        })
                }
                continue
            }
            if (-not (Test-HashEqual $currentTsfSha $afterSha)) {
                throw "The installed $architecture TSF is neither the recorded original nor release version; refusing to replace it."
            }
            $tsfRestoreChanges.Add([pscustomobject]@{
                    Architecture = $architecture
                    Destination = $expectedPath
                    Source = $artifacts[$backupRelative].FullPath
                    BeforeSha256 = $currentTsfSha
                    RestoreSha256 = $beforeSha
                    PendingStagePath = if ($pendingStageSha -and (Test-HashEqual $pendingStageSha $afterSha)) { $pendingStagePath } else { $null }
                })
        }
    }

    $skinExistedProperty = $rollback.PSObject.Properties['skinExisted']
    if ($null -eq $skinExistedProperty -or $skinExistedProperty.Value -isnot [bool]) {
        throw 'rollback.json has no valid boolean skinExisted field.'
    }
    $skinExisted = [bool]$skinExistedProperty.Value
    $skinBackupRelative = [string]$rollback.skinBackupRelativeDirectory
    $skinFiles = New-Object 'System.Collections.Generic.List[object]'
    if ($skinExisted) {
        if ($skinBackupRelative -ne "skin/$($script:SkinId)") {
            throw "rollback.json records an unexpected skin backup directory: $skinBackupRelative"
        }
        $prefix = $skinBackupRelative.TrimEnd('/') + '/'
        foreach ($artifact in $artifacts.Values) {
            if ($artifact.RelativePath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
                $skinFiles.Add([pscustomobject]@{
                    RelativePath = $artifact.RelativePath.Substring($prefix.Length)
                    FullPath = $artifact.FullPath
                    Sha256 = $artifact.Sha256
                    Size = $artifact.Size
                })
            }
        }
        if ($skinFiles.Count -eq 0) { throw 'The skin backup directory has no listed files.' }
    }

    $currentServerSha = if ([IO.File]::Exists($targetServer)) { Get-Sha256File -Path $targetServer } else { $null }
    if ($null -ne $currentServerSha -and -not ($script:AllowedServerHashes | Where-Object { Test-HashEqual $_ $currentServerSha })) {
        throw 'The current Server is an unknown custom build. Refusing to replace it during restore.'
    }
    $currentConfigSha = if ([IO.File]::Exists($targetConfig)) { Get-Sha256File -Path $targetConfig } else { $null }
    $stamp = (Get-Date).ToString('yyyyMMdd_HHmmss_fff', [Globalization.CultureInfo]::InvariantCulture)
    $skinPreservePath = if ([IO.Directory]::Exists($targetSkin)) {
        [IO.Path]::Combine($targetSkins, $script:SkinId + '.pre-restore-' + $stamp)
    } else { $null }
    $serverNeedsCopy = ($null -eq $currentServerSha -or -not (Test-HashEqual $currentServerSha $serverBeforeSha))

    return [pscustomobject]@{
        Selected = $Selected
        BackupDirectory = $Selected.Directory
        Rollback = $rollback
        Artifacts = $artifacts
        ServerBackup = $artifacts[$serverRelative]
        ConfigBackup = $artifacts[$configRelative]
        SkinFiles = @($skinFiles.ToArray())
        SkinExisted = $skinExisted
        SkinBackupRelative = $skinBackupRelative
        DataDirectory = $targetDataDirectory
        ServerDirectory = $targetServerDirectory
        ConfigPath = $targetConfig
        SkinDirectory = $targetSkin
        SkinsDirectory = $targetSkins
        ServerPath = $targetServer
        WatchdogPath = $targetWatchdog
        CurrentServerSha256 = $currentServerSha
        CurrentConfigSha256 = $currentConfigSha
        ServerNeedsCopy = $serverNeedsCopy
        ServerBeforeSha256 = $serverBeforeSha
        TsfRestoreChanges = @($tsfRestoreChanges.ToArray())
        ConfigBeforeSha256 = $configBeforeSha
        Stamp = $stamp
        SkinPreservePath = $skinPreservePath
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

function Initialize-PipeOwnerType {
    if ('KasumiRestorePipeOwnerV1' -as [type]) { return }
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
public static class KasumiRestorePipeOwnerV1 {
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool GetNamedPipeServerProcessId(SafePipeHandle pipe, out uint id);
}

function Initialize-DelayedFileMoveType {
    if ('KasumiRestoreDelayedFileMoveV1' -as [type]) { return }
    Add-Type -TypeDefinition @'
using System.Runtime.InteropServices;
public static class KasumiRestoreDelayedFileMoveV1 {
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
        if (-not [KasumiRestorePipeOwnerV1]::GetNamedPipeServerProcessId($pipe.SafePipeHandle, [ref]$owner)) {
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

function Wait-ForNewServer {
    param(
        [Parameter(Mandatory = $true)][string]$ServerPath,
        [int[]]$ExcludeProcessIds = @()
    )
    $deadline = [DateTime]::UtcNow.AddSeconds(45)
    do {
        $candidates = @(Get-ServerProcesses -ServerPath $ServerPath | Where-Object { $_.Id -notin $ExcludeProcessIds })
        if ($candidates.Count -gt 1) { throw "More than one Server appeared at '$ServerPath' after restart." }
        if ($candidates.Count -eq 1 -and -not $candidates[0].WaitForExit(2000)) { return $candidates[0] }
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
                throw "A new native crash event was recorded after restore (PID $($fields['ProcessId']), exception $($fields['ExceptionCode']))."
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

function Assert-RestoreLiveState {
    param($Plan)
    $watchdogs = @(Get-WatchdogProcesses -WatchdogPath $Plan.WatchdogPath)
    if ($watchdogs.Count -ne 1) {
        throw "Expected exactly one official Watchdog process at '$($Plan.WatchdogPath)'; found $($watchdogs.Count)."
    }
    $servers = @(Get-ServerProcesses -ServerPath $Plan.ServerPath)
    if ($servers.Count -gt 1) {
        throw "Expected at most one running Server at '$($Plan.ServerPath)'; found $($servers.Count)."
    }
    if ($servers.Count -eq 1) {
        Initialize-PipeOwnerType
        $owner = Get-AuxPipeOwner
        if ($owner -ne $servers[0].Id) {
            throw "The Aux pipe is not owned by the expected Server PID $($servers[0].Id) (owner PID: $owner)."
        }
        return $servers[0]
    }
    return $null
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
    $temp = $Path + '.kasumi-restore-' + [Guid]::NewGuid().ToString('N')
    try {
        [IO.File]::WriteAllBytes($temp, $Bytes)
        if ([IO.File]::Exists($Path)) { [IO.File]::Replace($temp, $Path, $null) }
        else { [IO.File]::Move($temp, $Path) }
    }
    finally {
        if ([IO.File]::Exists($temp)) { [IO.File]::Delete($temp) }
    }
}

function Copy-FileAtomicVerified {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Destination,
        [Parameter(Mandatory = $true)][string]$ExpectedSha256
    )
    [void][IO.Directory]::CreateDirectory((Split-Path -Parent $Destination))
    if ([IO.File]::Exists($Destination)) {
        Assert-NoReparsePoint -Path $Destination -Label "Restore destination $Destination"
    }
    $temp = $Destination + '.kasumi-restore-new-' + [Guid]::NewGuid().ToString('N')
    try {
        [IO.File]::Copy($Source, $temp, $true)
        if (-not (Test-HashEqual (Get-Sha256File -Path $temp) $ExpectedSha256)) {
            throw "Staged restore file hash mismatch: $Destination"
        }
        if ([IO.File]::Exists($Destination)) { [IO.File]::Replace($temp, $Destination, $null) }
        else { [IO.File]::Move($temp, $Destination) }
        if (-not (Test-HashEqual (Get-Sha256File -Path $Destination) $ExpectedSha256)) {
            throw "Restored file hash mismatch: $Destination"
        }
    }
    finally {
        if ([IO.File]::Exists($temp)) { [IO.File]::Delete($temp) }
    }
}

function Schedule-TsfRestoreChanges {
    param($Plan)
    if ($Plan.TsfRestoreChanges.Count -eq 0) { return @() }
    Initialize-DelayedFileMoveType
    $scheduled = New-Object 'System.Collections.Generic.List[object]'
    $staged = New-Object 'System.Collections.Generic.List[string]'
    try {
        foreach ($change in $Plan.TsfRestoreChanges) {
            if ($change.PendingStagePath -and [IO.File]::Exists($change.PendingStagePath)) {
                [IO.File]::Copy($change.Source, $change.PendingStagePath, $true)
                if (-not (Test-HashEqual (Get-Sha256File -Path $change.PendingStagePath) $change.RestoreSha256)) {
                    throw "Could not reset queued $($change.Architecture) TSF to its backup."
                }
                $scheduled.Add([pscustomobject]@{
                        architecture = $change.Architecture
                        path = $change.Destination
                        beforeSha256 = $change.BeforeSha256
                        restoreSha256 = $change.RestoreSha256
                        stagePath = $change.PendingStagePath
                        existingQueueReused = $true
                    })
                continue
            }
            $stagePath = $change.Destination + '.kasumi-restore-' + $Plan.Stamp + '.new'
            if ([IO.File]::Exists($stagePath) -or [IO.Directory]::Exists($stagePath)) {
                throw "TSF restore staging path already exists: $stagePath"
            }
            [IO.File]::Copy($change.Source, $stagePath, $false)
            $staged.Add($stagePath)
            if (-not (Test-HashEqual (Get-Sha256File -Path $stagePath) $change.RestoreSha256)) {
                throw "Staged $($change.Architecture) TSF restore hash mismatch."
            }
            $flags = 0x1 -bor 0x4
            if (-not [KasumiRestoreDelayedFileMoveV1]::MoveFileEx($stagePath, $change.Destination, $flags)) {
                $code = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
                throw "Could not queue the $($change.Architecture) TSF restore (Win32 $code)."
            }
            $scheduled.Add([pscustomobject]@{
                    architecture = $change.Architecture
                    path = $change.Destination
                    beforeSha256 = $change.BeforeSha256
                    restoreSha256 = $change.RestoreSha256
                    stagePath = $stagePath
                    existingQueueReused = $false
                })
        }
        return @($scheduled.ToArray())
    }
    catch {
        $queuedPaths = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
        foreach ($entry in $scheduled) {
            [void]$queuedPaths.Add($entry.stagePath)
            try {
                [IO.File]::Copy($entry.path, $entry.stagePath, $true)
                if (-not (Test-HashEqual (Get-Sha256File -Path $entry.stagePath) $entry.beforeSha256)) {
                    throw "Could not reset queued $($entry.architecture) TSF to its current image."
                }
            }
            catch { throw "TSF restore scheduling was incomplete, and its queued $($entry.architecture) replacement could not be reset: $($_.Exception.Message)" }
        }
        foreach ($stagePath in $staged) {
            if (-not $queuedPaths.Contains($stagePath) -and [IO.File]::Exists($stagePath)) {
                try { [IO.File]::Delete($stagePath) } catch { }
            }
        }
        throw
    }
}

function Invoke-RestoreTransaction {
    param($Plan)
    $preserved = [ordered]@{
        skin = $null
        config = $null
        server = $null
    }
    $skinWasPreserved = $false

    try {
        Write-Step 'Verifying backup artifacts again before modifying installed files...'
        [void](Get-BackupArtifactMap -Rollback $Plan.Rollback -BackupDirectory $Plan.BackupDirectory)

        # 1. Preserve and replace the installed skin directory. Never delete additions.
        if ([IO.Directory]::Exists($Plan.SkinDirectory)) {
            $skinPreserve = Get-UniqueSiblingPath -Directory $Plan.SkinsDirectory `
                -BaseName ($script:SkinId + '.pre-restore-' + $Plan.Stamp)
            Rename-Item -LiteralPath $Plan.SkinDirectory -NewName ([IO.Path]::GetFileName($skinPreserve))
            $preserved.skin = $skinPreserve
            $skinWasPreserved = $true
        }
        if ($Plan.SkinExisted) {
            [void][IO.Directory]::CreateDirectory($Plan.SkinDirectory)
            foreach ($skinFile in $Plan.SkinFiles) {
                $destination = Join-Path $Plan.SkinDirectory $skinFile.RelativePath.Replace('/', '\')
                if (-not (Test-PathInside -ChildPath $destination -ParentPath $Plan.SkinDirectory)) {
                    throw "Skin backup path escapes the skin directory: $($skinFile.RelativePath)"
                }
                Copy-FileAtomicVerified -Source $skinFile.FullPath -Destination $destination -ExpectedSha256 $skinFile.Sha256
            }
        }

        # 2. Preserve a differing config and write back the exact original bytes.
        if ([IO.File]::Exists($Plan.ConfigPath)) {
            $currentConfigHash = Get-Sha256File -Path $Plan.ConfigPath
            if (-not (Test-HashEqual $currentConfigHash $Plan.ConfigBeforeSha256)) {
                $configPreserve = Get-UniqueSiblingPath -Directory $Plan.DataDirectory -BaseName ('config.toml.pre-restore-' + $Plan.Stamp)
                Rename-Item -LiteralPath $Plan.ConfigPath -NewName ([IO.Path]::GetFileName($configPreserve))
                $preserved.config = $configPreserve
            }
        }
        $configBytes = [IO.File]::ReadAllBytes($Plan.ConfigBackup.FullPath)
        Write-BytesAtomic -Path $Plan.ConfigPath -Bytes $configBytes
        if (-not (Test-HashEqual (Get-Sha256File -Path $Plan.ConfigPath) $Plan.ConfigBeforeSha256)) {
            throw 'config.toml did not match its backup hash after restore.'
        }

        # 3. Preserve and restore the Server image.
        if ([IO.File]::Exists($Plan.ServerPath)) {
            $currentServerHash = Get-Sha256File -Path $Plan.ServerPath
            if (-not (Test-HashEqual $currentServerHash $Plan.ServerBeforeSha256)) {
                $serverPreserve = Get-UniqueSiblingPath -Directory $Plan.ServerDirectory -BaseName ($script:ServerFileName + '.pre-restore-' + $Plan.Stamp)
                Rename-Item -LiteralPath $Plan.ServerPath -NewName ([IO.Path]::GetFileName($serverPreserve))
                $preserved.server = $serverPreserve
            }
        }
        if (-not [IO.File]::Exists($Plan.ServerPath)) {
            [IO.File]::Copy($Plan.ServerBackup.FullPath, $Plan.ServerPath, $false)
        }
        if (-not (Test-HashEqual (Get-Sha256File -Path $Plan.ServerPath) $Plan.ServerBeforeSha256)) {
            throw 'Server image did not match its backup hash after restore.'
        }

        # 4. Normal restart and 30-second health check.
        Write-Step 'Requesting the IME normal restart through the Aux pipe...'
        $restartSince = Get-Date
        $exclude = @()
        $processes = @(Get-ServerProcesses -ServerPath $Plan.ServerPath)
        if ($processes.Count -gt 1) { throw 'Multiple Server processes were found while requesting restart.' }
        if ($processes.Count -eq 1) {
            Request-AuxRestart
            if (-not $processes[0].WaitForExit(15000)) {
                throw "Server PID $($processes[0].Id) did not accept the normal restart request."
            }
            $exclude = @([int]$processes[0].Id)
        }
        $newServer = Wait-ForNewServer -ServerPath $Plan.ServerPath -ExcludeProcessIds $exclude
        if ($null -eq $newServer) {
            throw 'The official Watchdog did not start a stable Server within 45 seconds.'
        }
        $health = Confirm-ServerHealth -ServerPid $newServer.Id -ServerPath $Plan.ServerPath -Since $restartSince -Seconds 30
        Write-Step "Restore restarted Server PID $($newServer.Id); 30-second health check passed."

        $scheduledTsf = @(Schedule-TsfRestoreChanges -Plan $Plan)
        if ($scheduledTsf.Count -gt 0) {
            Write-Step 'The original TSF files are queued for the next Windows restart.'
        }

        $result = [ordered]@{
            status = 'restored'
            restored = $true
            restarted = $true
            backup = $Plan.BackupDirectory
            serverPid = $newServer.Id
            serverSha256 = $Plan.ServerBeforeSha256
            tsfRestoreScheduled = ($scheduledTsf.Count -gt 0)
            rebootRequired = ($scheduledTsf.Count -gt 0)
            tsfChanges = $scheduledTsf
            health = $health
            preservedSkin = $preserved.skin
            preservedConfig = $preserved.config
            preservedServer = $preserved.server
            helperStarted = $false
            completedUtc = [DateTime]::UtcNow.ToString('o')
        }
        try { Write-JsonAtomic -Path (Join-Path $Plan.BackupDirectory 'restore-result.json') -Object $result }
        catch { Write-Warning "Restore succeeded, but restore-result.json could not be written: $($_.Exception.Message)" }
        return [pscustomobject]$result
    }
    catch {
        $failure = [ordered]@{
            status = 'restore-failed'
            failedUtc = [DateTime]::UtcNow.ToString('o')
            reason = $_.Exception.Message
            backup = $Plan.BackupDirectory
            preservedSkin = $preserved.skin
            preservedConfig = $preserved.config
            preservedServer = $preserved.server
            skinWasPreserved = $skinWasPreserved
            note = 'No automatic reinstall of the newer package was attempted. Preserved paths were not deleted.'
        }
        try { Write-JsonAtomic -Path (Join-Path $Plan.BackupDirectory 'restore-failure.json') -Object $failure }
        catch { Write-Warning "Could not write restore-failure.json: $($_.Exception.Message)" }
        throw
    }
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
try {
    if (-not [string]::IsNullOrWhiteSpace($BackupPath) -and $Latest) {
        throw 'Use either -BackupPath or -Latest, not both.'
    }
    $registry = Get-MetasequoiaRegistryInfo
    $defaultDataRoot = Get-DefaultDataRoot -RegistryInfo $registry
    $backupRoot = Join-Path $defaultDataRoot 'kasumi-backups'
    $inventory = @(Get-BackupInventory -Root $backupRoot)

    if ($List) {
        Show-BackupInventory -Items $inventory
        return
    }

    if ([string]::IsNullOrWhiteSpace($BackupPath) -and -not $Latest -and $inventory.Count -eq 0) {
        throw "No Kasumi backup entries were found under: $backupRoot"
    }

    $selected = Resolve-SelectedBackup -Items $inventory
    if ($null -eq $selected) {
        Write-Step 'Restore cancelled.'
        return
    }
    $plan = Get-RestorePlan -Selected $selected -RegistryInfo $registry
    $planView = [ordered]@{
        status = 'plan'
        backup = $plan.BackupDirectory
        backupStatus = $plan.Rollback.status
        dataDirectory = $plan.DataDirectory
        serverDirectory = $plan.ServerDirectory
        currentServerSha256 = $plan.CurrentServerSha256
        restoreServerSha256 = $plan.ServerBeforeSha256
        serverWillBeRestored = [bool]$plan.ServerNeedsCopy
        tsfRestoreScheduled = [bool]($plan.TsfRestoreChanges.Count -gt 0)
        rebootRequired = [bool]($plan.TsfRestoreChanges.Count -gt 0)
        tsfFilesToRestore = $plan.TsfRestoreChanges.Count
        skinFilesToRestore = $plan.SkinFiles.Count
        skinDirectoryWillBePreserved = [bool]($null -ne $plan.SkinPreservePath)
        configWillBeRestored = $true
        helperStarted = $false
    }

    if ($WhatIfPreference) {
        $planView.status = 'planned'
        $planView.whatIf = $true
        $planView.liveProcessChecksSkipped = $true
        Write-Step 'WhatIf: no backup, file, process, registry or service change was made.'
        [pscustomobject]$planView | ConvertTo-Json -Depth 6
        return
    }

    if (-not (Test-IsAdministrator)) {
        throw 'Administrator rights are required for a real restore. Start an elevated PowerShell and run Restore.ps1 again.'
    }

    [void](Assert-RestoreLiveState -Plan $plan)
    if (-not $PSCmdlet.ShouldProcess($plan.DataDirectory, 'Restore the selected Kasumi backup, preserve current files, and restart the IME')) {
        $planView.status = 'cancelled'
        [pscustomobject]$planView | ConvertTo-Json -Depth 6
        return
    }

    $result = Invoke-RestoreTransaction -Plan $plan
    [pscustomobject]$result | ConvertTo-Json -Depth 8
}
catch {
    Write-Host ('[kasumi-restore] ERROR: ' + $_.Exception.Message) -ForegroundColor Red
    throw
}
