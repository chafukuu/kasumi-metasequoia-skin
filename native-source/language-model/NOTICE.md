# 语言模型 sc.lm 的来源与许可

本产品安装的 `sc.lm` 是一个中文三元语言模型，用于给输入法的整句候选打分。

它由 libime 发布的语料模型 `lm_sc.arpa` 转换而来，转换不改变模型内容，只是把
ARPA 文本格式压缩成 kenlm 的量化 trie 二进制格式。

- 上游项目：libime — https://github.com/fcitx/libime
- 数据来源：https://download.fcitx-im.org/data/lm_sc.arpa-20260629.tar.zst
- 版权：Copyright (C) 2022~2026 CSSlayer &lt;wengxt@gmail.com&gt;
- 许可：GNU Lesser General Public License, version 2.1 or later (LGPL-2.1-or-later)

LGPL-2.1 的完整正文随本产品一同安装，见 `THIRD_PARTY_NOTICES.txt` 中 kenlm 一节
所附的许可证全文；该正文同样适用于本模型数据。

读取该模型的代码是 kenlm（https://github.com/kpu/kenlm），其来源、裁剪范围与许可
另见 `THIRD_PARTY_NOTICES.txt`。仓库根 `LICENSE` 不覆盖也不重新授权上述第三方数据。
