#pragma once

#include <cstddef>

// Shift+T 日期时间模式的输入形状。T 后面跟小写字母是唤醒词（rq / sj / xq，取此刻的日期时间），跟数字是
// 指定的日期时间：
//
//   YYYYMMDD                 日期                          20241225
//   YYYYMMDDhh[:mm[:ss]]     日期加时间，只写 hh 时分钟取 00  2024122514:30
//   YYYY/M[M]/D[D]           日期                          2024/12/25
//   YYYY/M[M]                年月                          2024/12
//   M[M]/D[D]                今年的某月某日                 12/25
//   h[h]:mm[:ss]             时间                          9:05、14:30:00
//
// 数字、/ 和 : 只在还能接成上面某一种形状时才是编码键：接不上的数字仍是选词键，接不上的 / 和 : 仍按标点
// 处理，Shift+数字始终选词。TSF 在同步吃键阶段按这里判断，Server 按同一条规则改输入串，引擎按它解析，三处
// 必须一致，否则两边的输入串会分叉。TSF 看不到配置，所以这里只认开头的 T，不管 T 模式开没开。
//
// 这里只看形状：月份、日期、时分秒的取值由引擎检查，取值不对时不出候选。
namespace FanyImeDateTimeInput
{
inline constexpr char kModePrefix = 'T';
// 最长的形状是 YYYYMMDDhh:mm:ss。
inline constexpr std::size_t kMaxBodyLength = 16;
inline constexpr std::size_t kMaxGroups = 3;

enum class Shape
{
    None,
    Date,         // YYYYMMDD
    DateTime,     // YYYYMMDDhh[:mm[:ss]]
    YearMonthDay, // YYYY/M/D
    YearMonth,    // YYYY/M
    MonthDay,     // M/D
    Time,         // h:mm[:ss]
};

// 被分隔符切开的一段数字：在 T 后面那一串里的起点和长度。
struct Group
{
    std::size_t start = 0;
    std::size_t length = 0;
};

struct Match
{
    Shape shape = Shape::None;
    std::size_t group_count = 0;
    Group groups[kMaxGroups] = {};
};

namespace detail
{
template <typename Char> constexpr bool IsDigit(Char ch)
{
    return ch >= Char('0') && ch <= Char('9');
}

struct Pattern
{
    char separator;                     // 段与段之间的分隔符，只有一段时用不上
    std::size_t max_groups;             // 最多几段
    std::size_t min_complete_groups;    // 至少几段才算写完
    std::size_t min_length[kMaxGroups]; // 每段至少几位
    std::size_t max_length[kMaxGroups]; // 每段至多几位
    Shape shape_by_groups[kMaxGroups];  // 写完时按段数给出的形状
};

// 顺序就是写完时的认定顺序：20241225 同时是 YYYYMMDD 写完和 YYYYMMDDhh 没写完，按前者算。
inline constexpr Pattern kPatterns[] = {
    {'\0', 1, 1, {8, 0, 0}, {8, 0, 0}, {Shape::Date, Shape::None, Shape::None}},
    {':', 3, 1, {10, 2, 2}, {10, 2, 2}, {Shape::DateTime, Shape::DateTime, Shape::DateTime}},
    {'/', 3, 2, {4, 1, 1}, {4, 2, 2}, {Shape::None, Shape::YearMonth, Shape::YearMonthDay}},
    {'/', 2, 2, {1, 1, 0}, {2, 2, 0}, {Shape::None, Shape::MonthDay, Shape::None}},
    {':', 3, 2, {1, 2, 2}, {2, 2, 2}, {Shape::None, Shape::Time, Shape::Time}},
};

// 把 T 后面那一串切成数字段。只能含数字、/ 和 :，最多三段；有多个分隔符时必须是同一种。
template <typename Char>
constexpr bool Split(const Char *body, std::size_t size, Group (&groups)[kMaxGroups], std::size_t &count,
                     char &separator)
{
    count = 1;
    separator = '\0';
    groups[0] = {0, 0};
    for (std::size_t index = 0; index < size; ++index)
    {
        const Char ch = body[index];
        if (IsDigit(ch))
        {
            ++groups[count - 1].length;
            continue;
        }
        if (ch != Char('/') && ch != Char(':'))
        {
            return false;
        }
        const char current = ch == Char('/') ? '/' : ':';
        if ((separator != '\0' && current != separator) || count == kMaxGroups)
        {
            return false;
        }
        separator = current;
        groups[count] = {index + 1, 0};
        ++count;
    }
    return true;
}

// complete 为 false 时只要求还能接成这个形状：最后一段可以没写完，紧跟在分隔符后面时甚至可以是空的。
constexpr bool Fits(const Pattern &pattern, const Group (&groups)[kMaxGroups], std::size_t count, char separator,
                    bool complete)
{
    if (count > pattern.max_groups || (count > 1 && separator != pattern.separator) ||
        (complete && count < pattern.min_complete_groups))
    {
        return false;
    }
    for (std::size_t index = 0; index < count; ++index)
    {
        const std::size_t length = groups[index].length;
        const bool last = index + 1 == count;
        if (length > pattern.max_length[index] || (length < pattern.min_length[index] && (complete || !last)))
        {
            return false;
        }
    }
    return true;
}
} // namespace detail

// T 后面那一串是否以数字开头，即按指定日期时间而不是唤醒词处理。
template <typename Char> constexpr bool IsSpecificBody(const Char *body, std::size_t size)
{
    return body != nullptr && size > 0 && detail::IsDigit(body[0]);
}

// T 后面那一串写完了时是哪种形状，各段在哪里；没写完或不是这些形状时 shape 为 None。
template <typename Char> constexpr Match MatchComplete(const Char *body, std::size_t size)
{
    Match match;
    char separator = '\0';
    if (body == nullptr || size == 0 || size > kMaxBodyLength ||
        !detail::Split(body, size, match.groups, match.group_count, separator))
    {
        return Match{};
    }
    for (const detail::Pattern &pattern : detail::kPatterns)
    {
        if (detail::Fits(pattern, match.groups, match.group_count, separator, true))
        {
            match.shape = pattern.shape_by_groups[match.group_count - 1];
            return match;
        }
    }
    return Match{};
}

// T 后面那一串是否还能接成某一种形状（空串也算：刚进 T 模式）。
template <typename Char> constexpr bool IsViablePrefix(const Char *body, std::size_t size)
{
    if (size == 0)
    {
        return true;
    }
    Group groups[kMaxGroups] = {};
    std::size_t count = 0;
    char separator = '\0';
    if (body == nullptr || size > kMaxBodyLength || !detail::Split(body, size, groups, count, separator))
    {
        return false;
    }
    for (const detail::Pattern &pattern : detail::kPatterns)
    {
        if (detail::Fits(pattern, groups, count, separator, false))
        {
            return true;
        }
    }
    return false;
}

// 整个输入串 text（开头是 T）里，光标停在 text[caret] 时能否插入 ch（数字、/ 或 :）。插入后 T 后面那一串
// 还得能接成某一种形状，所以 Trq 后面不收数字，2024/12/25 后面也不再收。
template <typename Char> constexpr bool AcceptsAt(const Char *text, std::size_t size, std::size_t caret, Char ch)
{
    if (text == nullptr || size == 0 || text[0] != Char(kModePrefix) || caret < 1 || caret > size ||
        size > kMaxBodyLength || (!detail::IsDigit(ch) && ch != Char('/') && ch != Char(':')))
    {
        return false;
    }
    Char body[kMaxBodyLength] = {};
    std::size_t length = 0;
    for (std::size_t index = 1; index <= size; ++index)
    {
        if (index == caret)
        {
            body[length++] = ch;
        }
        if (index < size)
        {
            body[length++] = text[index];
        }
    }
    return IsViablePrefix(body, length);
}
} // namespace FanyImeDateTimeInput
