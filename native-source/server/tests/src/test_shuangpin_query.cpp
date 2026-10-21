#include "tests/includes/test_framework.h"
#include "engine/core/ime_session.h"
#include "engine/shuangpin/shuangpin_query.h"
#include "engine/shuangpin/shuangpin_utils.h"
#include <algorithm>

TEST_CASE(ApplySegmentationCasesPreservesUppercaseMarkers)
{
    const std::string segmented = "xi'te'le'aa";
    const std::string cased = "xiteleaA";
    REQUIRE_EQ(shuangpin::apply_segmentation_cases(segmented, cased), std::string("xi'te'le'aA"));
}

TEST_CASE(IsFullHelpModeRecognizesDoubleHelpcodePattern)
{
    REQUIRE(ShuangpinUtil::IsFullHelpMode("xiteleaA"));
    REQUIRE(ShuangpinUtil::IsFullHelpMode("xiteleAa"));
    REQUIRE(ShuangpinUtil::IsFullHelpMode("xiteleAA"));
    REQUIRE(!ShuangpinUtil::IsFullHelpMode("xiteleaa"));
    REQUIRE(!ShuangpinUtil::IsFullHelpMode("xitelea"));
    REQUIRE(!ShuangpinUtil::IsFullHelpMode("xi"));
}

TEST_CASE(ActiveDoubleHelpcodeDetectionKeepsManualSegmentsDistinct)
{
    REQUIRE_EQ(shuangpin::detect_active_double_helpcode_length("yakp", "yakP"), static_cast<size_t>(2));
    REQUIRE_EQ(shuangpin::detect_active_double_helpcode_length("yakp", "yaKp"), static_cast<size_t>(2));
    REQUIRE_EQ(shuangpin::detect_active_double_helpcode_length("yakp", "yakp"), static_cast<size_t>(0));
    REQUIRE_EQ(shuangpin::detect_active_double_helpcode_length("ya'kp", "ya'kP"), static_cast<size_t>(0));
}

TEST_CASE(FullHelpCodesUseFirstUppercaseMarkerForReverseOrder)
{
    REQUIRE_EQ(ShuangpinUtil::GetFullHelpCodes("xiteleaB"), std::string("ab"));
    REQUIRE_EQ(ShuangpinUtil::GetFullHelpCodes("xiteleAb"), std::string("ba"));
    REQUIRE_EQ(ShuangpinUtil::GetFullHelpCodes("xiteleAB"), std::string("ba"));
}

TEST_CASE(DoubleHelpcodeCacheKeepsBothQueryOrdersDistinct)
{
    for (const bool reversed_first : {false, true})
    {
        ImeSession session(SchemeType::Shuangpin);
        session.set_shuangpin_helpcode_enabled(true);
        session.set_helpcode_keymap(
            HelpcodeUtils::load_helpcode_keymap(metasequoia::RuntimePaths::legacy().resources, "xiaohe"));
        for (const bool reversed : {reversed_first, !reversed_first})
        {
            session.reset();
            session.replace_shuangpin_raw_input("gcwk", reversed ? "gcWk" : "gcwK");
            const auto &candidates = session.get_candidates();
            const bool has_gao =
                std::any_of(candidates.begin(), candidates.end(), [](const auto &item) { return item.word == "高"; });
            REQUIRE_EQ(has_gao, !reversed);
        }
    }
}

TEST_CASE(DynamicCandidateUpdatesOnlyTheActiveDoubleHelpcodeCache)
{
    ImeSession session(SchemeType::Shuangpin);
    session.set_shuangpin_helpcode_enabled(true);
    session.set_helpcode_keymap(
        HelpcodeUtils::load_helpcode_keymap(metasequoia::RuntimePaths::legacy().resources, "xiaohe"));
    session.replace_shuangpin_raw_input("gcwk", "gcWk");
    session.reset();
    session.replace_shuangpin_raw_input("gcwk", "gcwK");
    REQUIRE_EQ(session.cache_dynamic_candidate_for_current_request("缓存回归词", CandidateSource::CloudSuggestion), 0);

    session.reset();
    session.replace_shuangpin_raw_input("gcwk", "gcwK");
    const auto &candidates = session.get_candidates();
    REQUIRE(std::any_of(candidates.begin(), candidates.end(), [](const auto &item) {
        return item.word == "缓存回归词" && item.pinyin == "gcwk" && item.source == CandidateSource::CloudSuggestion;
    }));

    session.reset();
    session.replace_shuangpin_raw_input("gcwk", "gcWk");
    const auto &reversed_candidates = session.get_candidates();
    REQUIRE(std::none_of(reversed_candidates.begin(), reversed_candidates.end(),
                         [](const auto &item) { return item.word == "缓存回归词"; }));
}
