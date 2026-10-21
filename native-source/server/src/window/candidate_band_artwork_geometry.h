#pragma once

#include "window/candidate_item_geometry.h"

#include <algorithm>
#include <cmath>
#include <optional>

struct CandidateBandArtworkGeometryDip
{
    CandidateItemRectDip backgroundDestination;
    CandidateItemRectDip backgroundClip;
    CandidateItemRectDip foregroundDestination;
};

// The full-size art never stretches with the card. Only the background clip
// and the right-anchored foreground position depend on the current card width.
inline std::optional<CandidateBandArtworkGeometryDip> CandidateBandArtworkGeometryFromBounds(
    const CandidateItemRectDip &card, float widthDip, float heightDip, float offsetYDip, float foregroundRightClipDip)
{
    if (!std::isfinite(card.x) || !std::isfinite(card.y) || !std::isfinite(card.width) || !std::isfinite(card.height) ||
        card.width <= 0.0f || card.height <= 0.0f || !std::isfinite(widthDip) || !std::isfinite(heightDip) ||
        !std::isfinite(offsetYDip) || !std::isfinite(foregroundRightClipDip) || widthDip <= 0.0f ||
        widthDip > 4096.0f || heightDip <= 0.0f || heightDip > 4096.0f || std::abs(offsetYDip) > 4096.0f ||
        foregroundRightClipDip < 0.0f || foregroundRightClipDip > widthDip)
        return std::nullopt;
    const float y = card.y + offsetYDip;
    const float right = card.x + card.width;
    if (!std::isfinite(y) || !std::isfinite(right))
        return std::nullopt;
    return CandidateBandArtworkGeometryDip{
        {card.x, y, widthDip, heightDip},
        {card.x, y, (std::max)(card.width - foregroundRightClipDip, 0.0f), heightDip},
        {right - widthDip, y, widthDip, heightDip}};
}
