#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "voice-input/voice_batch_protocol.h"
#include "tests/includes/test_framework.h"
#include <msime/voice/stt_service.h>
#include <nlohmann/json.hpp>

TEST_CASE(voice_batch_protocol_preserves_provider_fields_and_padding)
{
    VoiceInputConfig config;
    config.asr_provider = "openai";
    config.language = "zh-cn";
    config.asr_model = "fixture-asr";
    const std::vector<float> samples(160, 0.125f);
    const auto openai = VoiceInput::BuildBatchTranscription(samples, config);
    REQUIRE(openai.body.find("name=\"language\"\r\n\r\nzh") != std::string::npos);
    REQUIRE(openai.body.find("fixture-asr") != std::string::npos);
    config.asr_provider = "siliconflow";
    const auto siliconflow = VoiceInput::BuildBatchTranscription(samples, config);
    REQUIRE(siliconflow.body.find("name=\"language\"") == std::string::npos);
    const auto wave = siliconflow.body.find("RIFF");
    REQUIRE(wave != std::string::npos);
    // Tiny clips still contain a full second of padded PCM, as required by the existing provider adapter.
    const auto *bytes = reinterpret_cast<const unsigned char *>(siliconflow.body.data() + wave);
    const unsigned data_bytes = bytes[40] | (bytes[41] << 8) | (bytes[42] << 16) | (bytes[43] << 24);
    REQUIRE_EQ(data_bytes, 32000u);
    const std::vector<float> long_clip(61 * metasequoia::voice::sample_rate, 0.125f);
    REQUIRE(VoiceInput::BuildBatchTranscription(long_clip, config).body.size() > long_clip.size() * 2);
}

TEST_CASE(voice_batch_protocol_preserves_polish_prompt_and_thinking_settings)
{
    VoiceInputConfig config;
    config.polish_model = "fixture-polish";
    config.polish_prompt = "fixture prompt";
    config.polish_provider = "deepseek";
    // 厂商字段现在跟着接口地址走，所以 DeepSeek 行为要显式给一个 DeepSeek 地址。
    config.polish_endpoint = "https://api.deepseek.com/chat/completions";
    auto body = nlohmann::json::parse(VoiceInput::BuildBatchPolish("原始文本", config));
    REQUIRE_EQ(body["model"].get<std::string>(), "fixture-polish");
    REQUIRE_EQ(body["messages"][0]["content"].get<std::string>(), "fixture prompt");
    REQUIRE_EQ(body["messages"][1]["content"].get<std::string>(), "<asr_text>\n原始文本\n</asr_text>");
    REQUIRE_EQ(body["thinking"]["type"].get<std::string>(), "disabled");
    config.polish_provider = "siliconflow";
    config.polish_endpoint = "https://api.siliconflow.cn/v1/chat/completions";
    body = nlohmann::json::parse(VoiceInput::BuildBatchPolish("原始文本", config));
    REQUIRE_EQ(body["enable_thinking"].get<bool>(), false);
    REQUIRE(!body.contains("thinking"));
}

TEST_CASE(voice_batch_protocol_sends_deepseek_thinking_level)
{
    VoiceInputConfig config;
    config.polish_model = "fixture-polish";
    config.polish_prompt = "fixture prompt";
    config.polish_provider = "deepseek";
    config.polish_endpoint = "https://api.deepseek.com/chat/completions";
    for (const char *level : {"low", "high", "max"})
    {
        config.polish_thinking = level;
        const auto body = nlohmann::json::parse(VoiceInput::BuildBatchPolish("原始文本", config));
        REQUIRE_EQ(body["thinking"]["type"].get<std::string>(), "enabled");
        REQUIRE_EQ(body["reasoning_effort"].get<std::string>(), std::string(level));
    }
    // 其他提供商不认识 reasoning_effort，强度设置对它们不生效。
    config.polish_provider = "openai";
    config.polish_endpoint = "https://api.openai.com/v1/chat/completions";
    const auto body = nlohmann::json::parse(VoiceInput::BuildBatchPolish("原始文本", config));
    REQUIRE(!body.contains("thinking"));
    REQUIRE(!body.contains("reasoning_effort"));

    // 标签写错也要按地址判断：用户把 MiniMax 的地址填进 DeepSeek 那格时，
    // 请求里带着 thinking.type 会被对方以「requires adaptive thinking」直接拒绝。
    config.polish_provider = "deepseek";
    config.polish_endpoint = "https://api.minimax.cn/v1/chat/completions";
    const auto foreign = nlohmann::json::parse(VoiceInput::BuildBatchPolish("原始文本", config));
    REQUIRE(!foreign.contains("thinking"));
    REQUIRE(!foreign.contains("reasoning_effort"));
    REQUIRE(!foreign.contains("enable_thinking"));
}
