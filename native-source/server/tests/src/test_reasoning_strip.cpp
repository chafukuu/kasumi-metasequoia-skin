// 推理模型（DeepSeek R 系、MiniMax M3 系这类强制思考的）会把思考过程混在 content 里带回来。
// 输入法只上屏答案，思考残留必须剥干净——语音润色和 AI 联想两条路径共用这一个剥离器。
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "msime/voice/provider_protocol.h"
#include "msime/voice/stt_service.h"
#include "tests/includes/test_framework.h"

TEST_CASE(reasoning_strip_removes_complete_think_blocks)
{
    using metasequoia::voice::strip_reasoning;
    REQUIRE_EQ(strip_reasoning("<think>内心活动</think>\n答案"), std::string("答案"));
    REQUIRE_EQ(strip_reasoning("<think>a</think><think>b</think>答案"), std::string("答案"));
    REQUIRE_EQ(strip_reasoning("没有思考的普通文本"), std::string("没有思考的普通文本"));
    REQUIRE_EQ(strip_reasoning(""), std::string(""));
}

TEST_CASE(reasoning_strip_drops_unclosed_thinking)
{
    using metasequoia::voice::strip_reasoning;
    // max_tokens 截断会留下没有闭合标签的半截思考：没有可靠边界，宁可全丢，
    // 让调用方回退到原始 ASR 文本，也不能把半截推理当答案上屏。
    REQUIRE_EQ(strip_reasoning("<think>被截断的思考"), std::string(""));
    REQUIRE_EQ(strip_reasoning("前置文本<think>被截断"), std::string("前置文本"));
}

TEST_CASE(reasoning_strip_keeps_answer_that_merely_mentions_tags)
{
    using metasequoia::voice::strip_reasoning;
    // 答案本体出现在剥离之后的内容里时必须保留。
    const auto stripped = strip_reasoning("<think>x</think>\n<answer_tag> 不该被误删的正文");
    REQUIRE_EQ(stripped, std::string("<answer_tag> 不该被误删的正文"));
}

TEST_CASE(polished_text_strips_inline_reasoning_from_model_output)
{
    using metasequoia::voice::parse_polished_text;
    const auto *with_think =
        R"({"choices":[{"message":{"content":"<think>用户在测试麦克风</think>\nhello，听得到吗？"}}]})";
    REQUIRE_EQ(parse_polished_text(with_think), std::string("hello，听得到吗？"));

    // 整段都是思考（没有答案）：按缺文本处理，让调用方回退到原始 ASR 文本。
    for (const auto *response : {
             R"({"choices":[{"message":{"content":"<think>只有思考"}}]})",
             R"({"choices":[{"message":{"content":"<think>x</think>\n\n  "}}]})",
         })
    {
        bool threw = false;
        try
        {
            (void)parse_polished_text(response);
        }
        catch (const metasequoia::voice::VoiceError &)
        {
            threw = true;
        }
        REQUIRE(threw);
    }
}