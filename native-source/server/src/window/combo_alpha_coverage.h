#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace ComboAlphaCoverage
{
struct Footprint
{
    double leftPx = 0.0;
    double topPx = 0.0;
    double rightPx = 0.0;
    double bottomPx = 0.0;
    double sourcePerPixelX = 0.0;
    double sourcePerPixelY = 0.0;
    unsigned sourceWidth = 0;
    unsigned sourceHeight = 0;
    bool valid = false;
};

class Mask
{
  public:
    void Reset(unsigned width, unsigned height, const std::vector<std::uint8_t> &alpha)
    {
        width_ = 0;
        height_ = 0;
        stride_ = 0;
        prefix_.clear();
        if (!width || !height || static_cast<std::size_t>(width) > (std::numeric_limits<std::size_t>::max)() / height)
            return;
        const auto pixels = static_cast<std::size_t>(width) * height;
        if (alpha.size() != pixels || pixels > (std::numeric_limits<std::uint32_t>::max)())
            return;
        const auto columns = static_cast<std::size_t>(width) + 1;
        const auto rows = static_cast<std::size_t>(height) + 1;
        if (!columns || !rows || columns > (std::numeric_limits<std::size_t>::max)() / rows)
            return;
        prefix_.assign(columns * rows, 0);
        width_ = width;
        height_ = height;
        stride_ = columns;
        // The supplied alpha already includes LoadAlpha's source-space guard.
        // Record occupancy only: no additional fixed source dilation is added.
        for (unsigned y = 0; y < height; ++y)
        {
            std::uint32_t row = 0;
            for (unsigned x = 0; x < width; ++x)
            {
                row += alpha[static_cast<std::size_t>(y) * width + x] != 0;
                prefix_[(static_cast<std::size_t>(y) + 1) * stride_ + x + 1] =
                    prefix_[static_cast<std::size_t>(y) * stride_ + x + 1] + row;
            }
        }
    }

    Footprint Place(float leftDip, float topDip, float rightDip, float bottomDip, float dpiScale) const
    {
        Footprint placed;
        if (prefix_.empty() || !std::isfinite(leftDip) || !std::isfinite(topDip) || !std::isfinite(rightDip) ||
            !std::isfinite(bottomDip) || !std::isfinite(dpiScale) || dpiScale <= 0.0f || rightDip <= leftDip ||
            bottomDip <= topDip)
            return placed;
        placed.leftPx = static_cast<double>(leftDip) * dpiScale;
        placed.topPx = static_cast<double>(topDip) * dpiScale;
        placed.rightPx = static_cast<double>(rightDip) * dpiScale;
        placed.bottomPx = static_cast<double>(bottomDip) * dpiScale;
        placed.sourcePerPixelX = width_ / (placed.rightPx - placed.leftPx);
        placed.sourcePerPixelY = height_ / (placed.bottomPx - placed.topPx);
        placed.sourceWidth = width_;
        placed.sourceHeight = height_;
        placed.valid = std::isfinite(placed.sourcePerPixelX) && std::isfinite(placed.sourcePerPixelY) &&
                       placed.sourcePerPixelX > 0.0 && placed.sourcePerPixelY > 0.0;
        return placed;
    }

    bool ContainsPixel(const Footprint &placed, int x, int y) const
    {
        if (!placed.valid || prefix_.empty() || placed.sourceWidth != width_ || placed.sourceHeight != height_)
            return false;
        const double centreX = static_cast<double>(x) + 0.5;
        const double centreY = static_cast<double>(y) + 0.5;
        constexpr double radiusPx = 1.0;
        // Do not reject u < 0 at the pixel centre: the D2D geometry can cover a
        // pixel whose centre is up to half a pixel outside the destination.
        if (centreX < placed.leftPx - radiusPx || centreX >= placed.rightPx + radiusPx ||
            centreY < placed.topPx - radiusPx || centreY >= placed.bottomPx + radiusPx)
            return false;
        const auto low = [](double coordinate, unsigned limit) {
            return static_cast<unsigned>(std::clamp(std::floor(coordinate), 0.0, static_cast<double>(limit)));
        };
        const auto high = [](double coordinate, unsigned limit) {
            return static_cast<unsigned>(std::clamp(std::floor(coordinate) + 1.0, 0.0, static_cast<double>(limit)));
        };
        const unsigned left = low((centreX - radiusPx - placed.leftPx) * placed.sourcePerPixelX, width_);
        const unsigned right = high((centreX + radiusPx - placed.leftPx) * placed.sourcePerPixelX, width_);
        const unsigned top = low((centreY - radiusPx - placed.topPx) * placed.sourcePerPixelY, height_);
        const unsigned bottom = high((centreY + radiusPx - placed.topPx) * placed.sourcePerPixelY, height_);
        if (left >= right || top >= bottom)
            return false;
        const std::uint64_t positive =
            static_cast<std::uint64_t>(prefix_[static_cast<std::size_t>(bottom) * stride_ + right]) +
            prefix_[static_cast<std::size_t>(top) * stride_ + left];
        const std::uint64_t negative =
            static_cast<std::uint64_t>(prefix_[static_cast<std::size_t>(top) * stride_ + right]) +
            prefix_[static_cast<std::size_t>(bottom) * stride_ + left];
        return positive > negative;
    }

  private:
    unsigned width_ = 0;
    unsigned height_ = 0;
    std::size_t stride_ = 0;
    std::vector<std::uint32_t> prefix_;
};
} // namespace ComboAlphaCoverage
