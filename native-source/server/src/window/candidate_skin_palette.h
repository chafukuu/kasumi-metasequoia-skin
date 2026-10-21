#pragma once

#include "skin/candidate_skin_catalog.h"

#include <d2d1.h>
#include <string>
#include <windows.h>

struct CandidateSkinPalette
{
    D2D1_COLOR_F surface;
    D2D1_COLOR_F border;
    D2D1_COLOR_F text;
    // 候选文字与预编辑文字：皮肤的 candidate_text / preedit_text，不写时同 text；设置页的候选文字色三者一起覆盖。
    D2D1_COLOR_F candidateText{};
    D2D1_COLOR_F preeditText{};
};

// 基础皮肤出厂清单里的候选项高亮圆角（item_corner_radius_dip）；没写时为 4。
float CandidateSkinBaseItemRadiusDip(const std::string &baseSkinId);

D2D1_COLOR_F CandidateColorFromRgb(UINT rgb, float alpha = 1.0f);
D2D1_COLOR_F ParseCandidateCssColor(const std::string &text, D2D1_COLOR_F fallback);
// 底色、描边、文字色：基底外观（baseLook）上叠外部皮肤写了的颜色，最后是设置里的候选文字色。
CandidateSkinPalette ResolveCandidateSkinPalette(const CandidateSkinCatalog::Package &baseLook, bool light,
                                                 const std::string &configuredTextColor,
                                                 const CandidateSkinCatalog::CandidateColors *packageColors = nullptr);
// 同上，基底取 baseSkinId（为空时取 skinId）的出厂外观，不读数据目录。
CandidateSkinPalette ResolveCandidateSkinPalette(const std::string &skinId, bool light,
                                                 const std::string &configuredTextColor,
                                                 const CandidateSkinCatalog::CandidateColors *packageColors = nullptr,
                                                 const std::string &baseSkinId = {});
CandidateSkinPalette FlattenCandidateSkinPaletteForGdi(const CandidateSkinPalette &palette,
                                                       D2D1_COLOR_F fallbackSurface);
COLORREF FlattenCandidateColor(D2D1_COLOR_F color, D2D1_COLOR_F background);
