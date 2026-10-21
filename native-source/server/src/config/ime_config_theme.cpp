// 明暗主题：全局主题模式、各界面的主题覆盖，以及按系统「应用使用浅色主题」解析出实际主题。
#include "config/ime_config_internal.h"
#include <Windows.h>
#include <winreg.h>
#include <string>

using namespace ime_config_detail;

namespace
{
std::string NormalizeThemeMode(const std::string &mode)
{
    if (mode == "light" || mode == "system")
        return mode;
    if (mode == "auto")
        return "system";
    return "dark";
}

std::string NormalizeSurfaceTheme(const std::string &theme)
{
    if (theme == "light" || theme == "dark" || theme == "follow")
        return theme;
    return "follow";
}

bool SetSurfaceThemeValue(const char *key, const std::string &theme, std::string &target)
{
    const std::string normalized = NormalizeSurfaceTheme(theme);
    if (theme != "light" && theme != "dark" && theme != "follow")
        return false;
    if (!WriteConfiguredValue("appearance", key, EscapeTomlBasicString(normalized)))
        return false;
    target = normalized;
    return true;
}
} // namespace

bool IsSystemAppsLightTheme()
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0,
                      KEY_READ, &key) != ERROR_SUCCESS)
    {
        return false;
    }
    DWORD value = 0;
    DWORD size = sizeof(value);
    const LONG result =
        RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, nullptr, reinterpret_cast<LPBYTE>(&value), &size);
    RegCloseKey(key);
    return result == ERROR_SUCCESS && value != 0;
}

std::string ResolveConfiguredTheme(const std::string &surface_theme)
{
    const std::string surface = NormalizeSurfaceTheme(surface_theme);
    if (surface == "light" || surface == "dark")
        return surface;
    if (g_theme_mode == "light")
        return "light";
    if (g_theme_mode == "system" || g_theme_mode == "auto")
        return IsSystemAppsLightTheme() ? "light" : "dark";
    return "dark";
}

const std::string &GetConfiguredThemeMode()
{
    return g_theme_mode;
}

bool SetConfiguredThemeMode(const std::string &mode)
{
    if (mode != "dark" && mode != "light" && mode != "system" && mode != "auto")
        return false;
    const std::string normalized = NormalizeThemeMode(mode);
    if (!WriteConfiguredValue("appearance", "theme_mode", EscapeTomlBasicString(normalized)))
        return false;
    g_theme_mode = normalized;
    return true;
}

const std::string &GetConfiguredThemeSettings()
{
    return g_theme_settings;
}

bool SetConfiguredThemeSettings(const std::string &theme)
{
    return SetSurfaceThemeValue("theme_settings", theme, g_theme_settings);
}

const std::string &GetConfiguredThemeCand()
{
    return g_theme_cand;
}

bool SetConfiguredThemeCand(const std::string &theme)
{
    return SetSurfaceThemeValue("theme_cand", theme, g_theme_cand);
}

const std::string &GetConfiguredThemeFtb()
{
    return g_theme_ftb;
}

bool SetConfiguredThemeFtb(const std::string &theme)
{
    return SetSurfaceThemeValue("theme_ftb", theme, g_theme_ftb);
}

const std::string &GetConfiguredThemeMenu()
{
    return g_theme_menu;
}

bool SetConfiguredThemeMenu(const std::string &theme)
{
    return SetSurfaceThemeValue("theme_menu", theme, g_theme_menu);
}

const std::string &GetConfiguredThemeEmoji()
{
    return g_theme_emoji;
}

bool SetConfiguredThemeEmoji(const std::string &theme)
{
    return SetSurfaceThemeValue("theme_emoji", theme, g_theme_emoji);
}

const std::string &GetConfiguredThemeScreenKeyboard()
{
    return g_theme_screen_keyboard;
}

bool SetConfiguredThemeScreenKeyboard(const std::string &theme)
{
    return SetSurfaceThemeValue("theme_screen_keyboard", theme, g_theme_screen_keyboard);
}

const std::string &GetConfiguredThemeHandwriting()
{
    return g_theme_handwriting;
}

bool SetConfiguredThemeHandwriting(const std::string &theme)
{
    return SetSurfaceThemeValue("theme_handwriting", theme, g_theme_handwriting);
}

const std::string &GetConfiguredThemeVoice()
{
    return g_theme_voice;
}

bool SetConfiguredThemeVoice(const std::string &theme)
{
    return SetSurfaceThemeValue("theme_voice", theme, g_theme_voice);
}
