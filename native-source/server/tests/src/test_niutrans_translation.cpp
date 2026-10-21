#include "tests/includes/test_framework.h"

#include "cloud/niutrans_translation.h"

TEST_CASE(NiuTransRequiresBothCredentials)
{
    REQUIRE(NiuTransTranslation::IsUsableConfig({"app123", "key456"}));
    REQUIRE(!NiuTransTranslation::IsUsableConfig({"", "key456"}));
    REQUIRE(!NiuTransTranslation::IsUsableConfig({"app123", ""}));
    // Placeholder-shaped credentials from config.default.toml must not count as usable.
    REQUIRE(!NiuTransTranslation::IsUsableConfig({"<YOUR_APP_ID>", "<YOUR_APIKEY>"}));
}

TEST_CASE(NiuTransBuildsSortedAuthString)
{
    // md5 of "apikey=key456&appId=app123&from=zh&srcText=你好&timestamp=1700000000000&to=en"
    const std::string auth =
        NiuTransTranslation::BuildAuthString({"app123", "key456"}, "zh", "en", "1700000000000", "你好");
    REQUIRE_EQ(auth, std::string("7b63ea8c434003c0dfb6e0ef4e0f6a46"));
}

TEST_CASE(NiuTransParsesResponses)
{
    REQUIRE_EQ(NiuTransTranslation::ParseTranslationResponse(R"({"from":"zh","to":"en","tgtText":"hello"})"),
               std::string("hello"));
    REQUIRE(
        NiuTransTranslation::ParseTranslationResponse(R"({"errorCode":"20003","errorMsg":"参数校验异常"})").empty());
    REQUIRE(NiuTransTranslation::ParseTranslationResponse("not json").empty());
}
