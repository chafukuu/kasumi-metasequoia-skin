#include "settings/native/native_settings_theme.h"

#include <windows.h>

#include <dwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace native_settings
{
namespace
{
Palette MakeDark()
{
    Palette p;
    p.light = false;
    p.accent = Hex(0x8E8CD8);
    p.accentStrong = Hex(0x6B69D6);
    p.accentSoft = Rgba(142, 140, 216, 0.16f);
    p.accentSoftBorder = Rgba(142, 140, 216, 0.25f);
    p.chromeBg = Hex(0x202020);
    p.sidebarHover = Rgba(255, 255, 255, 0.08f);
    p.itemActiveHover = Rgba(255, 255, 255, 0.06f);
    p.text = Hex(0xF3F3F3);
    p.textSecondary = Hex(0xC8C8C8);
    p.textMuted = Hex(0xA0A0A0);
    p.textSubtle = Hex(0x888888);
    p.sidebarItem = Hex(0xE6E6E6);
    p.border = Rgba(255, 255, 255, 0.08f);
    p.borderStrong = Rgba(255, 255, 255, 0.12f);
    p.cardBg = Rgba(38, 38, 38, 0.5f);
    p.cardBgSolid = Hex(0x242424);
    p.surfaceRaised = Hex(0x2D2D2D);
    p.surfaceSunken = Rgba(255, 255, 255, 0.045f);
    p.surfaceSubtle = Rgba(255, 255, 255, 0.035f);
    p.dropdownBg = Rgba(45, 45, 45, 0.85f);
    p.dropdownMenuBg = Rgba(50, 50, 54, 0.98f);
    p.dropdownMenuBorder = Rgba(255, 255, 255, 0.19f);
    p.dropdownMenuHighlight = Rgba(255, 255, 255, 0.07f);
    p.dropdownItem = Hex(0xEEEEEE);
    p.dropdownItemHover = Rgba(142, 140, 216, 0.2f);
    p.controlBg = Rgba(45, 45, 45, 0.8f);
    p.controlBorder = Rgba(255, 255, 255, 0.12f);
    p.buttonSecondaryBg = Hex(0x3B3B3B);
    p.buttonSecondaryHover = Hex(0x484848);
    p.buttonSecondaryText = Hex(0xFFFFFF);
    p.kbdBg = Hex(0x303030);
    p.kbdBorder = Hex(0x505050);
    p.kbdBorderBottom = Hex(0x656565);
    p.kbdText = Hex(0xE6E6E6);
    p.kbdShadow = Rgba(255, 255, 255, 0.08f);
    p.tableHeaderBg = Hex(0x292929);
    p.tableBorder = Hex(0x343434);
    p.dangerText = Hex(0xE8B4B4);
    p.dangerHover = Hex(0xFF7777);
    p.divider = Hex(0x908F8F, static_cast<float>(0x16) / 255.0f);
    p.titlebarBtnHover = Rgba(255, 255, 255, 0.06f);
    p.titlebarBtnActive = Rgba(255, 255, 255, 0.12f);
    p.tooltipBg = Rgba(22, 22, 22, 0.95f);
    p.tooltipBorder = Rgba(255, 255, 255, 0.12f);
    p.tooltipText = Hex(0xF3F3F3);
    p.toggleOff = Hex(0x555555);
    p.toggleOn = Hex(0x8E8CD8);
    p.skinPreviewStageBg = Rgba(18, 18, 18, 0.13f);
    p.overlayBg = Rgba(0, 0, 0, 0.55f);
    p.toastBg = Hex(0x2B2B2B);
    p.toastBorder = Hex(0x484848);
    p.toastText = Hex(0xF3F3F3);
    p.candBg = Hex(0x202020);
    p.candBorder = Hex(0x9B9B9B, static_cast<float>(0x2E) / 255.0f);
    p.candText = Hex(0xE9E8E8);
    p.candNum = Hex(0xE9E8E8, static_cast<float>(0x9D) / 255.0f);
    p.candHover = Hex(0x414141);
    p.candSelected = Hex(0x3E3E3E, static_cast<float>(0xB9) / 255.0f);
    p.scrollbarThumb = Rgba(180, 180, 180, 0.28f);
    p.scrollbarThumbHover = Rgba(220, 220, 220, 0.45f);
    p.checkboxBorder = Hex(0x8A8886);
    p.checkboxBorderHover = Hex(0xC8C6C4);
    p.checkboxChecked = Hex(0x8E8CD8, static_cast<float>(0xA7) / 255.0f);
    p.radioBorder = Hex(0x616161);
    p.closeHover = Hex(0xC42B1C);
    p.closeActive = Hex(0xA72216);
    // 0 1px 4px rgba(0, 0, 0, 0.25)
    p.cardShadow[0] = {2.0f, 0.25f, 1.0f};
    p.cardShadowCount = 1;
    // 0 18px 42px rgba(0, 0, 0, 0.58), 0 5px 14px rgba(0, 0, 0, 0.42)
    p.menuShadow[0] = {21.0f, 0.58f, 18.0f};
    p.menuShadow[1] = {7.0f, 0.42f, 5.0f};
    return p;
}

Palette MakeLight()
{
    Palette p;
    p.light = true;
    p.accent = Hex(0x6F6BC7);
    p.accentStrong = Hex(0x5B57B8);
    p.accentSoft = Rgba(111, 107, 199, 0.12f);
    p.accentSoftBorder = Rgba(111, 107, 199, 0.22f);
    p.chromeBg = Hex(0xF3F3F3);
    p.sidebarHover = Rgba(0, 0, 0, 0.04f);
    p.itemActiveHover = Rgba(0, 0, 0, 0.06f);
    p.text = Hex(0x242424);
    p.textSecondary = Hex(0x424242);
    p.textMuted = Hex(0x616161);
    p.textSubtle = Hex(0x757575);
    p.sidebarItem = Hex(0x242424);
    p.border = Rgba(0, 0, 0, 0.08f);
    p.borderStrong = Rgba(0, 0, 0, 0.12f);
    p.cardBg = Hex(0xFFFFFF);
    p.cardBgSolid = Hex(0xFFFFFF);
    p.surfaceRaised = Hex(0xFFFFFF);
    p.surfaceSunken = Hex(0xF5F5F5);
    p.surfaceSubtle = Hex(0xF7F7F7);
    p.dropdownBg = Hex(0xFFFFFF);
    p.dropdownMenuBg = Hex(0xFFFFFF);
    p.dropdownMenuBorder = Rgba(0, 0, 0, 0.17f);
    p.dropdownMenuHighlight = Rgba(255, 255, 255, 0.9f);
    p.dropdownItem = Hex(0x242424);
    p.dropdownItemHover = Rgba(111, 107, 199, 0.12f);
    p.controlBg = Hex(0xFFFFFF);
    p.controlBorder = Rgba(0, 0, 0, 0.14f);
    p.buttonSecondaryBg = Hex(0xF0F0F0);
    p.buttonSecondaryHover = Hex(0xE5E5E5);
    p.buttonSecondaryText = Hex(0x242424);
    p.kbdBg = Hex(0xF5F5F5);
    p.kbdBorder = Hex(0xD1D1D1);
    p.kbdBorderBottom = Hex(0xBDBDBD);
    p.kbdText = Hex(0x242424);
    p.kbdShadow = Rgba(0, 0, 0, 0.06f);
    p.tableHeaderBg = Hex(0xF7F7F7);
    p.tableBorder = Rgba(0, 0, 0, 0.08f);
    p.dangerText = Hex(0xB42318);
    p.dangerHover = Hex(0x912018);
    p.divider = Rgba(0, 0, 0, 0.08f);
    p.titlebarBtnHover = Rgba(0, 0, 0, 0.05f);
    p.titlebarBtnActive = Rgba(0, 0, 0, 0.08f);
    p.tooltipBg = Rgba(255, 255, 255, 0.98f);
    p.tooltipBorder = Rgba(0, 0, 0, 0.1f);
    p.tooltipText = Hex(0x242424);
    p.toggleOff = Hex(0xC8C8C8);
    p.toggleOn = Hex(0x8E8CD8);
    p.skinPreviewStageBg = Rgba(0, 0, 0, 0.03f);
    p.overlayBg = Rgba(0, 0, 0, 0.35f);
    p.toastBg = Hex(0xFFFFFF);
    p.toastBorder = Rgba(0, 0, 0, 0.12f);
    p.toastText = Hex(0x242424);
    p.candBg = Hex(0xFFFFFF);
    p.candBorder = Rgba(0, 0, 0, 0.12f);
    p.candText = Hex(0x1A1A1A);
    p.candNum = Rgba(26, 26, 26, 0.55f);
    p.candHover = Hex(0xECECEC);
    p.candSelected = Hex(0xE8E8E8);
    p.scrollbarThumb = Rgba(0, 0, 0, 0.28f);
    p.scrollbarThumbHover = Rgba(0, 0, 0, 0.45f);
    p.checkboxBorder = Hex(0x8A8886);
    p.checkboxBorderHover = Hex(0x605E5C);
    p.checkboxChecked = Hex(0x8E8CD8, static_cast<float>(0xA7) / 255.0f);
    p.radioBorder = Hex(0x616161);
    p.closeHover = Hex(0xC42B1C);
    p.closeActive = Hex(0xA72216);
    // 0 1px 2px rgba(0, 0, 0, 0.04), 0 1px 3px rgba(0, 0, 0, 0.06)
    p.cardShadow[0] = {1.0f, 0.04f, 1.0f};
    p.cardShadow[1] = {1.5f, 0.06f, 1.0f};
    p.cardShadowCount = 2;
    // 0 18px 38px rgba(30, 30, 40, 0.2), 0 4px 12px rgba(30, 30, 40, 0.12)
    p.menuShadow[0] = {19.0f, 0.2f, 18.0f};
    p.menuShadow[1] = {6.0f, 0.12f, 4.0f};
    return p;
}

bool g_light = false;

int HexDigit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}
} // namespace

