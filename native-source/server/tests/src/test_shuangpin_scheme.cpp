#include "tests/includes/test_framework.h"
#include "engine/core/ime_session.h"
#include "engine/schemes/shuangpin_scheme.h"
#include "engine/shuangpin/shuangpin_query.h"
#include "engine/quanpin/quanpin_utils.h"
#include <algorithm>
#include <cstdio>
#include <utility>
#include <vector>

namespace
{
void InputKey(ShuangpinScheme &scheme, UINT vk, WCHAR wch, UINT modifiers_down = 0)
{
    scheme.handle_key(vk, modifiers_down, wch);
}

void InputSessionKeys(ImeSession &session, const std::string &keys)
{
    for (const char ch : keys)
    {
        const char vk = ch >= 'a' && ch <= 'z' ? static_cast<char>(ch - ('a' - 'A')) : ch;
        session.handle_key(static_cast<UINT>(vk), 0, static_cast<WCHAR>(ch));
    }
}

const ShuangpinProfile &GetTestShuangpinProfile()
{
    static const ShuangpinProfile profile = [] {
        ShuangpinProfile value = GetXiaoheShuangpinProfile();
        value.name = "test";
        value.finals["iang"] = "d";
        value.finals["uang"] = "d";
        return value;
    }();
    return profile;
}
} // namespace

TEST_CASE(ShuangpinProfilesDecodeYoAsOneSyllable)
{
    for (const auto *profile :
         {&GetXiaoheShuangpinProfile(), &GetZiranmaShuangpinProfile(), &GetShoudaoShuangpinProfile(),
          &GetMicrosoftShuangpinProfile(), &GetSogouShuangpinProfile(), &GetZiguangShuangpinProfile(),
          &GetZhinengAbcShuangpinProfile(), &GetGuobiaoShuangpinProfile(), &GetPinyinJiajiaShuangpinProfile()})
    {
        ShuangpinScheme scheme(*profile);
        InputKey(scheme, 'Y', L'y');
        InputKey(scheme, 'O', L'o');
        const auto request = scheme.build_request();
        REQUIRE_EQ(request.raw_segmentation, std::string("yo"));
        REQUIRE_EQ(request.normalized_segmentation, std::string("yo"));
        REQUIRE(shuangpin::is_complete_input("yo", *profile));
    }
}

TEST_CASE(ShuangpinSchemeBuildRequestPreservesCaseAndNormalizesQuery)
{
    ShuangpinScheme scheme;
    InputKey(scheme, 'X', L'X', 1);
    InputKey(scheme, 'I', L'i');

    const QueryRequest request = scheme.build_request();
    REQUIRE(request.valid);
    REQUIRE_EQ(request.raw_input, std::string("xi"));
    REQUIRE_EQ(request.raw_input_with_cases, std::string("Xi"));
    REQUIRE_EQ(request.raw_segmentation, std::string("Xi"));
    REQUIRE_EQ(request.normalized_input, std::string("xi"));
}

TEST_CASE(ShuangpinDoubleHelpcodesRemainAnUnsegmentedPreeditSuffix)
{
    ImeSession session(SchemeType::Shuangpin);
    session.set_shuangpin_helpcode_enabled(true);
    InputSessionKeys(session, "yakP");
    REQUIRE_EQ(session.get_request().raw_segmentation, std::string("ya'kP"));
    REQUIRE_EQ(session.get_request().normalized_segmentation, std::string("ya'kP"));

    session.reset();
    InputSessionKeys(session, "yaKp");
    REQUIRE_EQ(session.get_request().raw_segmentation, std::string("ya'Kp"));
    REQUIRE_EQ(session.get_request().normalized_segmentation, std::string("ya'Kp"));
}

TEST_CASE(ShuangpinSchemeSpaceDoesNotResetComposition)
{
    ShuangpinScheme scheme;
    InputKey(scheme, 'X', L'x');
    InputKey(scheme, 'I', L'i');
    InputKey(scheme, VK_SPACE, L' ');

    const QueryRequest request = scheme.build_request();
    REQUIRE(request.valid);
    REQUIRE_EQ(request.raw_input, std::string("xi"));
}

