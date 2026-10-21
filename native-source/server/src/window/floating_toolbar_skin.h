#pragma once

#include <d2d1.h>
#include <string>

// Colors and corner radii of the D2D floating toolbar, one set per built-in
// skin and theme. Radii are in DIPs before the user scale.
struct FloatingToolbarSkin
{
    D2D1_COLOR_F fill;
    D2D1_COLOR_F border;
    D2D1_COLOR_F glyph;
    D2D1_COLOR_F hover;
    D2D1_COLOR_F divider;
    D2D1_COLOR_F handle;
    float radius;
    float iconRadius;
};

namespace CandidateSkinCatalog
{
struct Package;
}

// The toolbar of a base skin's look (CandidateSkinCatalog::ResolveBuiltInLook): its [toolbar]
// colors and radii. External skins pass their base skin's look and then layer their own
// [toolbar] overrides with ApplyFloatingToolbarSkinOverrides.
FloatingToolbarSkin ResolveFloatingToolbarSkin(const CandidateSkinCatalog::Package &baseLook, bool light);

// Same, from the factory look of skinId (no data directory involved).
FloatingToolbarSkin ResolveFloatingToolbarSkin(const std::string &skinId, bool light);

// Applies an external skin's [toolbar] colors and corner radii for the given
// theme. Unset or unparsable values keep what the base skin resolved.
void ApplyFloatingToolbarSkinOverrides(FloatingToolbarSkin &skin, const CandidateSkinCatalog::Package &package,
                                       bool light);
