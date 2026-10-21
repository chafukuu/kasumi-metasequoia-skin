// Separator: a thin horizontal rule used between groups of controls.
#include "msimeui/Controls.h"

#include "msimeui/DeviceResources.h"

#include "ControlsInternal.h"

#include <algorithm>

namespace msimeui
{
using namespace controls_detail;

Separator::Separator(float height) : height_(height)
{
}

SizeF Separator::Measure(const SizeF &availableSize)
{
    return {availableSize.width, height_};
}

void Separator::Arrange(const RectF &finalRect)
{
    bounds_ = finalRect;
}

void Separator::Render(DeviceResources &deviceResources)
{
    ID2D1RenderTarget *target = deviceResources.GetRenderTarget();
    if (!target)
    {
        return;
    }

    ID2D1SolidColorBrush *brush = deviceResources.GetSolidColorBrush(D2D1::ColorF(0xE2E8F0));
    if (!brush)
    {
        return;
    }
    const float centerY = bounds_.y + bounds_.height * 0.5f;
    target->DrawLine(D2D1::Point2F(bounds_.x, centerY), D2D1::Point2F(bounds_.x + bounds_.width, centerY), brush,
                     std::max(bounds_.height, 1.0f));
}
} // namespace msimeui