TEST_CASE(ShuangpinSchemeDeduplicatesConsecutiveManualSeparators)
{
    ShuangpinScheme scheme;
    InputKey(scheme, 'X', L'x');
    InputKey(scheme, 'I', L'i');
    InputKey(scheme, VK_OEM_7, L'\'');
    InputKey(scheme, VK_OEM_7, L'\'');
    InputKey(scheme, VK_OEM_7, L'\'');

    const QueryRequest request = scheme.build_request();
    REQUIRE(request.valid);
    REQUIRE_EQ(request.raw_input, std::string("xi'"));
    REQUIRE_EQ(request.key_strokes.size(), static_cast<size_t>(3));
}

TEST_CASE(ShuangpinSchemeEnterResetsComposition)
{
    ShuangpinScheme scheme;
    InputKey(scheme, 'X', L'x');
    InputKey(scheme, 'I', L'i');
    InputKey(scheme, VK_RETURN, 0);

    const QueryRequest request = scheme.build_request();
    REQUIRE(!request.valid);
    REQUIRE_EQ(request.raw_input, std::string(""));
}

TEST_CASE(ShuangpinSchemeBackspaceRemovesLastInput)
{
    ShuangpinScheme scheme;
    InputKey(scheme, 'X', L'x');
    InputKey(scheme, 'I', L'i');
    InputKey(scheme, VK_BACK, 0);

    const QueryRequest request = scheme.build_request();
    REQUIRE(request.valid);
    REQUIRE_EQ(request.raw_input, std::string("x"));
    REQUIRE_EQ(request.raw_input_with_cases, std::string("x"));
}

TEST_CASE(ShuangpinSchemeUsesInjectedProfile)
{
    ShuangpinScheme xiaohe;
    InputKey(xiaohe, 'X', L'x');
    InputKey(xiaohe, 'L', L'l');
    REQUIRE_EQ(xiaohe.build_request().normalized_segmentation, std::string("xiang"));

    ShuangpinScheme custom(GetTestShuangpinProfile());
    InputKey(custom, 'X', L'x');
    InputKey(custom, 'D', L'd');
    REQUIRE_EQ(custom.build_request().normalized_segmentation, std::string("xiang"));
}

TEST_CASE(ZiranmaProfileDecodesWikipediaKeyboardLayout)
{
    const auto &profile = GetZiranmaShuangpinProfile();
    const std::vector<std::pair<std::string, std::string>> cases{
        {"jq", "jiu"},  {"xw", "xia"},  {"gw", "gua"},   {"he", "he"},    {"gr", "guan"},  {"xt", "xue"},
        {"my", "ming"}, {"ky", "kuai"}, {"du", "du"},    {"li", "li"},    {"bo", "bo"},    {"lo", "luo"},
        {"lp", "lun"},  {"da", "da"},   {"js", "jiong"}, {"ds", "dong"},  {"xd", "xiang"}, {"gd", "guang"},
        {"hf", "hen"},  {"dg", "deng"}, {"dh", "dang"},  {"dj", "dan"},   {"dk", "dao"},   {"dl", "dai"},
        {"fz", "fei"},  {"dx", "die"},  {"dc", "diao"},  {"dv", "dui"},   {"lv", "lv"},    {"db", "dou"},
        {"ln", "lin"},  {"lm", "lian"}, {"vh", "zhang"}, {"ih", "chang"}, {"uh", "shang"},
    };

    for (const auto &[input, expected] : cases)
    {
        REQUIRE_EQ(shuangpin::normalize_input(input, profile), expected);
    }
}

TEST_CASE(ZiranmaProfileDecodesZeroInitialSyllables)
{
    const auto &profile = GetZiranmaShuangpinProfile();
    const std::vector<std::pair<std::string, std::string>> cases{
        {"aa", "a"},   {"oo", "o"},  {"ee", "e"},  {"ai", "ai"},  {"an", "an"}, {"ao", "ao"},
        {"ah", "ang"}, {"ei", "ei"}, {"en", "en"}, {"eg", "eng"}, {"er", "er"}, {"ou", "ou"},
    };

    for (const auto &[input, expected] : cases)
    {
        REQUIRE_EQ(shuangpin::normalize_input(input, profile), expected);
    }
}

