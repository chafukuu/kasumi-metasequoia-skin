#include "spelling_graph.h"

#include "../shuangpin/shuangpin_utils.h"

#include <algorithm>

namespace direct_helpcode
{
namespace
{
bool is_letter(char ch)
{
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

bool is_upper(char ch)
{
    return ch >= 'A' && ch <= 'Z';
}

char to_lower(char ch)
{
    return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch + ('a' - 'A')) : ch;
}

// 两键是不是一个完整音节；是就返回它的全拼。第二键可以是微软/搜狗双拼的 ; 韵母。
std::string syllable_quanpin(const std::string &code, const ShuangpinProfile &profile)
{
    if (!ShuangpinUtil::is_accepted_syllable_code(code, profile))
        return {};
    return ShuangpinUtil::cvt_single_sp_to_pinyin(code, profile);
}

SyllableSpelling make_spelling(std::size_t begin, std::size_t end, std::string code, std::string quanpin,
                               SpellingKind kind, char first = 0, char second = 0)
{
    SyllableSpelling spelling;
    spelling.begin = begin;
    spelling.end = end;
    spelling.code = std::move(code);
    spelling.quanpin = std::move(quanpin);
    spelling.first = first;
    spelling.second = second;
    spelling.kind = kind;
    return spelling;
}

std::vector<bool> forward_reach(const SpellingGraph &graph, bool normal_only)
{
    std::vector<bool> reach(graph.size + 1, false);
    reach[0] = true;
    for (std::size_t pos = 0; pos < graph.size; ++pos)
    {
        if (!reach[pos])
            continue;
        if (graph.skip[pos])
            reach[pos + 1] = true;
        for (const auto &spelling : graph.edges[pos])
        {
            if (!normal_only || !spelling.is_abbreviation())
                reach[spelling.end] = true;
        }
    }
    return reach;
}

SpellingGraph build_attempt(const std::string &typed, const ShuangpinProfile &profile, const AuxPredicate &has_aux,
                            const SpellingOptions &options, bool lenient)
{
    SpellingGraph graph;
    const std::size_t n = typed.size();
    graph.size = n;
    graph.edges.resize(n);
    graph.skip.resize(n, false);
    // 大写标记开着、又不是兜底那一遍时，大写字母只能当四码的第二位辅码。
    const bool strict_case = options.uppercase_marker && !lenient;
    const auto usable = [&](std::size_t index) { return !strict_case || !is_upper(typed[index]); };
    for (std::size_t pos = 0; pos < n; ++pos)
    {
        const char ch = typed[pos];
        graph.skip[pos] = ch == '\'' || (lenient && (ch == '/' || ch == ';'));
        if (!is_letter(ch) || !usable(pos))
            continue;

        auto &out = graph.edges[pos];
        if (pos + 1 < n && ((is_letter(typed[pos + 1]) && usable(pos + 1)) || typed[pos + 1] == ';'))
        {
            std::string code{to_lower(ch), to_lower(typed[pos + 1])};
            std::string quanpin = syllable_quanpin(code, profile);
            if (!quanpin.empty())
            {
                out.push_back(make_spelling(pos, pos + 2, code, quanpin, SpellingKind::Plain));
                if (pos + 2 < n && is_letter(typed[pos + 2]) && usable(pos + 2))
                {
                    const char first = to_lower(typed[pos + 2]);
                    if (has_aux && has_aux(quanpin, first, 0))
                        out.push_back(make_spelling(pos, pos + 3, code, quanpin, SpellingKind::Aux1, first));
                    if (pos + 3 < n && is_letter(typed[pos + 3]))
                    {
                        const char second = to_lower(typed[pos + 3]);
                        if (has_aux && has_aux(quanpin, first, second))
                        {
                            if (options.slash_marker && pos + 4 < n && typed[pos + 4] == '/')
                                out.push_back(
                                    make_spelling(pos, pos + 5, code, quanpin, SpellingKind::Aux2Slash, first, second));
                            if (options.uppercase_marker && is_upper(typed[pos + 3]))
                                out.push_back(
                                    make_spelling(pos, pos + 4, code, quanpin, SpellingKind::Aux2Upper, first, second));
                            else
                                out.push_back(make_spelling(pos, pos + 4, code, quanpin, SpellingKind::Aux2Abbrev,
                                                            first, second));
                        }
                    }
                }
            }
        }
        const std::string initial(1, to_lower(ch));
        out.push_back(make_spelling(pos, pos + 1, initial,
                                    ShuangpinUtil::convert_seg_shuangpin_to_seg_complete_pinyin(initial, profile),
                                    SpellingKind::Initial));
    }

    // librime：存在一条全由非缩写拼写走到末尾的路径时，缩写边全部作废。
    if (forward_reach(graph, true)[n])
    {
        for (auto &out : graph.edges)
        {
            out.erase(std::remove_if(out.begin(), out.end(),
                                     [](const SyllableSpelling &spelling) { return spelling.is_abbreviation(); }),
                      out.end());
        }
    }

    const auto reach = forward_reach(graph, false);
    if (!reach[n])
        return {};
    // 只留落在完整路径上的边：词格只需要能走到末尾的那些。
    std::vector<bool> to_end(n + 1, false);
    to_end[n] = true;
    for (std::size_t pos = n; pos-- > 0;)
    {
        bool ok = graph.skip[pos] && to_end[pos + 1];
        for (const auto &spelling : graph.edges[pos])
            ok = ok || to_end[spelling.end];
        to_end[pos] = ok;
    }
    for (std::size_t pos = 0; pos < n; ++pos)
    {
        auto &out = graph.edges[pos];
        if (!reach[pos])
        {
            out.clear();
            continue;
        }
        out.erase(std::remove_if(out.begin(), out.end(),
                                 [&](const SyllableSpelling &spelling) { return !to_end[spelling.end]; }),
                  out.end());
    }
    return graph;
}
} // namespace

SpellingGraph build_spelling_graph(const std::string &typed, const ShuangpinProfile &profile,
                                   const AuxPredicate &has_aux, const SpellingOptions &options)
{
    if (typed.empty())
        return {};
    SpellingGraph graph = build_attempt(typed, profile, has_aux, options, false);
    if (graph.empty())
    {
        // 用不上的 / 和 ; 退回成分隔符、大写字母不再只当第二位辅码，候选照常出，不至于整串作废。
        graph = build_attempt(typed, profile, has_aux, options, true);
    }
    return graph;
}

std::size_t next_spelling_position(const SpellingGraph &graph, std::size_t pos)
{
    while (pos < graph.size && graph.skip[pos])
        ++pos;
    return pos;
}

bool single_path(const SpellingGraph &graph, std::vector<SyllableSpelling> &path)
{
    path.clear();
    if (graph.empty())
        return false;
    // 从每个位置走到末尾有几条路，数到 2 就够了。
    const std::size_t n = graph.size;
    std::vector<int> ways(n + 1, 0);
    ways[n] = 1;
    for (std::size_t pos = n; pos-- > 0;)
    {
        int count = graph.skip[pos] ? ways[pos + 1] : 0;
        for (const auto &spelling : graph.edges[pos])
            count += ways[spelling.end];
        ways[pos] = (std::min)(count, 2);
    }
    if (ways[0] != 1)
        return false;
    for (std::size_t pos = 0; pos < n;)
    {
        if (graph.skip[pos])
        {
            ++pos;
            continue;
        }
        const auto next = std::find_if(graph.edges[pos].begin(), graph.edges[pos].end(),
                                       [&](const SyllableSpelling &spelling) { return ways[spelling.end] > 0; });
        if (next == graph.edges[pos].end())
            return false;
        path.push_back(*next);
        pos = next->end;
    }
    return true;
}

} // namespace direct_helpcode
