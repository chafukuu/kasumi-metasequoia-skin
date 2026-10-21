# 生成词格整句打分用的 sc.lm。
#
# 词库是从钉住的发布下载的，语言模型不是：它的上游是 libime 的 ARPA 语料，我们按
# libime 自己那组参数现场转换。转换是确定性的，所以 language-model\lock.json 既钉
# 下载的摘要，也钉产物的摘要 —— 两头都校验过，本地和 CI 才会得到同一个文件。
#
# 幂等：sc.lm 已在且摘要正确时直接返回。因此 Invoke-LocalTest.ps1 可以在每次打完整
# 包前无条件调用它，日常 test.ps1 不会为此多花时间。
[CmdletBinding()]
param(
    [string]$RepoRoot = (Split-Path -Parent $PSScriptRoot),
    # 下载的压缩包与解出来的 ARPA 都留在这里。ARPA 有 ~190 MB，保留它是为了改转换参数
    # 时不必重下；删掉也只是下次多下一次。
    [string]$CacheDirectory,
    # 即使摘要已经对上也重新转换一次。改了 lock.json 的 build 参数时用。
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$modelDirectory = Join-Path $RepoRoot 'language-model'
$lockPath = Join-Path $modelDirectory 'lock.json'
if (-not (Test-Path -LiteralPath $lockPath)) {
    throw "语言模型的来源锁不存在：$lockPath"
}
$lock = Get-Content -LiteralPath $lockPath -Raw | ConvertFrom-Json
if ($lock.schema_version -ne 1) {
    throw "不支持的 lock.json schema_version：$($lock.schema_version)"
}
if (-not $CacheDirectory) { $CacheDirectory = Join-Path $modelDirectory 'cache' }

$modelPath = Join-Path $modelDirectory $lock.output.path

function Test-Digest {
    param([Parameter(Mandatory)][string]$LiteralPath, [Parameter(Mandatory)][string]$Expected)
    if (-not (Test-Path -LiteralPath $LiteralPath)) { return $false }
    return (Get-FileHash -Algorithm SHA256 -LiteralPath $LiteralPath).Hash.ToLower() -eq $Expected.ToLower()
}

function Assert-Digest {
    param([Parameter(Mandatory)][string]$LiteralPath, [Parameter(Mandatory)][string]$Expected,
          [Parameter(Mandatory)][string]$Description)
    $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $LiteralPath).Hash.ToLower()
    if ($actual -ne $Expected.ToLower()) {
        throw "$Description 的 SHA256 与 lock.json 不符：期望 $($Expected.ToLower())，实得 $actual（$LiteralPath）"
    }
}

if (-not $Force -and (Test-Digest -LiteralPath $modelPath -Expected $lock.output.sha256)) {
    Write-Host "语言模型已就绪：$modelPath"
    return
}

New-Item -ItemType Directory -Path $CacheDirectory -Force | Out-Null
$archiveName = Split-Path -Leaf ([uri]$lock.source.url).AbsolutePath
$archivePath = Join-Path $CacheDirectory $archiveName
$arpaPath = Join-Path $CacheDirectory $lock.source.member

if (-not (Test-Digest -LiteralPath $archivePath -Expected $lock.source.archive_sha256)) {
    Write-Host "下载语料模型：$($lock.source.url)"
    # 摘要没对上的残留必须先删：curl 的断点续传会把两次不同的下载接在一起。
    if (Test-Path -LiteralPath $archivePath) { Remove-Item -LiteralPath $archivePath -Force }
    curl.exe -L --fail --retry 3 -o $archivePath $lock.source.url
    if ($LASTEXITCODE -ne 0) { throw "下载语料模型失败（$LASTEXITCODE）：$($lock.source.url)" }
}
Assert-Digest -LiteralPath $archivePath -Expected $lock.source.archive_sha256 -Description '语料模型压缩包'

# Windows 自带的 tar 是 bsdtar，直接认 zstd，不需要另装 zstd。
if (Test-Path -LiteralPath $arpaPath) { Remove-Item -LiteralPath $arpaPath -Force }
tar.exe -xf $archivePath -C $CacheDirectory
if ($LASTEXITCODE -ne 0) { throw "解压语料模型失败（$LASTEXITCODE）：$archivePath" }
if (-not (Test-Path -LiteralPath $arpaPath)) {
    throw "压缩包里没有 $($lock.source.member)：$archivePath"
}

# 只 configure engine\ngram 这一个目录，不走 engine\CMakeLists.txt。转换工具只依赖
# kenlm 自己（零 Boost、零 SQLite），为它拉起整个引擎的依赖没有意义，在只下载了
# 二进制产物的打包机上还未必装得起来。
$toolBuild = Join-Path $CacheDirectory 'build-tool'
$toolPath = Join-Path $toolBuild 'Release\metasequoia_ime_build_binary.exe'
if (-not (Test-Path -LiteralPath $toolPath)) {
    cmake -S (Join-Path $RepoRoot 'engine\ngram') -B $toolBuild -DMETASEQUOIA_IME_BUILD_NGRAM_TOOLS=ON
    if ($LASTEXITCODE -ne 0) { throw "配置转换工具失败（$LASTEXITCODE）" }
    cmake --build $toolBuild --config Release --target metasequoia_ime_build_binary --parallel
    if ($LASTEXITCODE -ne 0) { throw "构建转换工具失败（$LASTEXITCODE）" }
}
if (-not (Test-Path -LiteralPath $toolPath)) {
    # 单配置生成器（Ninja、Makefiles）不套 Release 子目录。
    $toolPath = Join-Path $toolBuild 'metasequoia_ime_build_binary.exe'
    if (-not (Test-Path -LiteralPath $toolPath)) { throw "没有找到转换工具：$toolBuild" }
}

# 先写到临时文件再改名：转换中途失败不应留下一个摘要对不上的 sc.lm，让下次运行
# 以为只是缓存脏了。
$stagingPath = "$modelPath.incoming"
if (Test-Path -LiteralPath $stagingPath) { Remove-Item -LiteralPath $stagingPath -Force }
& $toolPath @($lock.build.flags) $arpaPath $stagingPath
if ($LASTEXITCODE -ne 0) { throw "转换语言模型失败（$LASTEXITCODE）" }
Assert-Digest -LiteralPath $stagingPath -Expected $lock.output.sha256 -Description '语言模型 sc.lm'
Move-Item -LiteralPath $stagingPath -Destination $modelPath -Force

Write-Host "语言模型生成完成：$modelPath（$((Get-Item -LiteralPath $modelPath).Length) 字节）"
