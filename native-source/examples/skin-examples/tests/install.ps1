$ErrorActionPreference = 'Stop'
$originalLocalAppData = $env:LOCALAPPDATA
$testRoot = Join-Path ([System.IO.Path]::GetTempPath()) ([guid]::NewGuid().ToString())
try {
    $env:LOCALAPPDATA = $testRoot
    $imeRoot = Join-Path $testRoot 'metasequoiaime'
    $destination = Join-Path $imeRoot 'skins/niya-demo'
    $config = Join-Path $imeRoot 'config.toml'
    & "$PSScriptRoot/../install.ps1" -NoActivate
    if (Test-Path $config) { throw 'NoActivate must not create settings' }
    foreach ($file in @('skin.toml', 'assets/character.png', 'assets/background.png')) {
        $source = Join-Path "$PSScriptRoot/../skins/niya-demo" $file
        $copy = Join-Path $destination $file
        if ((Get-FileHash $source).Hash -ne (Get-FileHash $copy).Hash) {
            throw "Copied asset differs: $file"
        }
    }
    $initial = "[appearance]`ncandidate_skin = `"fluent`"`nother_setting = true`n"
    Set-Content -LiteralPath $config -Value $initial -NoNewline
    & "$PSScriptRoot/../install.ps1"
    $actual = Get-Content -Raw -LiteralPath $config
    if ($actual -ne $initial.Replace('"fluent"', '"niya-demo"')) {
        throw 'Activation changed unrelated settings or failed to select the skin'
    }
    & "$PSScriptRoot/../install.ps1"
    if ((Get-Content -Raw -LiteralPath $config) -ne $actual) {
        throw 'Installing twice is not idempotent'
    }
} finally {
    $env:LOCALAPPDATA = $originalLocalAppData
    Remove-Item -Recurse -Force $testRoot -ErrorAction SilentlyContinue
}