TEST_CASE(ShoudaoProfileDecodesKeyboardLayout)
{
    const auto &profile = GetShoudaoShuangpinProfile();
    const std::vector<std::pair<std::string, std::string>> cases{
        {"jq", "jiu"},   {"gw", "gua"}, {"he", "he"},    {"dr", "die"},   {"gt", "guan"},  {"dy", "dang"},
        {"du", "du"},    {"li", "li"},  {"bo", "bo"},    {"lo", "luo"},   {"dp", "diao"},  {"da", "da"},
        {"ds", "dou"},   {"dd", "dao"}, {"df", "deng"},  {"gg", "guai"},  {"mg", "ming"},  {"dh", "dong"},
        {"jh", "jiong"}, {"dj", "dan"}, {"hk", "hen"},   {"xk", "xia"},   {"dl", "dai"},   {"jl", "jue"},
        {"yl", "yue"},   {"dz", "dun"}, {"xx", "xiang"}, {"gx", "guang"}, {"lc", "lin"},   {"lv", "lv"},
        {"dv", "dui"},   {"lb", "lve"}, {"dn", "dian"},  {"fm", "fei"},   {"vy", "zhang"}, {"iy", "chang"},
        {"ey", "shang"}, {"ei", "shi"},
    };

    for (const auto &[input, expected] : cases)
    {
        REQUIRE_EQ(shuangpin::normalize_input(input, profile), expected);
    }
}

TEST_CASE(ShoudaoProfileDecodesZeroInitialSyllables)
{
    const auto &profile = GetShoudaoShuangpinProfile();
    const std::vector<std::pair<std::string, std::string>> cases{
        {"aa", "a"},  {"ai", "ai"}, {"an", "an"},  {"ay", "ang"}, {"ao", "ao"}, {"ue", "e"},
        {"ui", "ei"}, {"en", "en"}, {"uf", "eng"}, {"er", "er"},  {"oo", "o"},  {"ou", "ou"},
    };

    for (const auto &[input, expected] : cases)
    {
        REQUIRE_EQ(shuangpin::normalize_input(input, profile), expected);
    }
}

TEST_CASE(MicrosoftProfileDecodesKeyboardLayout)
{
    const auto &profile = GetMicrosoftShuangpinProfile();
    const std::vector<std::pair<std::string, std::string>> cases{
        {"ud", "shuang"}, {"pn", "pin"},  {"vs", "zhong"}, {"m;", "ming"}, {"x;", "xing"}, {"jy", "ju"},
        {"jt", "jue"},    {"yr", "yuan"}, {"ly", "lv"},    {"lv", "lve"},  {"gy", "guai"}, {"dp", "dun"},
    };

    for (const auto &[input, expected] : cases)
    {
        REQUIRE_EQ(shuangpin::normalize_input(input, profile), expected);
    }
}

TEST_CASE(MicrosoftProfileDecodesZeroInitialSyllables)
{
    const auto &profile = GetMicrosoftShuangpinProfile();
    const std::vector<std::pair<std::string, std::string>> cases{
        {"oa", "a"},  {"ol", "ai"}, {"oj", "an"},  {"oh", "ang"}, {"ok", "ao"}, {"oe", "e"},
        {"oz", "ei"}, {"of", "en"}, {"og", "eng"}, {"or", "er"},  {"oo", "o"},  {"ob", "ou"},
    };

    for (const auto &[input, expected] : cases)
    {
        REQUIRE_EQ(shuangpin::normalize_input(input, profile), expected);
    }
}

