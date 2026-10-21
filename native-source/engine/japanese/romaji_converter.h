#pragma once

#include <string>
#include <string_view>
#include <optional>
#include <vector>

namespace japanese
{
struct RomajiConversion
{
    struct Segment
    {
        size_t input_end;
        size_t hiragana_end;
    };
    std::string hiragana;
    std::string pending;
    bool complete = false;
    // 保存实际按键边界，避免把 shi/si、nn/n' 等写法反向转换成另一种罗马音。
    std::vector<Segment> segments;
    std::optional<size_t> InputLengthForHiragana(size_t length) const;
};

RomajiConversion ConvertRomaji(std::string_view input);
std::string HiraganaToKatakana(std::string_view hiragana);
std::string HiraganaToRomaji(std::string_view kana);
bool IsSingleKanaConversion(const RomajiConversion &conversion);

// Romaji prefixes such as "k" or "ky" map to every table kana whose spelling
// starts with that prefix. This is the Japanese counterpart of Google Pinyin's
// half spelling id (shengmu).
std::vector<std::string> KanaForRomajiPrefix(std::string_view pending);
} // namespace japanese
