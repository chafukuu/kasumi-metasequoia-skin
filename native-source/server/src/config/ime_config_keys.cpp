// 按键行为配置：翻页键、方向键选词（含横排左右键）、以词定字、智能标点、配对标点、顿号键与标点锁定，
// 以及发给 TSF 的对应载荷。
#include "config/ime_config_internal.h"
#include <string>
#include "engine/contracts/enumeration_comma.h"
#include "utils/common_utils.h"
#include "global/globals.h"

using namespace ime_config_detail;

bool GetConfiguredPagingMinusEqualEnabled()
{
    return g_paging_minus_equal_enabled;
}

bool SetConfiguredPagingMinusEqualEnabled(bool enabled)
{
    const bool word_enabled = g_word_to_character_enabled && !(enabled && g_word_to_character_keys == "minus_equal");
    if (!WriteConfiguredValues({{"general", "paging_minus_equal", enabled ? "true" : "false"},
                                {"input", "word_to_character", word_enabled ? "true" : "false"}}))
    {
        return false;
    }
    g_paging_minus_equal_enabled = enabled;
    g_word_to_character_enabled = word_enabled;
    return true;
}

bool GetConfiguredPagingTabEnabled()
{
    return g_paging_tab_enabled;
}

bool SetConfiguredPagingTabEnabled(bool enabled)
{
    if (!WriteConfiguredValue("general", "paging_tab", enabled ? "true" : "false"))
    {
        return false;
    }
    g_paging_tab_enabled = enabled;
    return true;
}

bool GetConfiguredPagingCommaPeriodEnabled()
{
    return g_paging_comma_period_enabled;
}

bool SetConfiguredPagingCommaPeriodEnabled(bool enabled)
{
    if (!WriteConfiguredValue("general", "paging_comma_period", enabled ? "true" : "false"))
    {
        return false;
    }
    g_paging_comma_period_enabled = enabled;
    return true;
}

bool GetConfiguredPagingBracketsEnabled()
{
    return g_paging_brackets_enabled;
}

bool SetConfiguredPagingBracketsEnabled(bool enabled)
{
    const bool word_enabled = g_word_to_character_enabled && !(enabled && g_word_to_character_keys == "brackets");
    if (!WriteConfiguredValues({{"general", "paging_brackets", enabled ? "true" : "false"},
                                {"input", "word_to_character", word_enabled ? "true" : "false"}}))
    {
        return false;
    }
    g_paging_brackets_enabled = enabled;
    g_word_to_character_enabled = word_enabled;
    return true;
}

std::wstring FormatPagingCommaPeriodWorkerPayload()
{
    // data[0] = paging flag for legacy clients; "|style" is ignored by old TSF.
    // The style is the effective one: raw turns into pinyin while shuangpin shows its quanpin.
    return (g_paging_comma_period_enabled ? L"1|" : L"0|") + string_to_wstring(GlobalSettings::getTsfPreeditStyle());
}

bool GetConfiguredPagingPageUpDownEnabled()
{
    return g_paging_page_up_down_enabled;
}

bool SetConfiguredPagingPageUpDownEnabled(bool enabled)
{
    if (!WriteConfiguredValue("general", "paging_page_up_down", enabled ? "true" : "false"))
    {
        return false;
    }
    g_paging_page_up_down_enabled = enabled;
    return true;
}

bool GetConfiguredPagingMouseWheelEnabled()
{
    return g_paging_mouse_wheel_enabled;
}

bool SetConfiguredPagingMouseWheelEnabled(bool enabled)
{
    if (!WriteConfiguredValue("general", "paging_mouse_wheel", enabled ? "true" : "false"))
    {
        return false;
    }
    g_paging_mouse_wheel_enabled = enabled;
    return true;
}

bool GetConfiguredCandidateArrowNavigationEnabled()
{
    return g_candidate_arrow_navigation_enabled;
}

bool SetConfiguredCandidateArrowNavigationEnabled(bool enabled)
{
    if (!WriteConfiguredValue("general", "candidate_arrow_navigation", enabled ? "true" : "false"))
    {
        return false;
    }
    g_candidate_arrow_navigation_enabled = enabled;
    return true;
}

bool GetConfiguredCandidateHorizontalArrowNavigationEnabled()
{
    return g_candidate_horizontal_arrow_navigation_enabled;
}

bool SetConfiguredCandidateHorizontalArrowNavigationEnabled(bool enabled)
{
    if (!WriteConfiguredValue("general", "candidate_horizontal_arrow_navigation", enabled ? "true" : "false"))
    {
        return false;
    }
    g_candidate_horizontal_arrow_navigation_enabled = enabled;
    return true;
}

std::wstring FormatHorizontalArrowSelectionWorkerPayload()
{
    return g_candidate_horizontal_arrow_navigation_enabled && g_candidate_window_layout == "horizontal" ? L"1" : L"0";
}

bool GetConfiguredWordToCharacterEnabled()
{
    return g_word_to_character_enabled;
}

std::string GetConfiguredWordToCharacterKeys()
{
    return g_word_to_character_keys;
}

