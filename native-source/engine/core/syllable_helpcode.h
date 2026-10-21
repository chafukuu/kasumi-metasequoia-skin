#pragma once

#include <cstddef>
#include <string>
#include <vector>

// 句中辅助码（双拼）：在一个完整音节后面敲反引号，接着的字母筛这个音节上的字。
//
//   ulpb`x      第二个音节「鹤」位置上的字，辅助码第一码必须是 x
//   ulpb`xY     同上，第二码必须是 y（第二码要用大写才参与筛选）
//   ulpb`xihfa  约束留在第二个音节上，后面照常往下打
//
// 一个输入串里可以有多段。约束在整句解码时就生效（kenlm 词格建图筛边、Google 解码器
// 建格子筛字），而不是解完再筛，所以被锁住的字会带着上下文重新整句打分。
struct SyllableHelpcode
{
    // 受约束的音节，按去掉反引号段之后的双拼切分计，0 起。
    std::size_t syllable = 0;
    // 第一码，小写字母。
    char first = 0;
    // 第二码，小写字母；0 表示没有第二码，只按第一码筛。
    char second = 0;

    bool operator==(const SyllableHelpcode &other) const
    {
        return syllable == other.syllable && first == other.first && second == other.second;
    }
    bool operator!=(const SyllableHelpcode &other) const
    {
        return !(*this == other);
    }
};

using SyllableHelpcodes = std::vector<SyllableHelpcode>;

// 辅助码表里查到的码（如 "ek"）是否满足这一条约束。
inline bool syllable_helpcode_matches(const SyllableHelpcode &helpcode, const std::string &code)
{
    if (code.empty() || code[0] != helpcode.first)
        return false;
    return helpcode.second == 0 || (code.size() > 1 && code[1] == helpcode.second);
}

// 缓存键用的签名：同一串拼音带不同约束，候选不同，不能共用缓存。
inline std::string syllable_helpcodes_signature(const SyllableHelpcodes &helpcodes)
{
    std::string signature;
    for (const auto &helpcode : helpcodes)
    {
        signature += '`';
        signature += std::to_string(helpcode.syllable);
        signature += helpcode.first;
        if (helpcode.second != 0)
            signature += helpcode.second;
    }
    return signature;
}
