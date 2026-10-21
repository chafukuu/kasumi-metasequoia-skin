#pragma once

#include <d2d1.h>
#include <string>

// Colors and type scale of the Direct2D settings window. Every value was taken from a CSS
// custom property of the former web settings page (styles/variables.css, see
// native_settings_pages.h); the property name is noted where it is not obvious.
namespace native_settings
{
constexpr D2D1_COLOR_F Hex(unsigned rgb, float alpha = 1.0f)
{
    return {static_cast<float>((rgb >> 16) & 0xFF) / 255.0f, static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
            static_cast<float>(rgb & 0xFF) / 255.0f, alpha};
}

constexpr D2D1_COLOR_F Rgba(int r, int g, int b, float alpha = 1.0f)
{
    return {static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f, static_cast<float>(b) / 255.0f, alpha};
}

D2D1_COLOR_F WithAlpha(D2D1_COLOR_F color, float alpha);
// `top` composited over an opaque `bottom`, i.e. what the page shows for a translucent
// color sitting on the window background.
D2D1_COLOR_F Over(const D2D1_COLOR_F &top, const D2D1_COLOR_F &bottom);
// CSS `#rrggbb` / `#rgb` / `#rrggbbaa`; false when the text is not a hex color.
bool ParseCssColor(const std::string &text, D2D1_COLOR_F &color);
std::string ToCssHex(const D2D1_COLOR_F &color);

struct Palette
{
    bool light = false;

    D2D1_COLOR_F accent;                // --accent-color
    D2D1_COLOR_F accentStrong;          // --accent-strong
    D2D1_COLOR_F accentSoft;            // --accent-soft
    D2D1_COLOR_F accentSoftBorder;      // --accent-soft-border
    D2D1_COLOR_F chromeBg;              // --chrome-bg, the window background
    D2D1_COLOR_F sidebarHover;          // --sidebar-hover
    D2D1_COLOR_F itemActiveHover;       // --item-active-hover
    D2D1_COLOR_F text;                  // --text-color
    D2D1_COLOR_F textSecondary;         // --text-secondary
    D2D1_COLOR_F textMuted;             // --text-muted
    D2D1_COLOR_F textSubtle;            // --text-subtle
    D2D1_COLOR_F sidebarItem;           // --sidebar-item-color
    D2D1_COLOR_F border;                // --border-color
    D2D1_COLOR_F borderStrong;          // --border-strong
    D2D1_COLOR_F cardBg;                // --card-bg (translucent in the dark theme)
    D2D1_COLOR_F cardBgSolid;           // --card-bg-solid
    D2D1_COLOR_F surfaceRaised;         // --surface-raised
    D2D1_COLOR_F surfaceSunken;         // --surface-sunken
    D2D1_COLOR_F surfaceSubtle;         // --surface-subtle
    D2D1_COLOR_F dropdownBg;            // --dropdown-bg
    D2D1_COLOR_F dropdownMenuBg;        // --dropdown-menu-bg
    D2D1_COLOR_F dropdownMenuBorder;    // --dropdown-menu-border
    D2D1_COLOR_F dropdownMenuHighlight; // inset 1px of --dropdown-menu-shadow
    D2D1_COLOR_F dropdownItem;          // --dropdown-item-color
    D2D1_COLOR_F dropdownItemHover;     // --dropdown-item-hover-bg
    D2D1_COLOR_F controlBg;             // --control-bg
    D2D1_COLOR_F controlBorder;         // --control-border
    D2D1_COLOR_F buttonSecondaryBg;     // --button-secondary-bg
    D2D1_COLOR_F buttonSecondaryHover;  // --button-secondary-hover
    D2D1_COLOR_F buttonSecondaryText;   // --button-secondary-text
    D2D1_COLOR_F kbdBg;                 // --kbd-bg
    D2D1_COLOR_F kbdBorder;             // --kbd-border
    D2D1_COLOR_F kbdBorderBottom;       // --kbd-border-bottom
    D2D1_COLOR_F kbdText;               // --kbd-text
    D2D1_COLOR_F kbdShadow;             // --kbd-shadow
    D2D1_COLOR_F tableHeaderBg;         // --table-header-bg
    D2D1_COLOR_F tableBorder;           // --table-border
    D2D1_COLOR_F dangerText;            // --danger-text
    D2D1_COLOR_F dangerHover;           // --danger-hover
    D2D1_COLOR_F divider;               // --divider-color
    D2D1_COLOR_F titlebarBtnHover;      // --titlebar-btn-hover
    D2D1_COLOR_F titlebarBtnActive;     // --titlebar-btn-active
    D2D1_COLOR_F tooltipBg;             // --tooltip-bg
    D2D1_COLOR_F tooltipBorder;         // --tooltip-border
    D2D1_COLOR_F tooltipText;           // --tooltip-color
    D2D1_COLOR_F toggleOff;             // --toggle-off-bg
    D2D1_COLOR_F toggleOn;              // .ftb-toggle-btn.active
    D2D1_COLOR_F skinPreviewStageBg;    // --skin-preview-stage-bg
    D2D1_COLOR_F overlayBg;             // --overlay-bg
    D2D1_COLOR_F toastBg;               // --toast-bg
    D2D1_COLOR_F toastBorder;           // --toast-border
    D2D1_COLOR_F toastText;             // --toast-text
    D2D1_COLOR_F candBg;                // --cand-bg
    D2D1_COLOR_F candBorder;            // --cand-border
    D2D1_COLOR_F candText;              // --cand-text
    D2D1_COLOR_F candNum;               // --cand-num
    D2D1_COLOR_F candHover;             // --cand-hover
    D2D1_COLOR_F candSelected;          // --cand-selected
    D2D1_COLOR_F scrollbarThumb;        // --scrollbar-thumb
    D2D1_COLOR_F scrollbarThumbHover;   // --scrollbar-thumb-hover
    D2D1_COLOR_F checkboxBorder;        // .check-option input border
    D2D1_COLOR_F checkboxBorderHover;   // .check-option input:hover border
    D2D1_COLOR_F checkboxChecked;       // .check-option input:checked
    D2D1_COLOR_F radioBorder;           // .radio-option input border
    D2D1_COLOR_F closeHover;            // .window-button.close:hover
    D2D1_COLOR_F closeActive;           // .window-button.close:active

