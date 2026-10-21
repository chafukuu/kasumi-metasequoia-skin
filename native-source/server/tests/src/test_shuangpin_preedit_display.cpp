#include "tests/includes/test_framework.h"
#include "engine/contracts/preedit_caret_map.h"
#include "utils/shuangpin_preedit_display.h"

#include <cstddef>
#include <string>
#include <vector>

TEST_CASE(ShuangpinQuanpinPreeditKeepsSeparatorsForPinyinStyle)
{
    const auto preedit = BuildShuangpinQuanpinPreedit("nihc", "ni'hc", "ni'hao", /*keep_separators=*/true);
    REQUIRE_EQ(preedit.text, std::string("ni'hao"));
    // n|i|h|c: 两键中间停在全拼音节的同一偏移，音节末尾停在全拼音节末尾。
    REQUIRE(preedit.caret_map == std::vector<std::size_t>({0, 1, 2, 4, 6}));
}

TEST_CASE(ShuangpinQuanpinPreeditDropsSeparatorsForRawStyle)
{
    const auto preedit = BuildShuangpinQuanpinPreedit("nihc", "ni'hc", "ni'hao", /*keep_separators=*/false);
    REQUIRE_EQ(preedit.text, std::string("nihao"));
    REQUIRE(preedit.caret_map == std::vector<std::size_t>({0, 1, 2, 3, 5}));
}

TEST_CASE(ShuangpinQuanpinPreeditFollowsManualSeparator)
{
    // 用户手动敲的分词符号也算在原串里，光标停在它后面时落到下一个全拼音节开头。
    const auto preedit = BuildShuangpinQuanpinPreedit("ni'hc", "ni'hc", "ni'hao", /*keep_separators=*/true);
    REQUIRE_EQ(preedit.text, std::string("ni'hao"));
    REQUIRE(preedit.caret_map == std::vector<std::size_t>({0, 1, 2, 3, 4, 6}));

    const auto raw = BuildShuangpinQuanpinPreedit("ni'hc", "ni'hc", "ni'hao", /*keep_separators=*/false);
    REQUIRE_EQ(raw.text, std::string("nihao"));
    REQUIRE(raw.caret_map == std::vector<std::size_t>({0, 1, 2, 2, 3, 5}));
}

TEST_CASE(ShuangpinQuanpinPreeditKeepsTrailingInitial)
{
    const auto preedit = BuildShuangpinQuanpinPreedit("vgh", "vg'h", "zheng'h", /*keep_separators=*/true);
    REQUIRE_EQ(preedit.text, std::string("zheng'h"));
    REQUIRE(preedit.caret_map == std::vector<std::size_t>({0, 1, 5, 7}));
}

TEST_CASE(ShuangpinQuanpinPreeditFallsBackWhenSegmentsDiffer)
{
    const auto preedit = BuildShuangpinQuanpinPreedit("abcd", "ab'cd", "abc", /*keep_separators=*/true);
    REQUIRE_EQ(preedit.text, std::string("abc"));
    REQUIRE_EQ(preedit.caret_map.size(), std::size_t(5));
    REQUIRE_EQ(preedit.caret_map.front(), std::size_t(0));
    REQUIRE_EQ(preedit.caret_map.back(), std::size_t(3));
}

TEST_CASE(PreeditCaretMapRoundTrips)
{
    std::wstring payload = FanyImePreeditCaretMap::Encode(L"你好nihao", {2, 3, 4, 5, 7});
    REQUIRE(payload.find(L'\t') == std::wstring::npos);
    std::vector<std::size_t> caret_map;
    FanyImePreeditCaretMap::Decode(payload, caret_map);
    REQUIRE(payload == L"你好nihao");
    REQUIRE(caret_map == std::vector<std::size_t>({2, 3, 4, 5, 7}));
}

TEST_CASE(PreeditCaretMapIgnoresPlainAndMalformedPayloads)
{
    std::vector<std::size_t> caret_map{1};
    std::wstring plain = L"ni'hao";
    FanyImePreeditCaretMap::Decode(plain, caret_map);
    REQUIRE(plain == L"ni'hao");
    REQUIRE(caret_map.empty());

    std::wstring malformed = std::wstring(L"nihao") + FanyImePreeditCaretMap::kMarker + L"0,x";
    FanyImePreeditCaretMap::Decode(malformed, caret_map);
    REQUIRE(malformed == L"nihao");
    REQUIRE(caret_map.empty());

    std::wstring out_of_range = std::wstring(L"ni") + FanyImePreeditCaretMap::kMarker + L"0,9";
    FanyImePreeditCaretMap::Decode(out_of_range, caret_map);
    REQUIRE(out_of_range == L"ni");
    REQUIRE(caret_map.empty());
}
