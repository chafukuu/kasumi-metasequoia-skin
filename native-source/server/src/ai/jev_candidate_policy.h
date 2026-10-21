#pragma once

#include "engine/core/word_item.h"
#include "engine/core/input_session_types.h"
#include "engine/common/helpcode_utils.h"
#include "jev_client.h"
#include <algorithm>

namespace Jev
{
inline bool SameQuery(const metasequoia::OnlineQuery &a, const metasequoia::OnlineQuery &b)
{
    return a.session_id == b.session_id && a.generation == b.generation && a.scheme == b.scheme &&
           a.identity == b.identity && a.query_text == b.query_text && a.cache_key == b.cache_key &&
           a.pinyin_segments == b.pinyin_segments && a.ai_eligible && b.ai_eligible;
}
// Only local dictionary and model candidates are offered to Jev or moved by its choice.
inline bool IsPoolSource(const WordItem &item)
{
    return item.source == CandidateSource::Database || item.source == CandidateSource::UserDatabase ||
           item.sentence_association || item.source == CandidateSource::NeuralDesktop ||
           item.source == CandidateSource::NeuralKeyboard || item.source == CandidateSource::Collocation;
}

inline std::vector<std::string> CandidatePool(const std::vector<WordItem> &items, size_t syllables)
{
    std::vector<std::string> result;
    if (syllables == 0)
        return result;
    for (const auto &item : items)
    {
        if (!IsPoolSource(item) || HelpcodeUtils::count_han_chars(item.word) != syllables ||
            HelpcodeUtils::count_utf8_chars(item.word) != syllables ||
            std::find(result.begin(), result.end(), item.word) != result.end())
            continue;
        result.push_back(item.word);
        if (result.size() == kMaxCandidates)
            break;
    }
    return result;
}

// Only the slots held by unpinned pool candidates are reordered, so cloud, English, date, emoji and
// user-pinned rows keep the positions NormalizeMixedCandidateOrder gave them. Within those slots
// the local first/second choice stay first and the choice becomes the third. Preserve the full
// WordItem: pinyin, provenance and dictionary learning still belong to the original candidate.
inline bool Promote(std::vector<WordItem> &items, const std::string &word)
{
    std::vector<size_t> slots;
    for (size_t i = 0; i < items.size(); ++i)
        if (IsPoolSource(items[i]) && items[i].fixed_position == 0)
            slots.push_back(i);
    const auto selected = std::find_if(slots.begin(), slots.end(), [&](size_t i) { return items[i].word == word; });
    if (selected == slots.end())
        return false;
    for (auto rank = static_cast<size_t>(selected - slots.begin()); rank > 2; --rank)
        std::swap(items[slots[rank]], items[slots[rank - 1]]);
    return true;
}
} // namespace Jev