TEST_CASE(ShuangpinProfilesDecodeKei)
{
    const std::vector<std::pair<const ShuangpinProfile *, std::string>> cases{
        {&GetXiaoheShuangpinProfile(), "kw"},       {&GetZiranmaShuangpinProfile(), "kz"},
        {&GetShoudaoShuangpinProfile(), "km"},      {&GetMicrosoftShuangpinProfile(), "kz"},
        {&GetSogouShuangpinProfile(), "kz"},        {&GetZiguangShuangpinProfile(), "kk"},
        {&GetZhinengAbcShuangpinProfile(), "kq"},   {&GetGuobiaoShuangpinProfile(), "kb"},
        {&GetPinyinJiajiaShuangpinProfile(), "kw"},
    };

    for (const auto &[profile, input] : cases)
    {
        REQUIRE_EQ(shuangpin::normalize_input(input, *profile), std::string("kei"));
        REQUIRE(shuangpin::is_complete_input(input, *profile));
    }
}

TEST_CASE(MicrosoftSchemeAcceptsSemicolonAsIngFinalOnlyInSecondPosition)
{
    ShuangpinScheme scheme(GetMicrosoftShuangpinProfile());
    InputKey(scheme, 'M', L'm');
    InputKey(scheme, VK_OEM_1, L';');
    auto request = scheme.build_request();
    REQUIRE_EQ(request.raw_input, std::string("m;"));
    REQUIRE_EQ(request.raw_segmentation, std::string("m;"));
    REQUIRE_EQ(request.normalized_segmentation, std::string("ming"));

    scheme.reset();
    InputKey(scheme, 'X', L'x');
    InputKey(scheme, VK_OEM_1, L';');
    request = scheme.build_request();
    REQUIRE_EQ(request.raw_input, std::string("x;"));
    REQUIRE_EQ(request.raw_segmentation, std::string("x;"));
    REQUIRE_EQ(request.normalized_segmentation, std::string("xing"));

    scheme.reset();
    InputKey(scheme, VK_OEM_1, L';');
    REQUIRE(!scheme.build_request().valid);
}

TEST_CASE(SogouProfileDecodesKeyboardLayout)
{
    const auto &profile = GetSogouShuangpinProfile();
    const std::vector<std::pair<std::string, std::string>> cases{
        {"jq", "jiu"},   {"xw", "xia"},   {"gw", "gua"},  {"gr", "guan"}, {"xt", "xue"},   {"lt", "lve"},
        {"gy", "guai"},  {"ly", "lv"},    {"lo", "luo"},  {"dp", "dun"},  {"js", "jiong"}, {"ds", "dong"},
        {"xd", "xiang"}, {"gd", "guang"}, {"hf", "hen"},  {"dg", "deng"}, {"dh", "dang"},  {"dj", "dan"},
        {"dk", "dao"},   {"dl", "dai"},   {"m;", "ming"}, {"fz", "fei"},  {"dx", "die"},   {"dc", "diao"},
        {"dv", "dui"},   {"db", "dou"},   {"ln", "lin"},  {"lm", "lian"}, {"vh", "zhang"}, {"ih", "chang"},
        {"uh", "shang"},
    };

    for (const auto &[input, expected] : cases)
    {
        REQUIRE_EQ(shuangpin::normalize_input(input, profile), expected);
    }
}

TEST_CASE(ZiguangProfileDecodesKeyboardLayout)
{
    const auto &profile = GetZiguangShuangpinProfile();
    const std::vector<std::pair<std::string, std::string>> cases{
        {"dq", "dao"},   {"hw", "hen"}, {"he", "he"},    {"dr", "dan"},   {"dt", "deng"},  {"ly", "lin"},
        {"gy", "guai"},  {"du", "du"},  {"li", "li"},    {"lo", "luo"},   {"dp", "dai"},   {"da", "da"},
        {"ds", "dang"},  {"dd", "die"}, {"df", "dian"},  {"xg", "xiang"}, {"gg", "guang"}, {"dh", "dong"},
        {"jh", "jiong"}, {"jj", "jiu"}, {"fk", "fei"},   {"gl", "guan"},  {"m;", "ming"},  {"dz", "dou"},
        {"xx", "xia"},   {"gx", "gua"}, {"lv", "lv"},    {"db", "diao"},  {"dn", "dui"},   {"jn", "jue"},
        {"ln", "lve"},   {"dm", "dun"}, {"us", "zhang"}, {"as", "chang"}, {"is", "shang"},
    };

    for (const auto &[input, expected] : cases)
    {
        REQUIRE_EQ(shuangpin::normalize_input(input, profile), expected);
    }
}