static bool SetWordToCharacterConfig(bool enabled, const std::string &keys)
{
    if (keys != "brackets" && keys != "minus_equal")
        return false;
    const bool brackets = g_paging_brackets_enabled && !(enabled && keys == "brackets");
    const bool minus_equal = g_paging_minus_equal_enabled && !(enabled && keys == "minus_equal");
    if (!WriteConfiguredValues({{"input", "word_to_character", enabled ? "true" : "false"},
                                {"input", "word_to_character_keys", EscapeTomlBasicString(keys)},
                                {"general", "paging_brackets", brackets ? "true" : "false"},
                                {"general", "paging_minus_equal", minus_equal ? "true" : "false"}}))
    {
        return false;
    }
    g_word_to_character_enabled = enabled;
    g_word_to_character_keys = keys;
    g_paging_brackets_enabled = brackets;
    g_paging_minus_equal_enabled = minus_equal;
    return true;
}

bool SetConfiguredWordToCharacterEnabled(bool enabled)
{
    return SetWordToCharacterConfig(enabled, g_word_to_character_keys);
}

bool SetConfiguredWordToCharacterKeys(const std::string &keys)
{
    return SetWordToCharacterConfig(g_word_to_character_enabled, keys);
}

bool GetConfiguredSmartPunctuationEnabled()
{
    return g_smart_punctuation_enabled;
}

bool SetConfiguredSmartPunctuationEnabled(bool enabled)
{
    if (!WriteConfiguredValue("input", "smart_punctuation", enabled ? "true" : "false"))
    {
        return false;
    }
    g_smart_punctuation_enabled = enabled;
    return true;
}

bool GetConfiguredSmartPunctuationRepeatToChineseEnabled()
{
    return g_smart_punctuation_repeat_to_chinese_enabled;
}

bool SetConfiguredSmartPunctuationRepeatToChineseEnabled(bool enabled)
{
    if (!WriteConfiguredValue("input", "smart_punctuation_repeat_to_chinese", enabled ? "true" : "false"))
    {
        return false;
    }
    g_smart_punctuation_repeat_to_chinese_enabled = enabled;
    return true;
}

bool GetConfiguredSmartPunctuationSpaceConvertEnabled()
{
    return g_smart_punctuation_space_convert_enabled;
}

bool SetConfiguredSmartPunctuationSpaceConvertEnabled(bool enabled)
{
    if (!WriteConfiguredValue("input", "smart_punctuation_space_convert", enabled ? "true" : "false"))
    {
        return false;
    }
    g_smart_punctuation_space_convert_enabled = enabled;
    return true;
}

bool GetConfiguredSmartPunctuationDirectDigitEnabled()
{
    return g_smart_punctuation_direct_digit_enabled;
}

bool SetConfiguredSmartPunctuationDirectDigitEnabled(bool enabled)
{
    if (!WriteConfiguredValue("input", "smart_punctuation_direct_digit", enabled ? "true" : "false"))
    {
        return false;
    }
    g_smart_punctuation_direct_digit_enabled = enabled;
    return true;
}

bool GetConfiguredSmartPunctuationDirectLetterEnabled()
{
    return g_smart_punctuation_direct_letter_enabled;
}

bool SetConfiguredSmartPunctuationDirectLetterEnabled(bool enabled)
{
    if (!WriteConfiguredValue("input", "smart_punctuation_direct_letter", enabled ? "true" : "false"))
    {
        return false;
    }
    g_smart_punctuation_direct_letter_enabled = enabled;
    return true;
}

bool GetConfiguredPairedPunctuationEnabled()
{
    return g_paired_punctuation_enabled;
}

bool SetConfiguredPairedPunctuationEnabled(bool enabled)
{
    if (!WriteConfiguredValue("input", "paired_punctuation", enabled ? "true" : "false"))
    {
        return false;
    }
    g_paired_punctuation_enabled = enabled;
    return true;
}

bool GetConfiguredEnumerationCommaBackslashEnabled()
{
    return g_enumeration_comma_backslash_enabled;
}

bool SetConfiguredEnumerationCommaBackslashEnabled(bool enabled)
{
    // 至少留一个：设置页取消不掉最后一个，这里同样不收。
    if (!enabled && !g_enumeration_comma_slash_enabled)
    {
        return false;
    }
    if (!WriteConfiguredValue("input", "enumeration_comma_backslash", enabled ? "true" : "false"))
    {
        return false;
    }
    g_enumeration_comma_backslash_enabled = enabled;
    return true;
}

bool GetConfiguredEnumerationCommaSlashEnabled()
{
    return g_enumeration_comma_slash_enabled;
}

bool SetConfiguredEnumerationCommaSlashEnabled(bool enabled)
{
    if (!enabled && !g_enumeration_comma_backslash_enabled)
    {
        return false;
    }
    if (!WriteConfiguredValue("input", "enumeration_comma_slash", enabled ? "true" : "false"))
    {
        return false;
    }
    g_enumeration_comma_slash_enabled = enabled;
    return true;
}

std::wstring FormatEnumerationCommaWorkerPayload()
{
    return std::wstring(1, FanyImeEnumerationComma::PayloadFor(g_enumeration_comma_backslash_enabled,
                                                               g_enumeration_comma_slash_enabled));
}

const std::string &GetConfiguredPunctuationLock()
{
    return g_punctuation_lock;
}

bool SetConfiguredPunctuationLock(const std::string &lock)
{
    if (lock != "follow" && lock != "chinese" && lock != "english")
    {
        return false;
    }
    if (!WriteConfiguredValue("input", "punctuation_lock", EscapeTomlBasicString(lock)))
    {
        return false;
    }
    g_punctuation_lock = lock;
    return true;
}

std::wstring FormatPunctuationLockWorkerPayload()
{
    if (g_punctuation_lock == "chinese")
    {
        return L"1";
    }
    if (g_punctuation_lock == "english")
    {
        return L"2";
    }
    return L"0";
}
