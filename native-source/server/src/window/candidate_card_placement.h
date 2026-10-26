#pragma once

#include "window/candidate_horizontal_placement.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace FanyImeUi
{
struct CandidateCardPositionPx
{
    int left = 0;
    int top = 0;
    bool above = false;
};

// Only the opaque candidate card participates in edge fitting. Decoration,
// shadow padding and allocation buckets must not push text away from its caret.
inline std::optional<CandidateCardPositionPx> PlaceCandidateCard(int caretX, int caretBottom,
                                                                 const CandidateWorkAreaPx &work, double scale,
                                                                 double widthDip, double heightDip,
                                                                 double decisionHeightDip)
{
    if (work.right <= work.left || work.bottom <= work.top || !std::isfinite(scale) || scale <= 0.0 ||
        !std::isfinite(widthDip) || widthDip <= 0.0 || !std::isfinite(heightDip) || heightDip <= 0.0 ||
        !std::isfinite(decisionHeightDip) || decisionHeightDip <= 0.0)
        return std::nullopt;

    // Double intermediates also cover negative monitor origins and carets
    // reported just outside the work area without overflowing pixel arithmetic.
    const double workWidth = static_cast<double>(work.right) - work.left;
    const double workHeight = static_cast<double>(work.bottom) - work.top;
    const double padX = (std::min)(std::round(2.0 * scale), std::floor((workWidth - 1.0) * 0.5));
    const double padY = (std::min)(std::round(2.0 * scale), std::floor((workHeight - 1.0) * 0.5));
    const double left = work.left + padX;
    const double top = work.top + padY;
    const double right = work.right - padX;
    const double bottom = work.bottom - padY;
    const double width = std::ceil(widthDip * scale);
    const double height = std::ceil(heightDip * scale);
    const double decisionHeight = std::ceil((std::max)(heightDip, decisionHeightDip) * scale);
    const double below = static_cast<double>(caretBottom) + std::round(3.0 * scale);
    // TSF's current point is the bottom of the input line. Keep the existing
    // 24 DIP line estimate above it until the wire carries an actual line rect.
    const double aboveBottom = static_cast<double>(caretBottom) - std::round(24.0 * scale);
    const bool fitsBelow = below >= top && below + height <= bottom;
    const bool fitsAbove = aboveBottom <= bottom && aboveBottom - height >= top;
    bool above = below + decisionHeight > bottom;
    // A previous tall vertical list can prefer above, although this shorter
    // list fits below and there is no room above. Never place it on the line
    // merely to preserve that historical flip decision.
    if (above && !fitsAbove && fitsBelow)
        above = false;
    else if (!fitsBelow && fitsAbove)
        above = true;
    else if (!fitsBelow && !fitsAbove)
        above = aboveBottom - top > bottom - below;
    const double x = std::clamp(static_cast<double>(caretX), left, (std::max)(left, right - width));
    const double y = std::clamp(above ? aboveBottom - height : below, top, (std::max)(top, bottom - height));
    return CandidateCardPositionPx{static_cast<int>(x), static_cast<int>(y), above};
}
} // namespace FanyImeUi
