#pragma once

#include "window/candidate_item_geometry.h"

#include <algorithm>
#include <cmath>
#include <vector>

struct CandidateItemSeparatorLineDip
{
    float x = 0.0f;
    float top = 0.0f;
    float bottom = 0.0f;
    float width = 0.0f;
};

// Use the same arranged item rectangles as the text. Only internal boundaries
// of the first horizontal row are decorated; nothing is inferred from a count.
inline std::vector<CandidateItemSeparatorLineDip> CandidateItemSeparatorLinesFromBounds(
    const CandidateItemRectDip &card, const std::vector<CandidateItemRectDip> &items, float topDip, float bottomDip,
    float lineWidthDip, float lineGapDip)
{
    if (!std::isfinite(card.x) || !std::isfinite(card.y) || !std::isfinite(card.width) || !std::isfinite(card.height) ||
        card.width <= 0.0f || card.height <= 0.0f || !std::isfinite(topDip) || !std::isfinite(bottomDip) ||
        !std::isfinite(lineWidthDip) || !std::isfinite(lineGapDip) || topDip < 0.0f || bottomDip <= topDip ||
        bottomDip > 4096.0f || lineWidthDip <= 0.0f || lineWidthDip > 4096.0f || lineGapDip < lineWidthDip ||
        lineGapDip > 4096.0f)
        return {};
    const float top = (std::min)(topDip, card.height);
    const float bottom = (std::min)(bottomDip, card.height);
    if (bottom <= top)
        return {};

    const CandidateMainRowGeometry row = CandidateMainRowGeometryFromBounds(card, items);
    std::vector<CandidateItemSeparatorLineDip> lines;
    const float halfGap = lineGapDip * 0.5f;
    const float outerHalfWidth = halfGap + lineWidthDip * 0.5f;
    for (const float edge : row.edgesDip)
    {
        // Reject the whole pair at an edge rather than drawing a clipped half.
        if (edge < outerHalfWidth || edge + outerHalfWidth > card.width)
            continue;
        lines.push_back({edge - halfGap, top, bottom, lineWidthDip});
        lines.push_back({edge + halfGap, top, bottom, lineWidthDip});
    }
    return lines;
}
