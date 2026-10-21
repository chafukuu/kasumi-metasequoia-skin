#include "tests/includes/test_framework.h"
#include "src/ipc/candidate_selection_policy.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
std::vector<WordItem> MixedEnglishItems()
{
    std::vector<WordItem> items;
    for (int index = 0; index < 12; ++index)
        items.emplace_back("work", "中文" + std::to_string(index), 100 - index);
    for (const char *word : {"work", "works", "worked", "working", "workplace"})
        items.emplace_back(word, word, 0, CandidateSource::EnglishDictionary);
    return items;
}

size_t EnglishOnFirstPage(const std::vector<WordItem> &items, size_t page_size)
{
    return static_cast<size_t>(
        std::count_if(items.begin(), items.begin() + (std::min)(items.size(), page_size),
                      [](const WordItem &item) { return item.source == CandidateSource::EnglishDictionary; }));
}
} // namespace

TEST_CASE(MixedEnglishFirstPageCountPreservesTheExistingSingleCandidateDefault)
{
    auto items = MixedEnglishItems();
    FanyImeIpc::EnglishPlacement placement;
    placement.page_size = 6;
    FanyImeIpc::NormalizeMixedCandidateOrder(items, 1, placement);
    REQUIRE_EQ(EnglishOnFirstPage(items, 6), size_t{1});
    REQUIRE_EQ(items.front().word, std::string("中文0"));
    REQUIRE_EQ(items[1].word, std::string("work"));
}

TEST_CASE(MixedEnglishFirstPageCountShowsRealInflectionsWithoutRemovingOtherCandidates)
{
    for (size_t count : {size_t{2}, size_t{3}})
    {
        auto items = MixedEnglishItems();
        const size_t original_size = items.size();
        FanyImeIpc::EnglishPlacement placement;
        placement.page_size = 6;
        placement.first_page_count = count;
        placement.require_exact = true;
        placement.input = "wor"; // 中文可解析的前缀，但没有完全相同的英文词。
        FanyImeIpc::NormalizeMixedCandidateOrder(items, 1, placement);
        REQUIRE_EQ(EnglishOnFirstPage(items, 6), count);
        REQUIRE_EQ(items.size(), original_size);
        REQUIRE_EQ(items.front().word, std::string("中文0"));
        REQUIRE_EQ(items[6 - count].word, std::string("work"));
        REQUIRE_EQ(items[7 - count].word, std::string("works"));
        const auto normalized = items;
        FanyImeIpc::NormalizeMixedCandidateOrder(items, 1, placement);
        REQUIRE_EQ(items.size(), normalized.size());
        for (size_t index = 0; index < items.size(); ++index)
            REQUIRE_EQ(items[index].word, normalized[index].word);
    }
}

TEST_CASE(MixedEnglishFirstPageCountReservesEmojiAndClampsLearnedSlots)
{
    auto items = MixedEnglishItems();
    items.emplace_back("work", "😀", 0, CandidateSource::Emoji);
    FanyImeIpc::EnglishPlacement placement;
    placement.page_size = 6;
    placement.first_page_count = 3;
    placement.slot = 5;
    FanyImeIpc::NormalizeMixedCandidateOrder(items, 1, placement);
    REQUIRE_EQ(items.front().word, std::string("中文0"));
    REQUIRE_EQ(items[2].word, std::string("work"));
    REQUIRE_EQ(items[3].word, std::string("works"));
    REQUIRE_EQ(items[4].word, std::string("worked"));
    REQUIRE_EQ(items[5].source, CandidateSource::Emoji);
}