TEST_CASE(ZhinengAbcProfileDecodesKeyboardLayout)
{
    const auto &profile = GetZhinengAbcShuangpinProfile();
    const std::vector<std::pair<std::string, std::string>> cases{
        {"fq", "fei"},  {"dw", "dian"},  {"he", "he"},    {"jr", "jiu"},   {"xt", "xiang"}, {"gt", "guang"},
        {"my", "ming"}, {"du", "du"},    {"li", "li"},    {"lo", "luo"},   {"gp", "guan"},  {"da", "da"},
        {"ds", "dong"}, {"js", "jiong"}, {"xd", "xia"},   {"gd", "gua"},   {"hf", "hen"},   {"dg", "deng"},
        {"dh", "dang"}, {"dj", "dan"},   {"dk", "dao"},   {"dl", "dai"},   {"dz", "diao"},  {"dx", "die"},
        {"lc", "lin"},  {"gc", "guai"},  {"lv", "lv"},    {"db", "dou"},   {"dn", "dun"},   {"jm", "jue"},
        {"lm", "lve"},  {"dm", "dui"},   {"ah", "zhang"}, {"eh", "chang"}, {"vh", "shang"}, {"ee", "che"},
    };

    for (const auto &[input, expected] : cases)
    {
        REQUIRE_EQ(shuangpin::normalize_input(input, profile), expected);
    }
}

TEST_CASE(GuobiaoProfileDecodesKeyboardLayout)
{
    const auto &profile = GetGuobiaoShuangpinProfile();
    const std::vector<std::pair<std::string, std::string>> cases{
        {"xq", "xia"},   {"gq", "gua"},   {"gw", "guan"},  {"jw", "juan"},  {"he", "he"},    {"hr", "hen"},
        {"dt", "die"},   {"jy", "jiu"},   {"gy", "guai"},  {"du", "du"},    {"li", "li"},    {"lo", "luo"},
        {"dp", "dou"},   {"da", "da"},    {"ds", "dong"},  {"js", "jiong"}, {"dd", "dian"},  {"df", "dan"},
        {"dg", "dang"},  {"dh", "deng"},  {"mj", "ming"},  {"dk", "dai"},   {"ll", "lin"},   {"dz", "dun"},
        {"jz", "jun"},   {"xx", "xue"},   {"lx", "lve"},   {"dc", "dao"},   {"lv", "lv"},    {"dv", "dui"},
        {"fb", "fei"},   {"xn", "xiang"}, {"gn", "guang"}, {"dm", "diao"},  {"vg", "zhang"}, {"ig", "chang"},
        {"ug", "shang"},
    };

    for (const auto &[input, expected] : cases)
    {
        REQUIRE_EQ(shuangpin::normalize_input(input, profile), expected);
    }
}

TEST_CASE(PinyinJiajiaProfileDecodesKeyboardLayout)
{
    const auto &profile = GetPinyinJiajiaShuangpinProfile();
    const std::vector<std::pair<std::string, std::string>> cases{
        {"mq", "ming"},  {"fw", "fei"},  {"he", "he"},    {"hr", "hen"},   {"dt", "deng"},  {"dy", "dong"},
        {"jy", "jiong"}, {"du", "du"},   {"li", "li"},    {"lo", "luo"},   {"dp", "dou"},   {"da", "da"},
        {"ds", "dai"},   {"dd", "dao"},  {"df", "dan"},   {"dg", "dang"},  {"xh", "xiang"}, {"gh", "guang"},
        {"dj", "dian"},  {"dk", "diao"}, {"ll", "lin"},   {"dz", "dun"},   {"xx", "xue"},   {"lx", "lve"},
        {"gx", "guai"},  {"gc", "guan"}, {"dv", "dui"},   {"lv", "lv"},    {"xb", "xia"},   {"gb", "gua"},
        {"jn", "jiu"},   {"dm", "die"},  {"vg", "zhang"}, {"ug", "chang"}, {"ig", "shang"},
    };

    for (const auto &[input, expected] : cases)
    {
        REQUIRE_EQ(shuangpin::normalize_input(input, profile), expected);
    }
}

