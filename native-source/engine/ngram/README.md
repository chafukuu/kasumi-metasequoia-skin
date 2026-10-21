# ngram

整句候选的打分模型。`kenlm/` 是 [kpu/kenlm](https://github.com/kpu/kenlm) 查询子集的上游副本（提交号见 [../UPSTREAM.md](../UPSTREAM.md)），`language_model.h` 是引擎唯一看得到的接口。

引擎其余部分不 include 任何 kenlm 头文件：`language_model.cpp` 是唯一的那个翻译单元，kenlm 的 include 目录和它需要的 `_HAS_AUTO_PTR_ETC` 因此都能保持 `PRIVATE`。

## 用法

```cpp
#include "engine/ngram/language_model.h"

const auto &model = ngram::shared_language_model(paths.resource(assets::language_model));
if (!model.valid())
{
    // 模型缺失不是错误，调用方按没有模型处理（词格会退回按词频的启发式打分）。
}
ngram::State state = model.null_state(), next;
const float score = model.score(state, "你好", next); // log10 概率
```

几个容易踩的点：

- **从 `null_state()` 起算，不要用 `begin_state()`。** 出货的 `sc.lm` 是从一份不带 `<s>` / `</s>` 的 ARPA 建的，句首上下文只会带进一个模型没有任何 n-gram 的词。
- **分数是 log10**，和词格原先那套自然对数的启发式不能混着加。
- 未登录词按 `unknown_penalty()` 罚分，默认 `log10(1/6e7) ≈ -7.78`，与 libime 的 `DEFAULT_LANGUAGE_MODEL_UNKNOWN_PROBABILITY_PENALTY` 取同一个值。换这个数等于整体重调未登录词相对已登录词的代价。
- `shared_language_model()` 按路径共享实例。`sc.lm` 有 34 MB，全拼和双拼两本词典各载一份是白白多映射一遍；kenlm 的查询全是只读的，共享实例可以被多个线程同时用。

## 重新生成语言模型

出货的 `sc.lm` 由 libime 的 `lm_sc.arpa` 转换而来，转换工具就是 kenlm 自带的 `build_binary`，默认不构建。平时不必手工做这件事：`scripts/build-language-model.ps1` 会按 `language-model/lock.json` 下载语料、转换、两头校验摘要，打完整包时自动被调用（见 [../../language-model/README.md](../../language-model/README.md)）。

要手工跑一遍，或者调转换参数时：

```bash
# 本目录可以单独 configure，不必为 engine 依赖的 SQLite 拉起 vcpkg
cmake -S engine/ngram -B build-tool -DMETASEQUOIA_IME_BUILD_NGRAM_TOOLS=ON
cmake --build build-tool --config Release --target metasequoia_ime_build_binary
./build-tool/Release/metasequoia_ime_build_binary -s -a 22 -q 4 trie lm_sc.arpa sc.lm
```

改了参数就要同步改 `language-model/lock.json` 里的 `build.flags` 和 `output.sha256`，否则构建脚本会拿新产物去对旧摘要而报错。

`-q 4 -a 22` 与 `lm::ngram::QuantArrayTrieModel` 是一一对应的：改了参数就要改 `language_model.cpp` 里的 `using Model`，否则载入时抛 `FormatLoadException`。`-s` 对应 `config.sentence_marker_missing = lm::SILENT`，因为这份 ARPA 没有句首句尾标记。

`KENLM_MAX_ORDER=3` 必须与建模时的阶数一致，改它等于让运行时读不懂已经发布的模型文件。