TEST_CASE(MixedEnglishFirstPageCountHonorsSmallPagesAndExplicitFixedPositions)
{
    for (size_t page_size : {size_t{1}, size_t{2}, size_t{3}})
    {
        auto items = MixedEnglishItems();
        FanyImeIpc::EnglishPlacement placement;
        placement.page_size = page_size;
        placement.first_page_count = 3;
        FanyImeIpc::NormalizeMixedCandidateOrder(items, 1, placement);
        REQUIRE(EnglishOnFirstPage(items, page_size) <= (std::max)(size_t{1}, page_size - 1));
        if (page_size > 1)
            REQUIRE_EQ(items.front().word, std::string("中文0"));
    }
    auto items = MixedEnglishItems();
    items.back().fixed_position = 4;
    FanyImeIpc::EnglishPlacement placement;
    placement.page_size = 6;
    placement.first_page_count = 3;
    FanyImeIpc::NormalizeMixedCandidateOrder(items, 1, placement);
    REQUIRE_EQ(items[3].word, std::string("workplace"));
    const auto normalized = items;
    FanyImeIpc::NormalizeMixedCandidateOrder(items, 1, placement);
    for (size_t index = 0; index < items.size(); ++index)
        REQUIRE_EQ(items[index].word, normalized[index].word);
}

TEST_CASE(MixedEnglishFirstPageCountSharesThePageWithCloudAiAndEmoji)
{
    auto items = MixedEnglishItems();
    items.emplace_back("work", "云", 0, CandidateSource::CloudSuggestion);
    items.emplace_back("work", "AI", 0, CandidateSource::AiSuggestion);
    items.emplace_back("work", "😀", 0, CandidateSource::Emoji);
    FanyImeIpc::EnglishPlacement placement;
    placement.page_size = 6;
    placement.first_page_count = 3;
    FanyImeIpc::NormalizeMixedCandidateOrder(items, 1, placement);
    REQUIRE_EQ(items.front().word, std::string("中文0"));
    REQUIRE_EQ(items[1].source, CandidateSource::CloudSuggestion);
    REQUIRE_EQ(EnglishOnFirstPage(items, 6), size_t{2});
    REQUIRE_EQ(items[2].source, CandidateSource::AiSuggestion);
    REQUIRE_EQ(items[5].source, CandidateSource::Emoji);
    REQUIRE_EQ(std::count_if(items.begin(), items.end(),
                             [](const WordItem &item) { return item.source == CandidateSource::AiSuggestion; }),
               1);
    const auto normalized = items;
    FanyImeIpc::NormalizeMixedCandidateOrder(items, 1, placement);
    for (size_t index = 0; index < items.size(); ++index)
        REQUIRE_EQ(items[index].word, normalized[index].word);
}

TEST_CASE(MixedEnglishFirstPageCountDoesNotSplitQuickPhraseGroupsOrDisplaceTheChineseHead)
{
    for (size_t phrases : {size_t{3}, size_t{4}})
    {
        auto items = MixedEnglishItems();
        for (size_t index = 0; index < phrases; ++index)
            items.insert(items.begin() + static_cast<std::ptrdiff_t>(index),
                         WordItem("work", "短语" + std::to_string(index), 0, CandidateSource::QuickPhrase));
        FanyImeIpc::EnglishPlacement placement;
        placement.page_size = 6;
        placement.first_page_count = 3;
        FanyImeIpc::NormalizeMixedCandidateOrder(items, 1, placement);
        for (size_t index = 0; index < phrases; ++index)
            REQUIRE_EQ(items[index].source, CandidateSource::QuickPhrase);
        REQUIRE_EQ(items[phrases].word, std::string("中文0"));
        REQUIRE_EQ(EnglishOnFirstPage(items, 6), size_t{5} - phrases);
    }
}

TEST_CASE(MixedEnglishFirstPageCountIncludesFixedEnglishAndKeepsTheEmojiSlot)
{
    auto items = MixedEnglishItems();
    items.back().fixed_position = 2;
    items.emplace_back("work", "😀", 0, CandidateSource::Emoji);
    FanyImeIpc::EnglishPlacement placement;
    placement.page_size = 6;
    placement.first_page_count = 3;
    FanyImeIpc::NormalizeMixedCandidateOrder(items, 1, placement);
    REQUIRE_EQ(EnglishOnFirstPage(items, 6), size_t{3});
    REQUIRE_EQ(items.front().word, std::string("中文0"));
    REQUIRE_EQ(items[1].word, std::string("workplace"));
    REQUIRE_EQ(items[5].source, CandidateSource::Emoji);
}