D2D1_COLOR_F WithAlpha(D2D1_COLOR_F color, float alpha)
{
    color.a *= alpha;
    return color;
}

D2D1_COLOR_F Over(const D2D1_COLOR_F &top, const D2D1_COLOR_F &bottom)
{
    const float a = top.a;
    return {top.r * a + bottom.r * (1.0f - a), top.g * a + bottom.g * (1.0f - a), top.b * a + bottom.b * (1.0f - a),
            1.0f};
}

bool ParseCssColor(const std::string &text, D2D1_COLOR_F &color)
{
    std::string value = text;
    value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c) != 0; }),
                value.end());
    if (value.size() < 4 || value[0] != '#')
        return false;
    const std::string digits = value.substr(1);
    for (char c : digits)
    {
        if (HexDigit(c) < 0)
            return false;
    }
    auto pair = [&](size_t i) {
        return static_cast<float>(HexDigit(digits[i]) * 16 + HexDigit(digits[i + 1])) / 255.0f;
    };
    auto single = [&](size_t i) { return static_cast<float>(HexDigit(digits[i]) * 17) / 255.0f; };
    switch (digits.size())
    {
    case 3:
        color = {single(0), single(1), single(2), 1.0f};
        return true;
    case 4:
        color = {single(0), single(1), single(2), single(3)};
        return true;
    case 6:
        color = {pair(0), pair(2), pair(4), 1.0f};
        return true;
    case 8:
        color = {pair(0), pair(2), pair(4), pair(6)};
        return true;
    default:
        return false;
    }
}

