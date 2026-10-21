#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct ShuangpinProfile
{
    std::string name;
    std::unordered_map<std::string, std::string> initials;
    std::unordered_map<std::string, std::string> zero_initials;
    std::unordered_map<std::string, std::string> finals;
};

// Profiles have static storage duration and can safely be shared by sessions.
// A name registered with RegisterShuangpinProfile resolves to the latest registration.
const ShuangpinProfile &GetXiaoheShuangpinProfile();
const ShuangpinProfile &GetZiranmaShuangpinProfile();
const ShuangpinProfile &GetShoudaoShuangpinProfile();
const ShuangpinProfile &GetMicrosoftShuangpinProfile();
const ShuangpinProfile &GetSogouShuangpinProfile();
const ShuangpinProfile &GetZiguangShuangpinProfile();
const ShuangpinProfile &GetZhinengAbcShuangpinProfile();
const ShuangpinProfile &GetGuobiaoShuangpinProfile();
const ShuangpinProfile &GetPinyinJiajiaShuangpinProfile();
const ShuangpinProfile &GetShuangpinProfile(std::string_view name);

// Microsoft, Sogou and Ziguang put the "ing" final on ';', so ';' is an input key
// in the second position of a syllable for those profiles.
bool ShuangpinProfileUsesSemicolonFinal(const ShuangpinProfile &profile);

// How a syllable without an initial is typed; one entry per rule family of the common layouts.
enum class ShuangpinZeroInitialRule
{
    LetterO,     // Microsoft, Ziguang, Sogou, Zhineng ABC: 'o' + key of the final
    LetterA,     // Guobiao: 'a' + key of the final
    FirstLetter, // Pinyin Jiajia: first letter of the final + key of the final
    FullPinyin,  // Xiaohe, Ziranma: two-letter finals as spelled, others first letter + key
};
// Accepts "o", "a", "first_letter" and "full_pinyin".
std::optional<ShuangpinZeroInitialRule> ParseShuangpinZeroInitialRule(std::string_view name);

// A user-described layout. finals may carry "er", which only feeds the zero-initial codes;
// zero_initial_overrides replaces the code the rule would generate for individual finals.
struct CustomShuangpinLayout
{
    std::string name;
    ShuangpinZeroInitialRule zero_initial_rule = ShuangpinZeroInitialRule::LetterO;
    std::unordered_map<std::string, std::string> initials;
    std::unordered_map<std::string, std::string> finals;
    std::unordered_map<std::string, std::string> zero_initial_overrides;
};

// Fills profile and returns no errors only when every syllable encodes to a code that decodes
// back to it. Errors are user-facing Chinese sentences.
std::vector<std::string> BuildCustomShuangpinProfile(const CustomShuangpinLayout &layout, ShuangpinProfile &profile);

// Keeps a runtime-built profile alive for the rest of the process and makes GetShuangpinProfile
// resolve its name to it. Re-registering a name leaves earlier references valid.
const ShuangpinProfile &RegisterShuangpinProfile(ShuangpinProfile profile);
