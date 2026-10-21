#pragma once

#include "global/globals.h"
#include "ipc/candidate_selection_policy.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace FanyImeIpc
{
// 这些来源只活在界面列表里，会话扩展重建不出来。
inline bool IsUiOnlyCandidateSource(CandidateSource source)
{
    switch (source)
    {
    case CandidateSource::CloudSuggestion:
    case CandidateSource::AiSuggestion:
    case CandidateSource::EnglishDictionary:
    case CandidateSource::Emoji:
    case CandidateSource::Kaomoji:
    case CandidateSource::Symbol:
    case CandidateSource::DateTime:
        return true;
    default:
        return false;
    }
}

// Session expansion rebuilds local candidates; asynchronous results only live in the UI list.
// Quick phrases are rebuilt by the caller, so carry over only asynchronous sources, deduplicating
// against the expanded list before applying the same ordering policy as worker callbacks.
// Returns false when the list did not change.
inline bool SetExpandedCandidatesKeepingPagePosition(Global::CandidateUiState &ui, std::vector<WordItem> expanded,
                                                     const EnglishPlacement &english_placement = {})
{
    if (ui.selection_started)
    {
        // 翻页扩展只追加尚未显示的候选。引擎可能已经有新的重排结果，不能挪动用户正在选的项。
        // 扩展结果是整份本地候选，按「词 + 拼音 + 来源」逐条抵掉已显示的那些：同字不同音、
        // 快捷短语与词库同字都是各自独立的候选，不能只按字去重。界面独有的异步候选扩展里
        // 重建不出来，和非选词路径一样按字挡掉与它同字的本地候选。
        const auto identity = [](const WordItem &item) {
            return item.word + '\x1f' + item.pinyin + '\x1f' + std::to_string(static_cast<int>(item.source));
        };
        std::unordered_map<std::string, int> shown;
        std::unordered_set<std::string> ui_only_words;
        for (const auto &candidate : ui.items)
        {
            ++shown[identity(candidate)];
            if (IsUiOnlyCandidateSource(candidate.source))
                ui_only_words.insert(candidate.word);
        }
        const size_t size_before = ui.items.size();
        for (auto &candidate : expanded)
        {
            const auto it = shown.find(identity(candidate));
            if (it != shown.end() && it->second > 0)
            {
                --it->second;
                continue;
            }
            if (ui_only_words.count(candidate.word) != 0)
                continue;
            ui.items.push_back(std::move(candidate));
        }
        if (ui.items.size() == size_before)
            return false;
        ui.item_total_count = static_cast<int>(ui.items.size());
        ui.clear_page();
        return true;
    }
    for (const auto &candidate : ui.items)
    {
        if (IsUiOnlyCandidateSource(candidate.source) &&
            std::none_of(expanded.begin(), expanded.end(),
                         [&](const WordItem &item) { return item.word == candidate.word; }))
            expanded.push_back(candidate);
    }
    NormalizeMixedCandidateOrder(expanded, 1, english_placement);
    const int current_page = ui.page_index;
    const int current_selection = ui.selected_index_in_page;
    ui.set_items(std::move(expanded));
    ui.page_index = current_page;
    ui.selected_index_in_page = current_selection;
    return true;
}
} // namespace FanyImeIpc
