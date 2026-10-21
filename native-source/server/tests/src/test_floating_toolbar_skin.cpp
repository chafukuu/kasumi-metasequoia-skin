#include "tests/includes/test_framework.h"
#include "skin/candidate_skin_catalog.h"
#include "window/candidate_skin_palette.h"
#include "window/floating_toolbar_skin.h"

TEST_CASE(floating_toolbar_skin_defaults_to_fluent_toolbar)
{
    const FloatingToolbarSkin dark = ResolveFloatingToolbarSkin("fluent", false);
    REQUIRE_EQ(FlattenCandidateColor(dark.fill, dark.fill), RGB(26, 26, 26));
    REQUIRE_EQ(FlattenCandidateColor(dark.handle, dark.fill), RGB(107, 105, 214));
    REQUIRE_EQ(dark.radius, 8.0f);

    const FloatingToolbarSkin unknown = ResolveFloatingToolbarSkin("no_such_skin", true);
    REQUIRE_EQ(FlattenCandidateColor(unknown.fill, unknown.fill), RGB(255, 255, 255));
    REQUIRE_EQ(FlattenCandidateColor(unknown.glyph, unknown.fill), RGB(26, 26, 26));
}

TEST_CASE(floating_toolbar_skin_follows_builtin_skin_pages)
{
    const FloatingToolbarSkin wechat = ResolveFloatingToolbarSkin("wechat", false);
    REQUIRE_EQ(FlattenCandidateColor(wechat.fill, wechat.fill), RGB(21, 21, 21));
    REQUIRE_EQ(FlattenCandidateColor(wechat.handle, wechat.fill), RGB(7, 193, 96));

    const FloatingToolbarSkin graphite = ResolveFloatingToolbarSkin("graphite", true);
    REQUIRE_EQ(FlattenCandidateColor(graphite.fill, graphite.fill), RGB(251, 251, 252));
    REQUIRE_EQ(FlattenCandidateColor(graphite.glyph, graphite.fill), RGB(55, 65, 81));
    REQUIRE_EQ(graphite.radius, 4.0f);
    REQUIRE_EQ(graphite.iconRadius, 3.0f);

    const FloatingToolbarSkin willow = ResolveFloatingToolbarSkin("willow_green", false);
    REQUIRE_EQ(FlattenCandidateColor(willow.fill, willow.fill), RGB(45, 47, 46));
    REQUIRE_EQ(FlattenCandidateColor(willow.handle, willow.fill), RGB(101, 201, 141));
    REQUIRE_EQ(willow.radius, 9.0f);

    const FloatingToolbarSkin autumnDark = ResolveFloatingToolbarSkin("autumn_osmanthus", false);
    REQUIRE_EQ(FlattenCandidateColor(autumnDark.fill, autumnDark.fill), RGB(125, 146, 159));
    REQUIRE_EQ(FlattenCandidateColor(autumnDark.handle, autumnDark.fill), RGB(249, 125, 10));
    REQUIRE_EQ(autumnDark.radius, 10.0f);

    const FloatingToolbarSkin autumnLight = ResolveFloatingToolbarSkin("autumn_osmanthus", true);
    REQUIRE_EQ(FlattenCandidateColor(autumnLight.fill, autumnLight.fill), RGB(214, 236, 240));
    REQUIRE_EQ(FlattenCandidateColor(autumnLight.glyph, autumnLight.fill), RGB(26, 26, 26));

    // Microsoft keeps the default toolbar and only takes its purple selection bar colour for the handle.
    for (const bool light : {false, true})
    {
        const FloatingToolbarSkin microsoft = ResolveFloatingToolbarSkin("microsoft", light);
        const FloatingToolbarSkin fluent = ResolveFloatingToolbarSkin("fluent", light);
        REQUIRE_EQ(FlattenCandidateColor(microsoft.handle, microsoft.fill), RGB(225, 131, 217));
        REQUIRE_EQ(FlattenCandidateColor(microsoft.fill, microsoft.fill),
                   FlattenCandidateColor(fluent.fill, fluent.fill));
        REQUIRE_EQ(microsoft.radius, fluent.radius);
    }
}

// An external skin's [toolbar] tables reach the D2D toolbar.
TEST_CASE(floating_toolbar_skin_applies_external_overrides_for_the_theme)
{
    CandidateSkinCatalog::Package package;
    package.toolbarDark.handle = "#e08aa8";
    package.toolbarLight.background = "#fff7fa";
    package.toolbarLight.icon = "#3a2a30";
    package.toolbarLight.handle = "not a colour";
    package.toolbarCornerRadiusDip = 12.0;

    FloatingToolbarSkin dark = ResolveFloatingToolbarSkin("fluent", false);
    ApplyFloatingToolbarSkinOverrides(dark, package, false);
    REQUIRE_EQ(FlattenCandidateColor(dark.handle, dark.fill), RGB(224, 138, 168));
    // Keys the skin leaves out keep the base toolbar's values.
    REQUIRE_EQ(FlattenCandidateColor(dark.fill, dark.fill), RGB(26, 26, 26));
    REQUIRE_EQ(dark.radius, 12.0f);

    FloatingToolbarSkin light = ResolveFloatingToolbarSkin("fluent", true);
    ApplyFloatingToolbarSkinOverrides(light, package, true);
    REQUIRE_EQ(FlattenCandidateColor(light.fill, light.fill), RGB(255, 247, 250));
    REQUIRE_EQ(FlattenCandidateColor(light.glyph, light.fill), RGB(58, 42, 48));
    REQUIRE_EQ(FlattenCandidateColor(light.handle, light.fill), RGB(107, 105, 214));

    CandidateSkinCatalog::Package plain;
    FloatingToolbarSkin graphite = ResolveFloatingToolbarSkin("graphite", false);
    ApplyFloatingToolbarSkinOverrides(graphite, plain, false);
    REQUIRE_EQ(graphite.radius, 4.0f);
}
