#include "window/floating_toolbar_skin.h"

#include "skin/candidate_skin_catalog.h"
#include "window/candidate_skin_palette.h"

namespace
{
void ApplyToolbarLayer(FloatingToolbarSkin &skin, const CandidateSkinCatalog::Package &layer, bool light)
{
    const CandidateSkinCatalog::ToolbarColors &colors = light ? layer.toolbarLight : layer.toolbarDark;
    skin.fill = ParseCandidateCssColor(colors.background, skin.fill);
    skin.border = ParseCandidateCssColor(colors.border, skin.border);
    skin.handle = ParseCandidateCssColor(colors.handle, skin.handle);
    skin.divider = ParseCandidateCssColor(colors.divider, skin.divider);
    skin.glyph = ParseCandidateCssColor(colors.icon, skin.glyph);
    skin.hover = ParseCandidateCssColor(colors.hover, skin.hover);
    if (layer.toolbarCornerRadiusDip)
    {
        skin.radius = static_cast<float>(*layer.toolbarCornerRadiusDip);
    }
    if (layer.toolbarIconCornerRadiusDip)
    {
        skin.iconRadius = static_cast<float>(*layer.toolbarIconCornerRadiusDip);
    }
}
} // namespace

FloatingToolbarSkin ResolveFloatingToolbarSkin(const CandidateSkinCatalog::Package &baseLook, bool light)
{
    // 配色与圆角都来自基底清单的 [toolbar]；清单没写的项（不该发生）退回不透明的中性色。
    FloatingToolbarSkin skin{CandidateColorFromRgb(light ? 0xFFFFFF : 0x1A1A1A),
                             light ? D2D1::ColorF(0, 0.12f) : D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.15f),
                             CandidateColorFromRgb(light ? 0x1A1A1A : 0xFFFFFF),
                             light ? D2D1::ColorF(0, 0.08f) : D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.10f),
                             light ? D2D1::ColorF(0, 0.12f) : D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.15f),
                             CandidateColorFromRgb(0x6B69D6),
                             8.0f,
                             6.0f};
    ApplyToolbarLayer(skin, baseLook, light);
    return skin;
}

FloatingToolbarSkin ResolveFloatingToolbarSkin(const std::string &skinId, bool light)
{
    return ResolveFloatingToolbarSkin(CandidateSkinCatalog::FactoryLook(skinId), light);
}

void ApplyFloatingToolbarSkinOverrides(FloatingToolbarSkin &skin, const CandidateSkinCatalog::Package &package,
                                       bool light)
{
    ApplyToolbarLayer(skin, package, light);
}
