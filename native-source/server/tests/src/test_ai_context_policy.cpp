#include "ipc/ai_context_policy.h"
#include "tests/includes/test_framework.h"

TEST_CASE(ai_context_drops_credential_like_ascii_runs)
{
    using FanyImeIpc::StripCredentialLikeText;
    REQUIRE_EQ(StripCredentialLikeText("密码是Abc12345然后"), std::string("密码是然后"));
    REQUIRE_EQ(StripCredentialLikeText("key sk-proj-abcdEFGH1234 已填"), std::string("key  已填"));
    REQUIRE_EQ(StripCredentialLikeText("p@ssword"), std::string());
    REQUIRE_EQ(StripCredentialLikeText("2026-10-11"), std::string());
}

TEST_CASE(ai_context_keeps_plain_words_numbers_and_chinese)
{
    using FanyImeIpc::StripCredentialLikeText;
    REQUIRE_EQ(StripCredentialLikeText("明天3点开会"), std::string("明天3点开会"));
    REQUIRE_EQ(StripCredentialLikeText("用Python写个脚本"), std::string("用Python写个脚本"));
    REQUIRE_EQ(StripCredentialLikeText("international"), std::string("international"));
    REQUIRE_EQ(StripCredentialLikeText("13800138000"), std::string("13800138000"));
    // Short mixed runs are below the length threshold.
    REQUIRE_EQ(StripCredentialLikeText("A4纸和e.g."), std::string("A4纸和e.g."));
    REQUIRE_EQ(StripCredentialLikeText(""), std::string());
}

TEST_CASE(ai_context_credential_threshold_needs_length_and_two_classes)
{
    using FanyImeIpc::IsCredentialLikeAsciiRun;
    REQUIRE(!IsCredentialLikeAsciiRun("abc1234"));
    REQUIRE(IsCredentialLikeAsciiRun("abcd1234"));
    REQUIRE(!IsCredentialLikeAsciiRun("abcdefgh"));
    REQUIRE(!IsCredentialLikeAsciiRun("12345678"));
    REQUIRE(IsCredentialLikeAsciiRun("abcdefg!"));
    REQUIRE(IsCredentialLikeAsciiRun("1234-567"));
}
