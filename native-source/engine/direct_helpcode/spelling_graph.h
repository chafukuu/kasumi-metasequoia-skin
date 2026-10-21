#pragma once

#include "../shuangpin/shuangpin_profile.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

// 双拼直接辅助码（万象「直接辅助」）的拼写图。
//
// 万象把每个字的辅码写进词库音节（shí;dk），再用拼写运算派生出可敲的形式，librime 在按键位置上
// 把所有能匹配的拼写连成图、交给词格整句解码。这里在不改词库的前提下做同一件事：每个位置上
// 一个音节可以写成
//
//   ui      双拼两键                          Plain
//   uia     双拼 + 第一位辅码                  Aux1
//   uiab/   双拼 + 两位辅码 + /                Aux2Slash   SpellingOptions::slash_marker
//   uiaB    双拼 + 两位辅码，第二位大写         Aux2Upper   SpellingOptions::uppercase_marker
//   uiab    双拼 + 两位辅码（不带标记）         Aux2Abbrev  缩写，见下
//   u       单个声母键                        Initial     缩写，见下
//
// 辅码那一段只有在「这个音节下真有字的辅码以它开头」时才成立（AuxPredicate），这对应万象词库里
// 只为真实存在的 拼音;辅码 派生拼写。辅码不分大小写，和万象的大写变体一样。
//
// 缩写（Aux2Abbrev、Initial）对应 librime 的 kAbbreviation：只要存在一条全由非缩写拼写组成、
// 走到输入末尾的路径，所有缩写边就被剪掉（syllabifier.cc 的 stale edge 清理）。所以 uiab 单独
// 敲时能当一个带两位辅码的字，放进句子里就会让位给 ui'ab 这类正常切分，要在句中用四码必须
// 补 /（或开着大写标记时把第二位辅码敲成大写）。这正是万象的行为，不是这里的简化。
//
// 开着大写标记时大写字母只能当四码的第二位辅码：别处出现大写字母的切法先不考虑，四码因此和补 /
// 一样确定。一条都走不通时（比如习惯把第一位辅码也敲成大写）再退回不分大小写的旧规则。
namespace direct_helpcode
{

enum class SpellingKind
{
    Plain,
    Aux1,
    Aux2Slash,
    Aux2Upper,
    Aux2Abbrev,
    Initial,
};

struct SyllableSpelling
{
    // 在输入串里的范围 [begin, end)，含辅码与 /。
    std::size_t begin = 0;
    std::size_t end = 0;
    // 小写的双拼码：两键，Initial 是一键。
    std::string code;
    // 全拼音节；Initial 是声母。
    std::string quanpin;
    // 小写辅码，0 表示没有。
    char first = 0;
    char second = 0;
    SpellingKind kind = SpellingKind::Plain;

    bool is_abbreviation() const
    {
        return kind == SpellingKind::Aux2Abbrev || kind == SpellingKind::Initial;
    }
    bool has_aux() const
    {
        return first != 0;
    }
};

// 全拼音节 quanpin 下有没有字的辅码以 first（second 非 0 时为 first+second）开头。
using AuxPredicate = std::function<bool(const std::string &quanpin, char first, char second)>;

struct SpellingGraph
{
    std::size_t size = 0;
    // edges[pos]：从 pos 开始的拼写。只保留落在某条从 0 到 size 的完整路径上的边。
    std::vector<std::vector<SyllableSpelling>> edges;
    // skip[pos]：pos 上是分隔符，不出字，直接走到 pos + 1。手动分隔符 ' 总是；/ 与 ; 只在
    // 没有它们就切不通时才当分隔符兜底。
    std::vector<bool> skip;

    bool empty() const
    {
        return edges.empty();
    }
};

// 句中四码用什么结束，可以两个都开。
struct SpellingOptions
{
    // uiab/：两位辅码后补 /。
    bool slash_marker = true;
    // uiaB：第二位辅码敲成大写。
    bool uppercase_marker = false;

    bool operator==(const SpellingOptions &other) const
    {
        return slash_marker == other.slash_marker && uppercase_marker == other.uppercase_marker;
    }
    bool operator!=(const SpellingOptions &other) const
    {
        return !(*this == other);
    }
};

// 走不通时返回空图。
SpellingGraph build_spelling_graph(const std::string &typed, const ShuangpinProfile &profile,
                                   const AuxPredicate &has_aux, const SpellingOptions &options = {});

// 下一个出字的位置：跳过 pos 起连续的分隔符。
std::size_t next_spelling_position(const SpellingGraph &graph, std::size_t pos);

// 图里只有一条完整切分时返回它：没有可比的切法，不必整句解码。有零条或多条时返回 false。
bool single_path(const SpellingGraph &graph, std::vector<SyllableSpelling> &path);

} // namespace direct_helpcode