TEST_CASE(MixedEnglishFixedFirstWordLeavesTheSlotToTheNextUnfixedWord)
{
    auto items = MixedEnglishItems();
    items[12].fixed_position = 1; // 词库排第一的 work
    FanyImeIpc::EnglishPlacement placement;
    placement.page_size = 6;
    placement.first_page_count = 2;
    FanyImeIpc::NormalizeMixedCandidateOrder(items, 1, placement);
    REQUIRE_EQ(items.front().word, std::string("work"));
    REQUIRE_EQ(EnglishOnFirstPage(items, 6), size_t{2});
    const auto slotted = FanyImeIpc::SlottedEnglishIndex(items);
    REQUIRE(slotted.has_value());
    REQUIRE_EQ(items[*slotted].word, std::string("works"));
}

TEST_CASE(MixedEnglishSingleCompletionStillMovesAheadOfAsyncCandidates)
{
    auto items = MixedEnglishItems();
    items.emplace_back("work", "日期", 0, CandidateSource::DateTime);
    items.emplace_back("work", "云", 0, CandidateSource::CloudSuggestion);
    items.emplace_back("work", "AI", 0, CandidateSource::AiSuggestion);
    items.emplace_back("work", "😀", 0, CandidateSource::Emoji);
    FanyImeIpc::EnglishPlacement placement;
    placement.page_size = 5;
    placement.require_exact = true;
    placement.input = "wor";
    FanyImeIpc::NormalizeMixedCandidateOrder(items, 1, placement);
    REQUIRE_EQ(items[3].word, std::string("work"));
    REQUIRE_EQ(items[4].source, CandidateSource::Emoji);
}

TEST_CASE(MixedEnglishLearnedSlotDoesNotPushCloudOrAiOffTheFirstPage)
{
    auto items = MixedEnglishItems();
    items.emplace_back("work", "云", 0, CandidateSource::CloudSuggestion);
    items.emplace_back("work", "AI", 0, CandidateSource::AiSuggestion);
    items.emplace_back("work", "😀", 0, CandidateSource::Emoji);
    FanyImeIpc::EnglishPlacement placement;
    placement.page_size = 6;
    placement.first_page_count = 3;
    placement.slot = 1;
    FanyImeIpc::NormalizeMixedCandidateOrder(items, 1, placement);
    REQUIRE_EQ(items.front().word, std::string("中文0"));
    REQUIRE_EQ(items[1].word, std::string("work"));
    const auto on_first_page = [&](CandidateSource source) {
        return std::any_of(items.begin(), items.begin() + 6,
                           [&](const WordItem &item) { return item.source == source; });
    };
    REQUIRE(on_first_page(CandidateSource::CloudSuggestion));
    REQUIRE(on_first_page(CandidateSource::AiSuggestion));
    REQUIRE_EQ(items[5].source, CandidateSource::Emoji);
}

TEST_CASE(MixedEnglishFirstPageCountUsesDifferentWordsRatherThanCaseVariants)
{
    auto items = MixedEnglishItems();
    items.insert(items.begin() + 13, WordItem("work", "Work", 0, CandidateSource::EnglishDictionary));
    FanyImeIpc::EnglishPlacement placement;
    placement.page_size = 6;
    placement.first_page_count = 3;
    FanyImeIpc::NormalizeMixedCandidateOrder(items, 1, placement);
    REQUIRE_EQ(items[1].word, std::string("work"));
    REQUIRE_EQ(items[2].word, std::string("works"));
    REQUIRE_EQ(items[3].word, std::string("worked"));
    REQUIRE_EQ(std::count_if(items.begin(), items.end(), [](const WordItem &item) { return item.word == "Work"; }), 1);
}