TEST_CASE(NewProfilesDecodeZeroInitialSyllables)
{
    // 搜狗、紫光、智能ABC：O + 韵母所在键；国标：A + 韵母所在键；拼音加加：韵母首字母 + 韵母所在键。
    const std::vector<std::pair<const ShuangpinProfile *, std::vector<std::pair<std::string, std::string>>>> cases{
        {&GetSogouShuangpinProfile(),
         {{"oa", "a"},
          {"ol", "ai"},
          {"oj", "an"},
          {"oh", "ang"},
          {"ok", "ao"},
          {"oe", "e"},
          {"oz", "ei"},
          {"of", "en"},
          {"og", "eng"},
          {"or", "er"},
          {"oo", "o"},
          {"ob", "ou"}}},
        {&GetZiguangShuangpinProfile(),
         {{"oa", "a"},
          {"op", "ai"},
          {"or", "an"},
          {"os", "ang"},
          {"oq", "ao"},
          {"oe", "e"},
          {"ok", "ei"},
          {"ow", "en"},
          {"ot", "eng"},
          {"oj", "er"},
          {"oo", "o"},
          {"oz", "ou"}}},
        {&GetZhinengAbcShuangpinProfile(),
         {{"oa", "a"},
          {"ol", "ai"},
          {"oj", "an"},
          {"oh", "ang"},
          {"ok", "ao"},
          {"oe", "e"},
          {"oq", "ei"},
          {"of", "en"},
          {"og", "eng"},
          {"or", "er"},
          {"oo", "o"},
          {"ob", "ou"}}},
        {&GetGuobiaoShuangpinProfile(),
         {{"aa", "a"},
          {"ak", "ai"},
          {"af", "an"},
          {"ag", "ang"},
          {"ac", "ao"},
          {"ae", "e"},
          {"ab", "ei"},
          {"ar", "en"},
          {"ah", "eng"},
          {"al", "er"},
          {"ao", "o"},
          {"ap", "ou"}}},
        {&GetPinyinJiajiaShuangpinProfile(),
         {{"aa", "a"},
          {"as", "ai"},
          {"af", "an"},
          {"ag", "ang"},
          {"ad", "ao"},
          {"ee", "e"},
          {"ew", "ei"},
          {"er", "en"},
          {"et", "eng"},
          {"eq", "er"},
          {"oo", "o"},
          {"op", "ou"}}},
    };

    for (const auto &[profile, syllables] : cases)
    {
        for (const auto &[input, expected] : syllables)
        {
            REQUIRE_EQ(shuangpin::normalize_input(input, *profile), expected);
        }
    }
}

