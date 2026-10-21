#pragma once

#include <algorithm>
#include <cmath>
#include <optional>

namespace FanyImeUi
{
struct CandidateWorkAreaPx
{
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    bool operator==(const CandidateWorkAreaPx &other) const
    {
        return left == other.left && top == other.top && right == other.right && bottom == other.bottom;
    }
};

struct CandidateReservedLeftResult
{
    int cardLeftPx = 0;
    bool captured = false;
};

// Opt-in horizontal placement only. Reserve the opaque card's maximum width,
// not its transparent host, shadow or outer insets. Actual width and Y stay
// with the scene/presenter. A visible composition keeps its captured left even
// if its caret advances or its candidates shrink; Hide/skin changes reset it.
class CandidateReservedLeft
{
  public:
    void Reset()
    {
        hasAnchor_ = false;
    }

    std::optional<CandidateReservedLeftResult> Place(int caretX, const CandidateWorkAreaPx &workArea, double scale,
                                                     double maxCardWidthDip)
    {
        if (workArea.right <= workArea.left || workArea.bottom <= workArea.top || !std::isfinite(scale) ||
            scale <= 0.0 || !std::isfinite(maxCardWidthDip) || maxCardWidthDip <= 0.0)
        {
            Reset();
            return std::nullopt;
        }
        if (hasAnchor_ && workArea == workArea_ && scale == scale_ && maxCardWidthDip == maxCardWidthDip_)
            return CandidateReservedLeftResult{cardLeftPx_, false};

        // Calculate in doubles so negative monitors and very wide work areas
        // cannot overflow int subtraction. Limit an oversized reservation to
        // the available work area, retaining at least one physical pixel.
        const double workWidth = static_cast<double>(workArea.right) - workArea.left;
        const double edgePad = (std::min)(std::round(2.0 * scale), std::floor((workWidth - 1.0) * 0.5));
        const double availableWidth = (std::max)(1.0, workWidth - 2.0 * edgePad);
        const double reservedWidth = (std::min)(std::ceil(maxCardWidthDip * scale), availableWidth);
        const double left = workArea.left + edgePad;
        const double right = (std::max)(left, workArea.right - edgePad - reservedWidth);
        cardLeftPx_ = static_cast<int>(std::clamp(static_cast<double>(caretX), left, right));
        workArea_ = workArea;
        scale_ = scale;
        maxCardWidthDip_ = maxCardWidthDip;
        hasAnchor_ = true;
        return CandidateReservedLeftResult{cardLeftPx_, true};
    }

  private:
    bool hasAnchor_ = false;
    CandidateWorkAreaPx workArea_{};
    double scale_ = 0.0;
    double maxCardWidthDip_ = 0.0;
    int cardLeftPx_ = 0;
};
} // namespace FanyImeUi
