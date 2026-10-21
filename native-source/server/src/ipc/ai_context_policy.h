#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace FanyImeIpc
{
// 一段连续、不含空白的 ASCII 可见字符，只要够长且混了字母、数字、符号中的两类以上，就当作
// 密码、token 或密钥一类的凭证：AI 联想的上下文要发到云端，宁可少带也不能带出去。普通英文
// 单词、单独的数字和短缩写都留着，它们对联想有用，也不像凭证。
inline constexpr std::size_t kCredentialLikeMinLength = 8;

constexpr bool IsCredentialLikeAsciiRun(std::string_view run)
{
    if (run.size() < kCredentialLikeMinLength)
        return false;
    bool letter = false;
    bool digit = false;
    bool symbol = false;
    for (const char ch : run)
    {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z'))
            letter = true;
        else if (ch >= '0' && ch <= '9')
            digit = true;
        else
            symbol = true;
    }
    return static_cast<int>(letter) + static_cast<int>(digit) + static_cast<int>(symbol) >= 2;
}

// 把上下文里像凭证的 ASCII 串整段去掉，其余字节（包括 UTF-8 的中文）原样保留。
// Header-only pure policy so tests can pin it without linking the server stack.
inline std::string StripCredentialLikeText(std::string_view text)
{
    const auto visible_ascii = [](char ch) { return ch > ' ' && ch < 0x7F; };
    std::string result;
    result.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size())
    {
        if (!visible_ascii(text[i]))
        {
            result.push_back(text[i++]);
            continue;
        }
        std::size_t end = i;
        while (end < text.size() && visible_ascii(text[end]))
            ++end;
        const std::string_view run = text.substr(i, end - i);
        if (!IsCredentialLikeAsciiRun(run))
            result.append(run);
        i = end;
    }
    return result;
}
} // namespace FanyImeIpc
