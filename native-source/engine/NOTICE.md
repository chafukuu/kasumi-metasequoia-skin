# 来源与许可

本目录由 `metasequoiaime/MSIME-Engine` 内嵌而来，来源与裁剪范围见 [UPSTREAM.md](UPSTREAM.md)。下述声明保留原样：仓库根 `LICENSE` 不覆盖也不重新授权其中的第三方代码与数据。

- 输入引擎本体：仓库根 `LICENSE`；`googlepinyinime-rev/`、`ngram/kenlm/` 与 `utfcpp/` 各自保留上游许可（见各自目录下的 `LICENSE`）。`ngram/kenlm/` 大部分为 LGPL-2.1-or-later，另有若干 BSD 例外文件，清单见 `ngram/kenlm/LICENSE`，正文见同目录的 `COPYING`。
- 辅助码：[helpcode/NOTICE.md](helpcode/NOTICE.md)。
- 语音：`voice/LICENSE`；`voice/third_party/miniaudio` 保留其上游许可。语音模型的来源、版本、许可与 SHA256 见 [voice/assets/models/README.md](voice/assets/models/README.md)：`silero_vad.onnx` 取自 snakers4/silero-vad v6.2（MIT）。
- 日本语模型发布时必须附带 `mozc_dictionary_oss_README.txt`。
- 词格整句打分用的三元模型 `sc.lm` 来自 libime（© 2022~2026 CSSlayer，LGPL-2.1-or-later）。它不随词库发布下发，而是由 `scripts/build-language-model.ps1` 从 `language-model/lock.json` 钉住的上游语料在构建时转换而来；其声明安装为 `libime-lm-NOTICE.md`。

词库数据不在本目录内。它由 `product-lock.json` 钉住的发布产物在构建时下载，其来源与许可随该发布一同分发；whisper.cpp 与本地推理权重已随专门化一并移除。
