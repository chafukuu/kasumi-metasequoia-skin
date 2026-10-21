#pragma once

#include <cstddef>

// 双拼直接辅助码（万象式，规则见 engine/direct_helpcode/spelling_graph.h）的按键形状。
//
// 直接辅助码不要引导键：辅码字母就是普通字母键，TSF 本来就吃。要额外判断的只有两个键：
//
//   /  四码（双拼 + 两位辅码）后面的终止键：uiab/。不当编码键时它是「上屏高亮候选 + /」。
//   ;  微软/搜狗等双拼的 ing 韵母。平时按「所在一节是奇数键」判断，可辅码会打乱奇偶（uia 后面
//      接 x; 时这一节是偶数键），开着直接辅助码时改成只看前一个键是不是字母。
//
// TSF 在同步吃键阶段先判断，Server 随后按同一条规则决定收不收、引擎按它解析。三处必须一致，否则
// 两边的输入串会分叉。TSF 不链接引擎、没有音节表，所以这里只看形状，/ 到底凑不凑得成四码由引擎
// 判断，凑不成时引擎把它当分隔符处理，不会让整串作废。
//
// 四码也可以用第二位辅码大写来结束（uiaB，设置里和 / 各一个勾）。大写字母本来就是编码键，TSF 不用
// 另外判断；但 / 没勾时 TSF 不能再吃它，所以 DirectHelpcodeChanged 的载荷要带上 / 勾没勾。
namespace FanyImeDirectHelpcode
{
inline constexpr char kSlash = '/';

// DirectHelpcodeChanged 的载荷（一个字符）："0" 关，"1" 开且 / 是四码的终止键，"2" 开但 / 不是编码键。
inline constexpr wchar_t PayloadFor(bool enabled, bool slash)
{
    return !enabled ? L'0' : slash ? L'1' : L'2';
}

inline constexpr bool IsValidPayload(wchar_t payload)
{
    return payload == L'0' || payload == L'1' || payload == L'2';
}

inline constexpr bool EnabledFromPayload(wchar_t payload)
{
    return payload == L'1' || payload == L'2';
}

inline constexpr bool SlashFromPayload(wchar_t payload)
{
    return payload == L'1';
}

template <typename Char> constexpr bool IsAsciiLetter(Char ch)
{
    return (ch >= Char('a') && ch <= Char('z')) || (ch >= Char('A') && ch <= Char('Z'));
}

// 光标停在 text[caret] 时能否插入 /：光标前是「两键音节 + 两个辅码字母」的形状（音节第二键可以
// 是 ; 韵母），且光标后不是已经有一个 /。
template <typename Char> constexpr bool AcceptsSlashAt(const Char *text, std::size_t size, std::size_t caret)
{
    if (caret < 4 || caret > size || (caret < size && text[caret] == Char(kSlash)))
    {
        return false;
    }
    return IsAsciiLetter(text[caret - 1]) && IsAsciiLetter(text[caret - 2]) &&
           (IsAsciiLetter(text[caret - 3]) || text[caret - 3] == Char(';')) && IsAsciiLetter(text[caret - 4]);
}

// 光标停在 text[caret] 时 ; 能否作为 ing 韵母（只对用 ; 当韵母的双拼方案有意义）。
template <typename Char> constexpr bool AcceptsSemicolonFinalAt(const Char *text, std::size_t size, std::size_t caret)
{
    return caret >= 1 && caret <= size && IsAsciiLetter(text[caret - 1]);
}
} // namespace FanyImeDirectHelpcode
