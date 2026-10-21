#pragma once

#include <cstddef>
#include <string>
#include <vector>

// 双拼「显示转换后的全拼」用的预编辑：文本加上按键光标到显示光标的映射表。
struct ShuangpinQuanpinPreedit
{
    std::string text;
    // caret_map[i] 是按键原串光标停在 i（0..raw.size()）时 text 里的光标位置。
    std::vector<std::size_t> caret_map;
};

// raw 是用户敲的原串（含手动分词符号和句中辅助码），raw_segmentation / quanpin_segmentation
// 是同一次切分的双拼原串形式与全拼形式（见 IInputSession::get_shuangpin_preedit_forms）。
// keep_separators 为 false 时去掉全部分词符号，对应「原始按键」样式。
//
// 光标按音节对齐：落在某个双拼音节的开头或末尾时就落在对应全拼音节的开头或末尾，落在两键中间
// 时停在全拼音节的同一偏移处。两种形式的音节数对不上时退回按位置截断。
ShuangpinQuanpinPreedit BuildShuangpinQuanpinPreedit(const std::string &raw, const std::string &raw_segmentation,
                                                     const std::string &quanpin_segmentation, bool keep_separators);
