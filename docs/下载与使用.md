# 下载与使用

## 下载哪个文件

打开 [v0.5.3 发布页](https://github.com/chafukuu/kasumi-metasequoia-skin/releases/tag/v0.5.3)，下载：

`kasumi-metasequoia-skin-v0.5.3-windows-x64.zip`

同时提供 `SHA256SUMS.txt`。在 PowerShell 中可用以下命令核对下载文件：

```powershell
Get-FileHash .\kasumi-metasequoia-skin-v0.5.3-windows-x64.zip -Algorithm SHA256
```

结果应与校验文件相同。GitHub 自动提供的 Source code (zip/tar.gz) 是源码包，不包含安装用的 Server 二进制。

## 安装条件

- Windows x64；本机验证环境为 Windows 11。
- 已安装并能正常输入的 [水杉输入法 v0.11.1](https://github.com/metasequoiaime/msime-windows/releases/tag/v0.11.1)。本包不是完整输入法安装器，不包含词库和 TSF 注册组件。
- Microsoft Visual C++ v14 x64 运行库至少达到构建工具对应版本。当前构建使用 MSVC 14.51；可从[微软官方页面](https://learn.microsoft.com/zh-cn/cpp/windows/latest-supported-vc-redist)获取更新。
- 可以通过正常 Windows 管理员确认。

本体哈希不在发布清单的兼容列表时，安装器会停止。请保留报错和版本信息，不要跳过检查强行替换。

## 安装

1. 完整解压 Windows 安装包，不要在压缩软件内部运行。
2. 双击 `Install.cmd`，完成正常的 UAC 确认。
3. 等待安装器完成备份、替换和持续运行检查。安装期间输入法会正常重启。
4. 出现成功提示后，在记事本切换到水杉输入法。
5. 输入 `xiangcheng`，再连续输入、删除较长内容，检查候选都处于白色飘带内。
6. 按 Esc 或完成选词，检查文字、飘带和人物一起消失。

默认安装皮肤 `kasumi-adaptive`、字号 15、每页 7 项、横排单行并隐藏预编辑行。超长候选只在显示时省略，实际提交内容保持完整。

本版已由输入法本体绘制全部图形，无需另外启动 `KasumiBandOverlay`、裁切器或 LiveSD。旧伴随程序存在时，安装器会在验证其身份后请求正常退出。

## 只查看安装计划

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Install.ps1 -WhatIf
```

`-WhatIf` 不修改输入法，不需要管理员权限。目录与默认位置不同时，可以显式指定：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Install.ps1 -WhatIf -ServerDirectory 'D:\MetasequoiaIME\server' -DataDirectory 'D:\MetasequoiaData'
```

真实安装时在管理员 PowerShell 中执行相同命令并去掉 `-WhatIf`。默认先读取原安装的注册表路径，缺失时使用 Program Files 与当前用户 LocalAppData 的常规位置。

## 更新

保留旧下载包与备份，解压新版本后运行新的 `Install.cmd`。每次真实修改前会生成独立备份。完全相同的版本和设置会直接报告无需修改。

## 回退

备份通常位于：

`%LOCALAPPDATA%\metasequoiaime\kasumi-backups\<时间戳>`

自定义数据目录则位于该目录下的 `kasumi-backups`。备份内的 `rollback.json` 记录原本体、皮肤和配置的校验值。

先列出备份：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Restore.ps1 -List
```

在管理员 PowerShell 中预览并恢复最近一次备份：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Restore.ps1 -Latest -WhatIf
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Restore.ps1 -Latest
```

也可以用 `-BackupPath '完整备份目录'` 选择明确的一份备份。恢复会重启输入法；当前文件会另存，词库和用户输入历史不参与替换。未知自编本体会拒绝覆盖。

## 常见问题

| 现象 | 处理 |
|---|---|
| 下载后找不到 Install.cmd | 下载 Windows x64 安装包，而非 Source code；完整解压 |
| 提示本体哈希不受支持 | 确认安装的是水杉 v0.11.1，保留报错，不强行覆盖 |
| 提示缺少 VCRUNTIME/MSVCP 或无法找到入口点 | 从微软官方页面更新 v14 x64 运行库，再重试 |
| 看到两套人物或装饰 | 退出旧伴随程序，重新运行安装器；本版仅需水杉本体 |
| 长候选显示省略号 | 这是单行显示方式；选中后提交完整内容 |
| 管理员应用里皮肤不显示 | 当前本体未签名且未启用 UIAccess，该使用范围尚未验收 |
| 安装或重启失败 | 查看安装输出与备份；必要时按上文回退 |

提交问题时可附系统缩放、应用名、水杉版本和截图；请先删去截图中的私人输入内容。
