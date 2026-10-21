"""从 msime-dictionary 的文本源数据构建 Windows 端词库（msime.db、english.db、others.db、dict_japanese.dat）。

规则移植自 msime 的 Rust 构建器 crates/dict-builder（inputs.lock.json 的 msime.commit），
同一份源数据应得到与该提交构建的 dict-v* Release 逐行一致的表；`--compare-release` 检查这一点。
入口是 scripts/build-dictionary.py。
"""