std::string ToCssHex(const D2D1_COLOR_F &color)
{
    char buffer[8] = {};
    auto channel = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    std::snprintf(buffer, sizeof(buffer), "#%02x%02x%02x", channel(color.r), channel(color.g), channel(color.b));
    return buffer;
}

const Palette &DarkPalette()
{
    static const Palette palette = MakeDark();
    return palette;
}

const Palette &LightPalette()
{
    static const Palette palette = MakeLight();
    return palette;
}

const Palette &CurrentPalette()
{
    return g_light ? LightPalette() : DarkPalette();
}

void SetCurrentPalette(bool light)
{
    g_light = light;
}

bool IsFontInstalled(const std::wstring &family)
{
    static Microsoft::WRL::ComPtr<IDWriteFontCollection> collection = [] {
        Microsoft::WRL::ComPtr<IDWriteFactory> factory;
        Microsoft::WRL::ComPtr<IDWriteFontCollection> fonts;
        if (SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                          reinterpret_cast<IUnknown **>(factory.GetAddressOf()))))
        {
            factory->GetSystemFontCollection(fonts.GetAddressOf(), FALSE);
        }
        return fonts;
    }();
    if (!collection)
        return false;
    UINT32 index = 0;
    BOOL exists = FALSE;
    return SUCCEEDED(collection->FindFamilyName(family.c_str(), &index, &exists)) && exists;
}

const std::wstring &UiFontFamily()
{
    static const std::wstring family = [] {
        for (const wchar_t *candidate : {L"Noto Sans SC", L"Microsoft YaHei", L"Segoe UI Variable", L"Segoe UI"})
        {
            if (IsFontInstalled(candidate))
                return std::wstring(candidate);
        }
        return std::wstring(L"Microsoft YaHei");
    }();
    return family;
}

const std::wstring &SystemFontFamily()
{
    static const std::wstring family = [] {
        NONCLIENTMETRICSW metrics{sizeof(metrics)};
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0) &&
            metrics.lfMessageFont.lfFaceName[0] != L'\0')
            return std::wstring(metrics.lfMessageFont.lfFaceName);
        return std::wstring(L"Segoe UI");
    }();
    return family;
}

namespace
{
float g_layoutScale = 1.0f;
} // namespace

void SetLayoutScale(float scale)
{
    g_layoutScale = scale > 0.0f ? scale : 1.0f;
}

float LayoutScale()
{
    return g_layoutScale;
}

float CssBorder(float cssPixels)
{
    if (cssPixels <= 0.0f)
        return 0.0f;
    const float device = cssPixels * g_layoutScale;
    // Widths under one device pixel still paint one; wider ones lose the fraction.
    return (device < 1.0f ? 1.0f : std::floor(device)) / g_layoutScale;
}

const std::wstring &IconFontFamily()
{
    static const std::wstring family =
        IsFontInstalled(L"Segoe Fluent Icons") ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
    return family;
}
} // namespace native_settings
