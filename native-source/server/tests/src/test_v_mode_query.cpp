#include "tests/includes/test_framework.h"
#include "engine/contracts/v_mode_input.h"
#include "engine/local_modes/v_mode_query.h"

#include <string>

// V 模式：V 后接数字转中文，接算式给结果。Server 按原串识别 V 模式、取候选，规则与 TSF 共用。
TEST_CASE(v_mode_converts_numbers_to_chinese)
{
    const auto integer = metasequoia::local_modes::query_v_mode("123");
    REQUIRE_EQ(integer.size(), static_cast<size_t>(4));
    REQUIRE_EQ(integer[0].word, std::string("一百二十三"));
    REQUIRE_EQ(integer[1].word, std::string("壹佰贰拾叁"));
    REQUIRE(integer[0].source == CandidateSource::Generated);

    const auto money = metasequoia::local_modes::query_v_mode("123.45");
    REQUIRE_EQ(money[0].word, std::string("壹佰贰拾叁元肆角伍分"));
    REQUIRE_EQ(money[1].word, std::string("一百二十三点四五"));
}

TEST_CASE(v_mode_evaluates_arithmetic)
{
    const auto result = metasequoia::local_modes::query_v_mode("1+2*3");
    REQUIRE_EQ(result.size(), static_cast<size_t>(2));
    REQUIRE_EQ(result[0].word, std::string("7"));
    REQUIRE_EQ(result[1].word, std::string("1+2*3=7"));
    REQUIRE(metasequoia::local_modes::query_v_mode("1/0").empty());
    REQUIRE(metasequoia::local_modes::query_v_mode("1+").empty());
}

TEST_CASE(v_mode_composition_follows_the_scheme_trigger)
{
    using FanyImeVModeInput::Trigger;
    const std::string upper = "V12";
    const std::string lower = "v12";
    const std::string word = "vip";
    // 全拼大小写都认；双拼只认大写；关着都不认。
    REQUIRE(FanyImeVModeInput::IsComposition(upper.data(), upper.size(), Trigger::AnyCase));
    REQUIRE(FanyImeVModeInput::IsComposition(lower.data(), lower.size(), Trigger::AnyCase));
    REQUIRE(FanyImeVModeInput::IsComposition(upper.data(), upper.size(), Trigger::UppercaseOnly));
    REQUIRE(!FanyImeVModeInput::IsComposition(lower.data(), lower.size(), Trigger::UppercaseOnly));
    REQUIRE(!FanyImeVModeInput::IsComposition(upper.data(), upper.size(), Trigger::Off));
    // 小写 v 后面接字母仍是普通输入。
    REQUIRE(!FanyImeVModeInput::IsComposition(word.data(), word.size(), Trigger::AnyCase));
}
