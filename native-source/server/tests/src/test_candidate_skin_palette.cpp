#include "tests/includes/test_framework.h"
#include "window/candidate_skin_palette.h"

#include <cmath>

TEST_CASE(candidate_skin_palette_resolves_builtin_theme_and_text_override)
{
    const CandidateSkinPalette dark = ResolveCandidateSkinPalette("wechat", false, "auto");
    REQUIRE_EQ(FlattenCandidateColor(dark.surface, dark.surface), RGB(21, 21, 21));
    REQUIRE_EQ(FlattenCandidateColor(dark.border, dark.surface), RGB(41, 41, 41));
    REQUIRE_EQ(FlattenCandidateColor(dark.text, dark.surface), RGB(183, 183, 183));

    const CandidateSkinPalette light = ResolveCandidateSkinPalette("graphite", true, "#123456");
    REQUIRE_EQ(FlattenCandidateColor(light.surface, light.surface), RGB(251, 251, 252));
    REQUIRE_EQ(FlattenCandidateColor(light.border, light.surface), RGB(226, 229, 233));
    REQUIRE_EQ(FlattenCandidateColor(light.text, light.surface), RGB(18, 52, 86));
}

TEST_CASE(candidate_skin_palette_matches_willow_green_light_and_dark_preview)
{
    const CandidateSkinPalette dark = ResolveCandidateSkinPalette("willow_green", false, "auto");
    REQUIRE_EQ(FlattenCandidateColor(dark.surface, dark.surface), RGB(45, 47, 46));
    REQUIRE_EQ(dark.border.a, 0.0f);
    REQUIRE_EQ(FlattenCandidateColor(dark.text, dark.surface), RGB(216, 219, 216));

    const CandidateSkinPalette light = ResolveCandidateSkinPalette("willow_green", true, "auto");
    REQUIRE_EQ(FlattenCandidateColor(light.surface, light.surface), RGB(244, 245, 243));
    REQUIRE_EQ(light.border.a, 0.0f);
    REQUIRE_EQ(FlattenCandidateColor(light.text, light.surface), RGB(52, 57, 54));
}

TEST_CASE(candidate_skin_palette_matches_autumn_osmanthus_light_and_dark_preview)
{
    const CandidateSkinPalette dark = ResolveCandidateSkinPalette("autumn_osmanthus", false, "auto");
    REQUIRE_EQ(FlattenCandidateColor(dark.surface, dark.surface), RGB(125, 146, 159));
    REQUIRE_EQ(dark.border.a, 0.0f);
    REQUIRE_EQ(FlattenCandidateColor(dark.text, dark.surface), RGB(245, 248, 250));

    const CandidateSkinPalette light = ResolveCandidateSkinPalette("autumn_osmanthus", true, "auto");
    REQUIRE_EQ(FlattenCandidateColor(light.surface, light.surface), RGB(214, 236, 240));
    REQUIRE_EQ(light.border.a, 0.0f);
    REQUIRE_EQ(FlattenCandidateColor(light.text, light.surface), RGB(31, 49, 56));
}

TEST_CASE(candidate_skin_palette_matches_microsoft_light_and_dark_preview)
{
    const CandidateSkinPalette dark = ResolveCandidateSkinPalette("microsoft", false, "auto");
    REQUIRE_EQ(FlattenCandidateColor(dark.surface, dark.surface), RGB(44, 44, 44));
    REQUIRE_EQ(FlattenCandidateColor(dark.border, dark.surface), RGB(28, 28, 28));
    REQUIRE_EQ(FlattenCandidateColor(dark.text, dark.surface), RGB(255, 255, 255));

    const CandidateSkinPalette light = ResolveCandidateSkinPalette("microsoft", true, "auto");
    REQUIRE_EQ(FlattenCandidateColor(light.surface, light.surface), RGB(249, 249, 249));
    REQUIRE(std::abs(light.border.a - 0.1f) < 0.001f);
    REQUIRE_EQ(FlattenCandidateColor(light.text, light.surface), RGB(26, 26, 26));
}

TEST_CASE(candidate_skin_palette_custom_missing_colors_inherit_package_base)
{
    CandidateSkinCatalog::CandidateColors colors;
    colors.surface = "#102030";
    const CandidateSkinPalette palette = ResolveCandidateSkinPalette("custom", false, "auto", &colors, "willow_green");

    REQUIRE_EQ(FlattenCandidateColor(palette.surface, palette.surface), RGB(16, 32, 48));
    REQUIRE_EQ(palette.border.a, 0.0f);
    REQUIRE_EQ(FlattenCandidateColor(palette.text, palette.surface), RGB(216, 219, 216));
}