TEST_CASE(EveryProfileRoundTripsEveryEncodableSyllable)
{
    // Encoding each syllable with the profile's own tables and decoding it back catches two
    // finals sharing a key where both would form a valid syllable with the same initial.
    const std::vector<std::string> excluded{"chua", "den", "fiao", "jve", "lo",  "lue", "nou",
                                            "nue",  "nun", "qve",  "xve", "yve", "zhei"};
    for (const auto *profile :
         {&GetXiaoheShuangpinProfile(), &GetZiranmaShuangpinProfile(), &GetShoudaoShuangpinProfile(),
          &GetMicrosoftShuangpinProfile(), &GetSogouShuangpinProfile(), &GetZiguangShuangpinProfile(),
          &GetZhinengAbcShuangpinProfile(), &GetGuobiaoShuangpinProfile(), &GetPinyinJiajiaShuangpinProfile()})
    {
        size_t checked = 0;
        for (const auto &syllable : quanpin::intact_pinyin_set())
        {
            // j/q/x/y spell ü as "u"; a "v" spelling of those is not a syllable the decoder produces.
            if (std::find(excluded.begin(), excluded.end(), syllable) != excluded.end() ||
                (std::string("jqxy").find(syllable[0]) != std::string::npos && syllable.find('v') != std::string::npos))
            {
                continue;
            }
            std::string code;
            if (const auto zero = profile->zero_initials.find(syllable); zero != profile->zero_initials.end())
            {
                code = zero->second;
            }
            else
            {
                const size_t initial_length = syllable.size() > 2 && syllable[1] == 'h' &&
                                                      std::string("zcs").find(syllable[0]) != std::string::npos
                                                  ? 2
                                                  : 1;
                if (std::string("aeo").find(syllable[0]) != std::string::npos || syllable.size() <= initial_length)
                {
                    continue;
                }
                const std::string initial = syllable.substr(0, initial_length);
                const auto final = profile->finals.find(syllable.substr(initial_length));
                if (final == profile->finals.end())
                {
                    continue;
                }
                const auto initial_key = profile->initials.find(initial);
                code = (initial_key == profile->initials.end() ? initial : initial_key->second) + final->second;
            }
            if (shuangpin::normalize_input(code, *profile) != syllable)
            {
                std::fprintf(stderr, "%s: %s -> %s decodes as %s\n", profile->name.c_str(), syllable.c_str(),
                             code.c_str(), shuangpin::normalize_input(code, *profile).c_str());
            }
            REQUIRE_EQ(shuangpin::normalize_input(code, *profile), syllable);
            ++checked;
        }
        REQUIRE(checked > 390);
    }
}

TEST_CASE(SemicolonFinalFollowsProfileLayout)
{
    REQUIRE(ShuangpinProfileUsesSemicolonFinal(GetMicrosoftShuangpinProfile()));
    REQUIRE(ShuangpinProfileUsesSemicolonFinal(GetSogouShuangpinProfile()));
    REQUIRE(ShuangpinProfileUsesSemicolonFinal(GetZiguangShuangpinProfile()));
    REQUIRE(!ShuangpinProfileUsesSemicolonFinal(GetXiaoheShuangpinProfile()));
    REQUIRE(!ShuangpinProfileUsesSemicolonFinal(GetZhinengAbcShuangpinProfile()));
    REQUIRE(!ShuangpinProfileUsesSemicolonFinal(GetGuobiaoShuangpinProfile()));
    REQUIRE(!ShuangpinProfileUsesSemicolonFinal(GetPinyinJiajiaShuangpinProfile()));

    for (const auto *profile : {&GetSogouShuangpinProfile(), &GetZiguangShuangpinProfile()})
    {
        ShuangpinScheme scheme(*profile);
        InputKey(scheme, 'X', L'x');
        InputKey(scheme, VK_OEM_1, L';');
        const auto request = scheme.build_request();
        REQUIRE_EQ(request.raw_input, std::string("x;"));
        REQUIRE_EQ(request.normalized_segmentation, std::string("xing"));
    }
}

TEST_CASE(ShuangpinProfileResolverSelectsNamedProfileAndFallsBackToXiaohe)
{
    for (const char *name :
         {"xiaohe", "ziranma", "shoudao", "microsoft", "sogou", "ziguang", "zhinengabc", "guobiao", "pinyinjiajia"})
    {
        REQUIRE_EQ(GetShuangpinProfile(name).name, std::string(name));
    }
    REQUIRE_EQ(GetShuangpinProfile("ziranma").name, std::string("ziranma"));
    REQUIRE_EQ(GetShuangpinProfile("shoudao").name, std::string("shoudao"));
    REQUIRE_EQ(GetShuangpinProfile("microsoft").name, std::string("microsoft"));
    REQUIRE_EQ(GetShuangpinProfile("xiaohe").name, std::string("xiaohe"));
    REQUIRE_EQ(GetShuangpinProfile("unknown").name, std::string("xiaohe"));
}
