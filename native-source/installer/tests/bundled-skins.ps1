# 覆盖随包皮肤之前的备份逻辑跑在安装器的 Pascal 脚本里，只看 .iss 文本证明不了它对。
# 这里把 msime_setup.iss 里的那几个函数原样抠出来，编进一个只在 InitializeSetup 里调用它们、
# 数据目录指向临时目录的探针安装器，真跑一遍再看目录。
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Resolve-Iscc {
    $command = Get-Command 'ISCC.exe' -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command) { return $command.Source }
    foreach ($version in @(7, 6)) {
        foreach ($root in @(${env:ProgramFiles(x86)}, $env:ProgramFiles, (Join-Path $env:LOCALAPPDATA 'Programs'))) {
            if (-not $root) { continue }
            $candidate = Join-Path $root "Inno Setup $version\ISCC.exe"
            if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
        }
    }
    throw '找不到 ISCC.exe'
}

$work = Join-Path ([IO.Path]::GetTempPath()) ('msime-bundled-skins-' + [Guid]::NewGuid())
function Write-Text([string]$Path, [string]$Text) {
    New-Item -ItemType Directory -Force -Path (Split-Path $Path) | Out-Null
    [IO.File]::WriteAllText($Path, $Text)
}
try {
    $bundle = Join-Path $work 'bundled_skins'
    $skins = Join-Path $work 'data\skins'
    $ids = @('same', 'changed', 'extra', 'missing', 'twice', 'absent')
    foreach ($id in $ids) {
        Write-Text (Join-Path $bundle "$id\skin.toml") "id = `"$id`"`n"
        Write-Text (Join-Path $bundle "$id\assets\bg.png") "$id png"
    }
    $manifest = foreach ($id in $ids) {
        foreach ($file in @('assets\bg.png', 'skin.toml')) {
            $hash = (Get-FileHash (Join-Path $bundle "$id\$file") -Algorithm SHA256).Hash.ToLowerInvariant()
            "$id|$file|$hash"
        }
    }
    $manifestPath = Join-Path $work 'bundled_skins.manifest'
    [IO.File]::WriteAllLines($manifestPath, [string[]]$manifest, [Text.UTF8Encoding]::new($false))

    New-Item -ItemType Directory -Force -Path $skins | Out-Null
    foreach ($id in @('same', 'changed', 'extra', 'missing', 'twice')) {
        Copy-Item (Join-Path $bundle $id) $skins -Recurse
    }
    Write-Text (Join-Path $skins 'changed\assets\bg.png') 'user edit'
    Write-Text (Join-Path $skins 'extra\assets\mine.png') 'user file'
    Remove-Item (Join-Path $skins 'missing\assets\bg.png')
    Write-Text (Join-Path $skins 'twice\skin.toml') 'user edit'
    Write-Text (Join-Path $skins 'twice.bak\skin.toml') 'earlier backup'

    $iss = [IO.File]::ReadAllText((Join-Path $PSScriptRoot '../msime_setup.iss'))
    $functions = [regex]::Match($iss, '(?s)function CountFilesUnder\(.*?(?=\r?\nfunction PrepareToInstall\()').Value
    if (-not $functions) { throw 'msime_setup.iss is missing the bundled skin backup functions' }
    $resultPath = Join-Path $work 'result.txt'
    $probe = Join-Path $work 'probe.iss'
    @"
[Setup]
AppName=Bundled skin probe
AppVersion=1.0
CreateAppDir=no
PrivilegesRequired=lowest
Uninstallable=no
OutputDir=$work
OutputBaseFilename=bundled-skin-probe
[Files]
Source: "$manifestPath"; Flags: dontcopy
[Code]
function GetDataDir(Param: String): String;
begin
  Result := '$(Join-Path $work 'data')';
end;

$functions

function InitializeSetup: Boolean;
var
  Problem: String;
begin
  Problem := BackupChangedBundledSkins;
  if Problem = '' then
    SaveStringToFile('$resultPath', 'ok', False)
  else
    SaveStringToFile('$resultPath', 'error', False);
  Result := False;
end;
"@ | Set-Content -Path $probe -Encoding UTF8

    & (Resolve-Iscc) /Q $probe
    if ($LASTEXITCODE -ne 0) { throw "ISCC failed to compile the probe (exit code $LASTEXITCODE)" }
    $process = Start-Process (Join-Path $work 'bundled-skin-probe.exe') -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES' -PassThru -Wait
    if (-not (Test-Path $resultPath)) { throw "Probe did not run (exit code $($process.ExitCode))" }
    if ([IO.File]::ReadAllText($resultPath) -ne 'ok') { throw 'BackupChangedBundledSkins reported an error' }

    $actual = @(Get-ChildItem -LiteralPath $skins -Directory | ForEach-Object Name | Sort-Object)
    $expected = @('changed.bak', 'extra.bak', 'missing.bak', 'same', 'twice.2.bak', 'twice.bak' | Sort-Object)
    if (($actual -join ',') -cne ($expected -join ',')) {
        throw "Unexpected skins directory after backup: $($actual -join ', ')"
    }
    if ([IO.File]::ReadAllText((Join-Path $skins 'changed.bak\assets\bg.png')) -ne 'user edit') { throw 'changed.bak lost the user edit' }
    if (-not (Test-Path (Join-Path $skins 'extra.bak\assets\mine.png'))) { throw 'extra.bak lost the user file' }
    if ([IO.File]::ReadAllText((Join-Path $skins 'twice.bak\skin.toml')) -ne 'earlier backup') { throw 'An earlier backup was overwritten' }
    if ([IO.File]::ReadAllText((Join-Path $skins 'twice.2.bak\skin.toml')) -ne 'user edit') { throw 'twice.2.bak lost the user edit' }
    Write-Host 'Bundled skins: unchanged kept, changed/extra/missing backed up, earlier backups preserved'
} finally {
    if (Test-Path $work) { Remove-Item $work -Recurse -Force }
}
