#pragma once

#include <cstddef>
#include <string>
#include <vector>

// Server 回给 TSF 的行内预编辑可以在末尾附一张光标映射表。
//
// TSF 平时按「字母个数」把按键缓冲里的光标映射到预编辑上，这要求预编辑和按键缓冲是同一串字母、
// 只多了分词符号。双拼显示转换后的全拼时不成立（hc 显示成 hao），只有 Server 知道每个按键位置
// 落在哪个音节，所以由它把整张表带过来：
//
//   预编辑 U+E001 p0,p1,...,pN
//
// pi 是按键缓冲光标停在 i（0..N，N 为按键缓冲长度）时预编辑里的光标位置，十进制、逗号分隔。
// 没有表的回包和旧 Server 的回包照旧解释；表的条数和按键缓冲对不上时 TSF 回落到字母映射。
// 回包可能被 Tab 分段（UILess、造词），表里不会出现 Tab。
namespace FanyImePreeditCaretMap
{
inline constexpr wchar_t kMarker = L'\uE001';

inline std::wstring Encode(const std::wstring &preedit, const std::vector<std::size_t> &caret_map)
{
    if (caret_map.empty())
    {
        return preedit;
    }
    std::wstring encoded = preedit;
    encoded.push_back(kMarker);
    for (std::size_t i = 0; i < caret_map.size(); ++i)
    {
        if (i > 0)
        {
            encoded.push_back(L',');
        }
        encoded += std::to_wstring(caret_map[i]);
    }
    return encoded;
}

// 把 payload 里的映射表拆出来，payload 只留下预编辑本身。没有表或表写坏了时 caret_map 为空。
inline void Decode(std::wstring &payload, std::vector<std::size_t> &caret_map)
{
    caret_map.clear();
    const std::size_t marker = payload.find(kMarker);
    if (marker == std::wstring::npos)
    {
        return;
    }
    std::size_t value = 0;
    bool has_digit = false;
    bool valid = true;
    for (std::size_t i = marker + 1; i < payload.size() && valid; ++i)
    {
        const wchar_t ch = payload[i];
        if (ch >= L'0' && ch <= L'9')
        {
            value = value * 10 + static_cast<std::size_t>(ch - L'0');
            has_digit = true;
        }
        else if (ch == L',' && has_digit)
        {
            caret_map.push_back(value);
            value = 0;
            has_digit = false;
        }
        else
        {
            valid = false;
        }
    }
    if (valid && has_digit)
    {
        caret_map.push_back(value);
    }
    else
    {
        caret_map.clear();
    }
    payload.erase(marker);
    for (const std::size_t position : caret_map)
    {
        if (position > payload.size())
        {
            caret_map.clear();
            break;
        }
    }
}
} // namespace FanyImePreeditCaretMap
