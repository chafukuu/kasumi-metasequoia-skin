#include "window/candidate_skin_palette.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace
{
std::string TrimCopy(std::string text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.erase(text.begin());
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.pop_back();
    return text;
}

BYTE ColorByte(float value)
{
    return static_cast<BYTE>(std::lround((std::clamp)(value, 0.0f, 1.0f) * 255.0f));
}

D2D1_COLOR_F CompositeColor(D2D1_COLOR_F color, D2D1_COLOR_F background)
{
    const float alpha = (std::clamp)(color.a, 0.0f, 1.0f);
    return D2D1::ColorF(color.r * alpha + background.r * (1.0f - alpha),
                        color.g * alpha + background.g * (1.0f - alpha),
                        color.b * alpha + background.b * (1.0f - alpha), 1.0f);
}
} // namespace

D2D1_COLOR_F CandidateColorFromRgb(UINT rgb, float alpha)
{
    return D2D1::ColorF(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, alpha);
}

D2D1_COLOR_F ParseCandidateCssColor(const std::string &text, D2D1_COLOR_F fallback)
{
    std::string value = TrimCopy(text);
    if (value.empty() || value == "auto" || value == "none" || value == "transparent")
        return value == "transparent" ? D2D1::ColorF(0, 0.0f) : fallback;
    if (value.rfind("rgba(", 0) == 0 || value.rfind("rgb(", 0) == 0)
    {
        const auto open = value.find('(');
        const auto close = value.rfind(')');
        if (open != std::string::npos && close != std::string::npos && close > open)
        {
            std::string inner = value.substr(open + 1, close - open - 1);
            std::replace(inner.begin(), inner.end(), ',', ' ');
            std::istringstream stream(inner);
            float r = 0;
            float g = 0;
            float b = 0;
            float a = 1.0f;
            if (stream >> r >> g >> b)
            {
                stream >> a;
                return D2D1::ColorF(r / 255.0f, g / 255.0f, b / 255.0f, a);
            }
        }
        return fallback;
    }
    if (value[0] == '#')
        value.erase(value.begin());
    // 与 CSS 一致：#rgb、#rgba、#rrggbb、#rrggbbaa。先整体校验，stoul 遇到非十六进制字符会截断而不是报错。
    if (!std::all_of(value.begin(), value.end(), [](unsigned char ch) { return std::isxdigit(ch) != 0; }))
        return fallback;
    if (value.size() == 3 || value.size() == 4)
    {
        std::string expanded;
        for (const char digit : value)
            expanded.append(2, digit);
        value = expanded;
    }
    if (value.size() == 6)
        return CandidateColorFromRgb(static_cast<UINT>(std::stoul(value, nullptr, 16)));
    if (value.size() == 8)
    {
        const unsigned long packed = std::stoul(value, nullptr, 16);
        return CandidateColorFromRgb(static_cast<UINT>((packed >> 8) & 0xFFFFFFu),
                                     static_cast<float>(packed & 0xFFu) / 255.0f);
    }
    return fallback;
}

CandidateSkinPalette ResolveCandidateSkinPalette(const std::string &skinId, bool light,
                                                 const std::string &configuredTextColor,
                                                 const CandidateSkinCatalog::CandidateColors *packageColors,
                                                 const std::string &baseSkinId)
{
    return ResolveCandidateSkinPalette(CandidateSkinCatalog::FactoryLook(baseSkinId.empty() ? skinId : baseSkinId),
                                       light, configuredTextColor, packageColors);
}

CandidateSkinPalette ResolveCandidateSkinPalette(const CandidateSkinCatalog::Package &baseLook, bool light,
                                                 const std::string &configuredTextColor,
                                                 const CandidateSkinCatalog::CandidateColors *packageColors)
{
    // 底色、描边与文字取自基底清单；清单没写时（不该发生）退回不透明的深 / 浅中性色，免得画成透明。
    CandidateSkinPalette palette{
        CandidateColorFromRgb(light ? 0xFFFFFF : 0x202020),
        light ? D2D1::ColorF(0, 0.12f) : CandidateColorFromRgb(0x9B9B9B, 0x2E / 255.0f),
        CandidateColorFromRgb(light ? 0x1A1A1A : 0xE9E8E8),
    };
    const CandidateSkinCatalog::CandidateColors &baseColors = light ? baseLook.light : baseLook.dark;
    if (!baseColors.surface.empty())
        palette.surface = ParseCandidateCssColor(baseColors.surface, palette.surface);
    if (!baseColors.border.empty())
        palette.border = ParseCandidateCssColor(baseColors.border, palette.border);
    if (!baseColors.text.empty())
        palette.text = ParseCandidateCssColor(baseColors.text, palette.text);
    if (packageColors)
    {
        if (!packageColors->surface.empty())
            palette.surface = ParseCandidateCssColor(packageColors->surface, palette.surface);
        if (!packageColors->border.empty())
            palette.border = ParseCandidateCssColor(packageColors->border, palette.border);
        if (!packageColors->text.empty())
            palette.text = ParseCandidateCssColor(packageColors->text, palette.text);
    }
    palette.candidateText = palette.text;
    palette.preeditText = palette.text;
    if (packageColors)
    {
        palette.candidateText = ParseCandidateCssColor(packageColors->candidateText, palette.candidateText);
        palette.preeditText = ParseCandidateCssColor(packageColors->preeditText, palette.preeditText);
    }
    palette.text = ParseCandidateCssColor(configuredTextColor, palette.text);
    palette.candidateText = ParseCandidateCssColor(configuredTextColor, palette.candidateText);
    palette.preeditText = ParseCandidateCssColor(configuredTextColor, palette.preeditText);
    return palette;
}

float CandidateSkinBaseItemRadiusDip(const std::string &baseSkinId)
{
    return static_cast<float>(CandidateSkinCatalog::FactoryLook(baseSkinId).itemCornerRadiusDip.value_or(4.0));
}

CandidateSkinPalette FlattenCandidateSkinPaletteForGdi(const CandidateSkinPalette &palette,
                                                       D2D1_COLOR_F fallbackSurface)
{
    const D2D1_COLOR_F surface = CompositeColor(palette.surface, fallbackSurface);
    return {surface, CompositeColor(palette.border, surface), CompositeColor(palette.text, surface)};
}

COLORREF FlattenCandidateColor(D2D1_COLOR_F color, D2D1_COLOR_F background)
{
    const D2D1_COLOR_F flattened = CompositeColor(color, background);
    return RGB(ColorByte(flattened.r), ColorByte(flattened.g), ColorByte(flattened.b));
}
