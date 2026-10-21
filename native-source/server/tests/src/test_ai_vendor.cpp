// Vendor-specific request fields must follow the endpoint that answers, not the provider label
// in the settings UI. A slot can hold any OpenAI-compatible service, and sending DeepSeek's
// thinking.type to one that does not accept it makes it reject the request outright.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "voice-input/voice_providers.h"
#include "tests/includes/test_framework.h"

TEST_CASE(chat_vendor_is_read_from_the_endpoint_host)
{
    using VoiceInput::ChatVendor;
    REQUIRE_EQ(VoiceInput::VendorFromChatEndpoint("https://api.deepseek.com/chat/completions"), ChatVendor::DeepSeek);
    REQUIRE_EQ(VoiceInput::VendorFromChatEndpoint("https://api.siliconflow.cn/v1/chat/completions"),
               ChatVendor::SiliconFlow);
    REQUIRE_EQ(VoiceInput::VendorFromChatEndpoint("https://api.groq.com/openai/v1/chat/completions"), ChatVendor::Groq);
    REQUIRE_EQ(VoiceInput::VendorFromChatEndpoint("https://api.openai.com/v1/chat/completions"), ChatVendor::OpenAI);
    // Host matching is case-insensitive and tolerates userinfo and an explicit port.
    REQUIRE_EQ(VoiceInput::VendorFromChatEndpoint("https://API.DeepSeek.COM/v1/chat/completions"),
               ChatVendor::DeepSeek);
    REQUIRE_EQ(VoiceInput::VendorFromChatEndpoint("https://user:pw@api.deepseek.com:443/chat/completions"),
               ChatVendor::DeepSeek);
    // Anything else is a plain OpenAI-compatible service, including local relays.
    REQUIRE_EQ(VoiceInput::VendorFromChatEndpoint("https://api.minimax.cn/v1/chat/completions"), ChatVendor::Other);
    REQUIRE_EQ(VoiceInput::VendorFromChatEndpoint("https://relay.example.com/v1/chat/completions"), ChatVendor::Other);
    REQUIRE_EQ(VoiceInput::VendorFromChatEndpoint("http://127.0.0.1:8080/v1/chat/completions"), ChatVendor::Other);
    // A host that merely starts with a vendor name is not that vendor.
    REQUIRE_EQ(VoiceInput::VendorFromChatEndpoint("https://deepseek.com.attacker.example/v1/chat/completions"),
               ChatVendor::Other);
    REQUIRE_EQ(VoiceInput::VendorFromChatEndpoint("https://notdeepseek.com/v1/chat/completions"), ChatVendor::Other);
    REQUIRE_EQ(VoiceInput::VendorFromChatEndpoint(""), ChatVendor::Other);
    REQUIRE_EQ(VoiceInput::VendorFromChatEndpoint("api.deepseek.com/chat/completions"), ChatVendor::Other);
}

TEST_CASE(deepseek_thinking_fields_stay_off_foreign_endpoints)
{
    using VoiceInput::ChatVendor;
    // The reported failure: the DeepSeek slot holding a MiniMax endpoint, which answers
    // "model requires adaptive thinking" as soon as thinking.type is sent.
    REQUIRE_EQ(VoiceInput::VendorForRequest("deepseek", "https://api.minimax.cn/v1/chat/completions"),
               ChatVendor::Other);
    REQUIRE(!VoiceInput::UsesDeepSeekThinking("deepseek", "https://api.minimax.cn/v1/chat/completions", "high"));
    REQUIRE(!VoiceInput::UsesDeepSeekThinking("deepseek", "https://api.minimax.cn/v1/chat/completions", "disabled"));

    // A real DeepSeek endpoint still gets thinking fields, whichever slot points at it.
    REQUIRE(VoiceInput::UsesDeepSeekThinking("deepseek", "https://api.deepseek.com/chat/completions", "high"));
    REQUIRE(VoiceInput::UsesDeepSeekThinking("custom", "https://api.deepseek.com/chat/completions", "low"));
    REQUIRE(!VoiceInput::UsesDeepSeekThinking("deepseek", "https://api.deepseek.com/chat/completions", "disabled"));
    REQUIRE(!VoiceInput::UsesDeepSeekThinking("deepseek", "https://api.deepseek.com/chat/completions", "nonsense"));
    // SiliconFlow and Groq endpoints never get DeepSeek fields either.
    REQUIRE(!VoiceInput::UsesDeepSeekThinking("deepseek", "https://api.siliconflow.cn/v1/chat/completions", "high"));
}

TEST_CASE(vendor_label_is_only_a_fallback_when_there_is_no_endpoint)
{
    using VoiceInput::ChatVendor;
    REQUIRE_EQ(VoiceInput::VendorForRequest("deepseek", ""), ChatVendor::DeepSeek);
    REQUIRE_EQ(VoiceInput::VendorForRequest("SiliconFlow", ""), ChatVendor::SiliconFlow);
    REQUIRE_EQ(VoiceInput::VendorForRequest("custom", ""), ChatVendor::Other);
    // Once an endpoint is known it outranks the label in both directions.
    REQUIRE_EQ(VoiceInput::VendorForRequest("custom", "https://api.groq.com/openai/v1/chat/completions"),
               ChatVendor::Groq);
    REQUIRE_EQ(VoiceInput::VendorForRequest("deepseek", "https://api.minimax.cn/v1/chat/completions"),
               ChatVendor::Other);
}