# 0.9.x 把效果档模型装在数据目录根下，新版只认 models\neural-desktop\，覆盖安装的清理又会把根下那份
# 当退役文件删掉。AdoptRetiredDesktopModel 在清理之前把它挪过去；这里把那段 Pascal 原样抠出来，
# 编进探针安装器真跑。期望的 SHA256 是参数，所以用小文件代替 25 MB 的真模型。
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

$work = Join-Path ([IO.Path]::GetTempPath()) ('msime-desktop-model-' + [Guid]::NewGuid())
$modelName = 'sentence-model-desktop.safetensors'
function Write-Text([string]$Path, [string]$Text) {
    New-Item -ItemType Directory -Force -Path (Split-Path $Path) | Out-Null
    [IO.File]::WriteAllText($Path, $Text)
}
function Invoke-Probe([string]$DataDir) {
    $env:MSIME_PROBE_DATA_DIR = $DataDir
    try {
        $process = Start-Process $probeExe -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES' -PassThru -Wait
    } finally {
        Remove-Item Env:MSIME_PROBE_DATA_DIR
    }
    if (-not (Test-Path (Join-Path $DataDir 'ran.txt'))) { throw "Probe did not run (exit code $($process.ExitCode))" }
}
try {
    New-Item -ItemType Directory -Force -Path $work | Out-Null
    $fixture = Join-Path $work 'model-fixture'
    [IO.File]::WriteAllText($fixture, 'model-v1 bytes')
    $expected = (Get-FileHash $fixture -Algorithm SHA256).Hash.ToLowerInvariant()

    $iss = [IO.File]::ReadAllText((Join-Path $PSScriptRoot '../msime_setup.iss'))
    $procedure = [regex]::Match($iss, '(?s)procedure AdoptRetiredDesktopModel\(.*?\r?\nend;').Value
    if (-not $procedure) { throw 'msime_setup.iss is missing AdoptRetiredDesktopModel' }
    $call = [regex]::Match($iss, "(?s)BackupChangedBundledSkins;.*?AdoptRetiredDesktopModel\('([0-9a-f]{64})'\);\r?\n#ifdef LightPackage")
    if (-not $call.Success) { throw 'AdoptRetiredDesktopModel must run before the light/full cleanup split' }
    $lock = Get-Content (Join-Path $PSScriptRoot '../../neural-model/lock.json') -Raw | ConvertFrom-Json
    $locked = ($lock.models | Where-Object file -eq $modelName).sha256
    if ($call.Groups[1].Value -ne $locked) { throw "Installer hash $($call.Groups[1].Value) does not match neural-model/lock.json $locked" }

    $probe = Join-Path $work 'probe.iss'
    @"
[Setup]
AppName=Desktop model probe
AppVersion=1.0
CreateAppDir=no
PrivilegesRequired=lowest
Uninstallable=no
OutputDir=$work
OutputBaseFilename=desktop-model-probe
[Code]
function GetDataDir(Param: String): String;
begin
  Result := GetEnv('MSIME_PROBE_DATA_DIR');
end;

$procedure

function InitializeSetup: Boolean;
begin
  AdoptRetiredDesktopModel('$expected');
  SaveStringToFile(AddBackslash(GetDataDir('')) + 'ran.txt', 'ok', False);
  Result := False;
end;
"@ | Set-Content -Path $probe -Encoding UTF8
    & (Resolve-Iscc) /Q $probe
    if ($LASTEXITCODE -ne 0) { throw "ISCC failed to compile the probe (exit code $LASTEXITCODE)" }
    $probeExe = Join-Path $work 'desktop-model-probe.exe'

    # 升级用户：根下有模型、新位置没有 -> 挪过去。
    $upgrade = Join-Path $work 'upgrade'
    Write-Text (Join-Path $upgrade $modelName) 'model-v1 bytes'
    Invoke-Probe $upgrade
    if (Test-Path (Join-Path $upgrade $modelName)) { throw 'Retired desktop model was left in the data directory root' }
    if (-not (Test-Path (Join-Path $upgrade "models\neural-desktop\$modelName"))) { throw 'Desktop model was not moved to models\neural-desktop' }

    # 已经下载过：不覆盖新位置那份，根下的留给清理删。
    $downloaded = Join-Path $work 'downloaded'
    Write-Text (Join-Path $downloaded $modelName) 'model-v1 bytes'
    Write-Text (Join-Path $downloaded "models\neural-desktop\$modelName") 'downloaded copy'
    Invoke-Probe $downloaded
    if ([IO.File]::ReadAllText((Join-Path $downloaded "models\neural-desktop\$modelName")) -ne 'downloaded copy') {
        throw 'An already downloaded desktop model was overwritten'
    }

    # 校验和不对：不挪，不能把坏文件当成可用模型。
    $mismatch = Join-Path $work 'mismatch'
    Write-Text (Join-Path $mismatch $modelName) 'something else'
    Invoke-Probe $mismatch
    if (Test-Path (Join-Path $mismatch "models\neural-desktop\$modelName")) { throw 'A mismatched file was adopted as the desktop model' }

    Write-Host 'Retired desktop model: moved on upgrade, existing download kept, mismatched file ignored'
} finally {
    if (Test-Path $work) { Remove-Item $work -Recurse -Force }
}