TEST_CASE(candidate_skin_palette_applies_custom_colors_with_builtin_fallbacks)
{
    CandidateSkinCatalog::CandidateColors colors;
    colors.surface = "#102030";
    colors.text = "rgba(255, 255, 255, 0.5)";
    const CandidateSkinPalette palette = ResolveCandidateSkinPalette("custom", false, "auto", &colors);

    REQUIRE_EQ(FlattenCandidateColor(palette.surface, palette.surface), RGB(16, 32, 48));
    REQUIRE_EQ(FlattenCandidateColor(palette.border, palette.surface), RGB(41, 54, 67));
    REQUIRE_EQ(FlattenCandidateColor(palette.text, palette.surface), RGB(136, 144, 152));
}

// Skin authors write CSS hex notation in every form, so D2D must read them all instead of silently falling back.
TEST_CASE(candidate_skin_palette_parses_every_css_hex_notation)
{
    const D2D1_COLOR_F fallback = D2D1::ColorF(0.0f, 0.0f, 1.0f, 1.0f);
    const D2D1_COLOR_F black = D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f);
    REQUIRE_EQ(FlattenCandidateColor(ParseCandidateCssColor("#f80", fallback), black), RGB(255, 136, 0));
    REQUIRE_EQ(FlattenCandidateColor(ParseCandidateCssColor("#ff8800", fallback), black), RGB(255, 136, 0));
    REQUIRE_EQ(FlattenCandidateColor(ParseCandidateCssColor("#FF8800", fallback), black), RGB(255, 136, 0));

    const D2D1_COLOR_F shortAlpha = ParseCandidateCssColor("#f808", fallback);
    REQUIRE_EQ(FlattenCandidateColor(D2D1::ColorF(shortAlpha.r, shortAlpha.g, shortAlpha.b), black), RGB(255, 136, 0));
    REQUIRE(std::abs(shortAlpha.a - 0x88 / 255.0f) < 0.001f);
    const D2D1_COLOR_F longAlpha = ParseCandidateCssColor("#ff880080", fallback);
    REQUIRE(std::abs(longAlpha.a - 0x80 / 255.0f) < 0.001f);
}

TEST_CASE(candidate_skin_palette_rejects_malformed_hex_colors)
{
    const D2D1_COLOR_F fallback = D2D1::ColorF(0.0f, 0.0f, 1.0f, 1.0f);
    const D2D1_COLOR_F black = D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f);
    for (const char *text : {"#12zz56", "#ggg", "#12345", "#-12345", "#"})
        REQUIRE_EQ(FlattenCandidateColor(ParseCandidateCssColor(text, fallback), black), RGB(0, 0, 255));
}

TEST_CASE(candidate_skin_palette_flattens_alpha_for_gdi)
{
    REQUIRE_EQ(FlattenCandidateColor(D2D1::ColorF(1.0f, 0.0f, 0.0f, 0.25f), D2D1::ColorF(0.0f, 0.0f, 1.0f, 1.0f)),
               RGB(64, 0, 191));
}

TEST_CASE(candidate_skin_palette_flattens_translucent_surface_before_border_and_text)
{
    const CandidateSkinPalette palette{D2D1::ColorF(1.0f, 0.0f, 0.0f, 0.5f), D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.25f),
                                       D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.5f)};
    const CandidateSkinPalette flattened =
        FlattenCandidateSkinPaletteForGdi(palette, D2D1::ColorF(0.0f, 0.0f, 1.0f, 1.0f));

    REQUIRE_EQ(FlattenCandidateColor(flattened.surface, flattened.surface), RGB(128, 0, 128));
    REQUIRE_EQ(FlattenCandidateColor(flattened.border, flattened.surface), RGB(96, 0, 96));
    REQUIRE_EQ(FlattenCandidateColor(flattened.text, flattened.surface), RGB(191, 128, 191));
}

TEST_CASE(candidate_skin_palette_canvas_base_is_transparent_with_readable_text)
{
    for (const bool light : {false, true})
    {
        const CandidateSkinPalette palette = ResolveCandidateSkinPalette("canvas", light, "auto");
        REQUIRE_EQ(palette.surface.a, 0.0f);
        REQUIRE_EQ(palette.border.a, 0.0f);
        REQUIRE_EQ(palette.text.a, 1.0f);
        // GDI users flatten the transparent surface onto its own RGB: a readable fluent-like card.
        REQUIRE_EQ(FlattenCandidateColor(palette.surface, palette.surface),
                   light ? RGB(255, 255, 255) : RGB(32, 32, 32));
        REQUIRE_EQ(FlattenCandidateColor(palette.text, palette.surface), light ? RGB(26, 26, 26) : RGB(242, 242, 242));
        REQUIRE_EQ(CandidateSkinBaseItemRadiusDip("canvas"), 4.0f);
    }
    // A package surface still wins over the canvas default.
    CandidateSkinCatalog::CandidateColors colors;
    colors.surface = "#10203080";
    const CandidateSkinPalette custom = ResolveCandidateSkinPalette("art", false, "auto", &colors, "canvas");
    REQUIRE(std::abs(custom.surface.a - 128.0f / 255.0f) < 0.001f);
}