    // --card-shadow as gaussian passes (sigma = blur / 2).
    struct Shadow
    {
        float sigma;
        float alpha;
        float offsetY;
    };
    Shadow cardShadow[2] = {};
    int cardShadowCount = 0;
    Shadow menuShadow[2] = {}; // --dropdown-menu-shadow (outer parts)
};

const Palette &DarkPalette();
const Palette &LightPalette();
const Palette &CurrentPalette();
void SetCurrentPalette(bool light);

// Body text family: the first installed of the page's `font-family` stack
// ("Noto Sans SC", "Microsoft YaHei", "Segoe UI Variable", "Segoe UI").
const std::wstring &UiFontFamily();
// `system-ui`: what form controls (<button>, <input>, <select>) render in unless the page
// sets `font: inherit`; the system message font ("Microsoft YaHei UI" on Chinese Windows).
const std::wstring &SystemFontFamily();
// Windows caption glyphs (Segoe Fluent Icons, or Segoe MDL2 Assets on Windows 10).
const std::wstring &IconFontFamily();
bool IsFontInstalled(const std::wstring &family);

// Device pixels per DIP of the window being laid out. The browser rounds border widths
// to whole device pixels before layout (1px at 150% becomes 1 device pixel), so boxes
// with borders come out slightly smaller than their CSS size; CssBorder reproduces that.
void SetLayoutScale(float scale);
float LayoutScale();
float CssBorder(float cssPixels = 1.0f);

// Type scale (variables.css :root).
constexpr float kFontPageTitle = 18.0f;
constexpr float kFontSectionTitle = 15.0f;
constexpr float kFontBody = 14.0f;
constexpr float kFontCaption = 12.0f;
// `body` sets no font-size, so anything that inherits it renders at 16px.
constexpr float kFontDefault = 16.0f;
constexpr float kLineHeightCaption = 1.6f;
} // namespace native_settings
