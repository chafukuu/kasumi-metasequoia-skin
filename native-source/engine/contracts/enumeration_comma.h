#pragma once

// 中文标点状态下哪些键输出顿号「、」：反斜杠 \ 和斜杠 /，设置里各一个勾、至少留一个。没勾的键不再是
// 中文标点，按原样输出。Server 把这组勾通过 EnumerationCommaKeysChanged 发给 TSF，TSF 据此决定吃不吃键、
// 换成什么字。
namespace FanyImeEnumerationComma
{
inline constexpr unsigned kBackslash = 1;
inline constexpr unsigned kSlash = 2;
// 旧 Server 不发这个消息，TSF 保持历史行为：只有 \ 出顿号。
inline constexpr unsigned kLegacyKeys = kBackslash;

// EnumerationCommaKeysChanged 的载荷（一个字符）：'1' 只有 \，'2' 只有 /，'3' 两个都是。两个都没勾不是
// 合法配置，按 '1' 发，和 TSF 的历史行为一致。
inline constexpr wchar_t PayloadFor(bool backslash, bool slash)
{
    const unsigned keys = (backslash ? kBackslash : 0u) | (slash ? kSlash : 0u);
    return static_cast<wchar_t>(L'0' + (keys == 0 ? kLegacyKeys : keys));
}

inline constexpr bool IsValidPayload(wchar_t payload)
{
    return payload == L'1' || payload == L'2' || payload == L'3';
}

inline constexpr unsigned KeysFromPayload(wchar_t payload)
{
    return IsValidPayload(payload) ? static_cast<unsigned>(payload - L'0') : kLegacyKeys;
}
} // namespace FanyImeEnumerationComma
