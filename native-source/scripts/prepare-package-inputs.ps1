# 打完整测试包（installer\test.ps1）前把仓外输入准备到位：
#   - neural-model\sentence-model.safetensors / sentence-model-desktop.safetensors
#   - language-model\sc.lm
#   - MetasequoiaImeDict\out\ 下的词库：用 build-dictionary.py 从本仓同级的 msime-dictionary 构建，不下载 Release
# 模型按各自的锁校验 SHA256；全部是幂等的：已就绪就直接跳过。
[CmdletBinding()]
param(
    # 忽略已有文件，强制重新下载 / 重新转换 / 重新构建词库。
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

& (Join-Path $PSScriptRoot 'fetch-neural-model.ps1') -Force:$Force
& (Join-Path $PSScriptRoot 'build-language-model.ps1') -Force:$Force

# 词库。本地测试包一律从 msime-dictionary 文本源数据构建（默认读本仓同级目录，可用环境变量
# MSIME_DICTIONARY 指定）；源数据提交和构建脚本都没变、工作区干净时 --if-stale 直接跳过。
# CI 与正式发布仍按 product-lock.json 取 dict-* release，不走这里。
$buildDictionary = Join-Path $PSScriptRoot 'build-dictionary.py'
if ($Force) { python $buildDictionary } else { python $buildDictionary --if-stale }
if ($LASTEXITCODE -ne 0) { throw "从 msime-dictionary 构建词库失败（$LASTEXITCODE）" }

Write-Host '全部就绪，可以运行 .\installer\test.ps1。'
