# 词格整句打分用的语言模型

本目录钉住 `sc.lm` 的来源，并放置生成它所需的声明。`sc.lm` 是一个 kenlm 三元模型，
`engine/ngram` 把它 mmap 起来给词格的整句候选打分；缺了它引擎会退回启发式打分，
整句候选只是变差，不会停止工作（见 `engine/docs/runtime-architecture.md`）。

模型本身不在仓库里，也不随词库发布下发。它由 `scripts/build-language-model.ps1`
从 `lock.json` 钉住的上游 ARPA 现场转换而来：

```powershell
.\scripts\build-language-model.ps1
```

该脚本下载 ARPA 压缩包、校验其 SHA-256、按 `lock.json` 里的参数转换成 `sc.lm`，
再校验产物的 SHA-256。转换是确定性的，所以产物摘要可以像下载摘要一样钉死。
脚本是幂等的：`sc.lm` 已存在且摘要正确时直接返回，不重新下载也不重新转换。
`installer\Invoke-LocalTest.ps1` 在打完整包前会自动调用它，因此日常跑 `test.ps1`
不需要手工执行。

上游与 libime 用的是同一份数据和同一组参数（见 libime 的 `data/CMakeLists.txt`），
这样我们编出来的模型与 fcitx5 发行版里的 `zh_CN.lm` 等价。`lock.json` 的
`build.order` 必须与 `engine/ngram/CMakeLists.txt` 里的 `KENLM_MAX_ORDER` 一致，
否则运行时读不懂编出来的文件。

`NOTICE.md` 是随产品安装的授权声明，安装后名为 `libime-lm-NOTICE.md`。它进仓库，
因为它是需要评审的声明文本，不是可以重新生成的产物。
