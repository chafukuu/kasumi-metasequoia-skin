param(
  [switch]$NoActivate
)

$ErrorActionPreference = 'Stop'
$skinId = 'niya-demo'
$source = Join-Path (Join-Path $PSScriptRoot 'skins') $skinId
$imeRoot = Join-Path $env:LOCALAPPDATA 'metasequoiaime'
$destination = Join-Path (Join-Path $imeRoot 'skins') $skinId

New-Item -ItemType Directory -Force -Path $destination | Out-Null
Copy-Item -Force -LiteralPath (Join-Path $source 'skin.toml') -Destination $destination
$toolbarCss = Join-Path $source 'toolbar.css'
if (Test-Path -LiteralPath $toolbarCss) {
  Copy-Item -Force -LiteralPath $toolbarCss -Destination $destination
}
$assets = Join-Path $source 'assets'
if (Test-Path -LiteralPath $assets) {
  Copy-Item -Recurse -Force -LiteralPath $assets -Destination $destination
}

if (-not $NoActivate) {
  $configPath = Join-Path $imeRoot 'config.toml'
  if (-not (Test-Path -LiteralPath $configPath)) {
    throw "Config not found: $configPath"
  }
  $configText = Get-Content -Raw -LiteralPath $configPath
  $updatedText = [regex]::Replace(
    $configText,
    '(?m)^candidate_skin\s*=\s*"[^"]*"\s*$',
    'candidate_skin = "niya-demo"'
  )
  if ($updatedText -eq $configText -and $configText -notmatch '(?m)^candidate_skin\s*=') {
    throw 'appearance.candidate_skin was not found in config.toml'
  }
  Set-Content -LiteralPath $configPath -Value $updatedText -Encoding utf8 -NoNewline
}

Write-Host "Installed $skinId to $destination"
Write-Host 'Restart Metasequoia IME Server to reload the skin package.'
