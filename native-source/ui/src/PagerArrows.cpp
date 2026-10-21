// PagerArrows: two small stroke-drawn chevron buttons ("previous" / "next") for paging a list.
#include "msimeui/Controls.h"

#include "msimeui/DeviceResources.h"
#include "msimeui/Window.h"

#include "ControlsInternal.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace msimeui
{
using namespace controls_detail;

namespace
{
// A solid triangle pointing at the leading ("◀") or trailing ("▶") side, centred in box. Each
// corner is cut roundness along both of its edges and joined by a curve through the corner, so
// the shape is one fill: a translucent (disabled) brush shows no darker overlap anywhere.
void RenderTriangle(ID2D1RenderTarget *target, const RectF &box, bool previous, float glyphSize, float roundness,
                    ID2D1SolidColorBrush *brush)
{
    ComPtr<ID2D1Factory> factory;
    target->GetFactory(factory.GetAddressOf());
    ComPtr<ID2D1PathGeometry> geometry;
    ComPtr<ID2D1GeometrySink> sink;
    if (!factory || FAILED(factory->CreatePathGeometry(geometry.GetAddressOf())) ||
        FAILED(geometry->Open(sink.GetAddressOf())))
    {
        return;
    }
    const float height = std::min(glyphSize, box.height);
    const float width = height * PagerArrows::Appearance::kTriangleAspect;
    const float cx = box.x + box.width * 0.5f;
    const float cy = box.y + box.height * 0.5f;
    const float tipX = previous ? cx - width * 0.5f : cx + width * 0.5f;
    const float baseX = previous ? cx + width * 0.5f : cx - width * 0.5f;
    const D2D1_POINT_2F corners[3] = {D2D1::Point2F(tipX, cy), D2D1::Point2F(baseX, cy - height * 0.5f),
                                      D2D1::Point2F(baseX, cy + height * 0.5f)};
    // Half the shortest edge at most, so neighbouring cuts never cross.
    const float radius = std::clamp(roundness, 0.0f, height * 0.5f);
    const auto toward = [radius](D2D1_POINT_2F from, D2D1_POINT_2F to) {
        const float dx = to.x - from.x;
        const float dy = to.y - from.y;
        const float length = std::sqrt(dx * dx + dy * dy);
        const float t = length > 0.0f ? radius / length : 0.0f;
        return D2D1::Point2F(from.x + dx * t, from.y + dy * t);
    };
    sink->BeginFigure(toward(corners[0], corners[1]), D2D1_FIGURE_BEGIN_FILLED);
    for (int i = 1; i <= 3; ++i)
    {
        const D2D1_POINT_2F corner = corners[i % 3];
        sink->AddLine(toward(corner, corners[i - 1]));
        sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(corner, toward(corner, corners[(i + 1) % 3])));
    }
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    if (SUCCEEDED(sink->Close()))
    {
        target->FillGeometry(geometry.Get(), brush);
    }
}
} // namespace

void PagerArrows::SetAppearance(Appearance appearance)
{
    appearance_ = std::move(appearance);
    InvalidateMeasure();
    InvalidateVisual();
}

void PagerArrows::SetEnabled(bool previous, bool next)
{
    if (previousEnabled_ == previous && nextEnabled_ == next)
    {
        return;
    }
    previousEnabled_ = previous;
    nextEnabled_ = next;
    if (!IsPartEnabled(pressed_))
    {
        pressed_ = Part::None;
    }
    InvalidateVisual();
}

bool PagerArrows::IsPartEnabled(Part part) const
{
    return (part == Part::Previous && previousEnabled_) || (part == Part::Next && nextEnabled_);
}

void PagerArrows::SetOnClick(ClickHandler handler)
{
    onClick_ = std::move(handler);
}

void PagerArrows::SetHoverEnabled(bool enabled)
{
    if (hoverEnabled_ == enabled)
    {
        return;
    }
    hoverEnabled_ = enabled;
    if (!enabled && hovered_ != Part::None)
    {
        hovered_ = Part::None;
        InvalidateVisual();
    }
}

RectF PagerArrows::GetPartBounds(Part part) const
{
    // The buttons sit at the trailing bottom of the arranged box, so an arranged
    // box taller or wider than the two buttons keeps them in that corner.
    const float width = std::min(appearance_.buttonWidth, bounds_.width);
    const float height = std::min(appearance_.buttonHeight, bounds_.height);
    const float y = bounds_.y + bounds_.height - height;
    const float nextX = bounds_.x + bounds_.width - width;
    if (part == Part::Next)
    {
        return {nextX, y, width, height};
    }
    if (part == Part::Previous)
    {
        return {std::max(bounds_.x, nextX - appearance_.gap - width), y, width, height};
    }
    return {};
}

PagerArrows::Part PagerArrows::HitTestPart(const PointF &point) const
{
    if (PointInRect(GetPartBounds(Part::Next), point))
    {
        return Part::Next;
    }
    if (PointInRect(GetPartBounds(Part::Previous), point))
    {
        return Part::Previous;
    }
    return Part::None;
}

SizeF PagerArrows::Measure(const SizeF &availableSize)
{
    (void)availableSize;
    const float divider = appearance_.dividerWidth > 0.0f ? appearance_.dividerWidth + appearance_.dividerGap : 0.0f;
    return {divider + appearance_.buttonWidth * 2.0f + appearance_.gap, appearance_.buttonHeight};
}

void PagerArrows::Arrange(const RectF &finalRect)
{
    bounds_ = finalRect;
}

void PagerArrows::Render(DeviceResources &deviceResources)
{
    ID2D1RenderTarget *target = deviceResources.GetRenderTarget();
    if (!target || bounds_.width <= 0.0f || bounds_.height <= 0.0f)
    {
        return;
    }

    if (appearance_.dividerWidth > 0.0f && appearance_.dividerColor.a > 0.001f)
    {
        // Measured against the previous button rather than bounds_, so a wider arranged box keeps them together.
        const RectF previous = GetPartBounds(Part::Previous);
        const RectF divider = {previous.x - appearance_.dividerGap - appearance_.dividerWidth, previous.y,
                               appearance_.dividerWidth, previous.height};
        if (divider.x >= bounds_.x - 0.5f)
        {
            FillRoundedRect(deviceResources, divider, 0.0f, appearance_.dividerColor, D2D1::ColorF(0, 0.0f), 0.0f);
        }
    }

    for (const Part part : {Part::Previous, Part::Next})
    {
        const RectF box = GetPartBounds(part);
        const bool enabled = IsPartEnabled(part);
        if (enabled && (pressed_ == part || hovered_ == part))
        {
            const D2D1_COLOR_F fill = pressed_ == part ? appearance_.pressedFill : appearance_.hoverFill;
            FillRoundedRect(deviceResources, box, appearance_.cornerRadius, fill, D2D1::ColorF(0, 0.0f), 0.0f);
        }

        ID2D1SolidColorBrush *brush =
            deviceResources.GetSolidColorBrush(enabled ? appearance_.glyphColor : appearance_.disabledGlyphColor);
        if (!brush)
        {
            continue;
        }
        if (appearance_.glyph == Glyph::Triangle)
        {
            RenderTriangle(target, box, part == Part::Previous, appearance_.glyphSize, appearance_.strokeWidth, brush);
            continue;
        }
        // A chevron twice as tall as it is wide: "<" for previous, ">" for next.
        const float half = std::min(appearance_.glyphSize, box.height) * 0.5f;
        const float depth = std::min(appearance_.GlyphWidth(), half * 0.55f);
        const float cx = box.x + box.width * 0.5f;
        const float cy = box.y + box.height * 0.5f;
        const float tipX = part == Part::Previous ? cx - depth * 0.5f : cx + depth * 0.5f;
        const float tailX = part == Part::Previous ? cx + depth * 0.5f : cx - depth * 0.5f;
        target->DrawLine(D2D1::Point2F(tailX, cy - half), D2D1::Point2F(tipX, cy), brush, appearance_.strokeWidth);
        target->DrawLine(D2D1::Point2F(tipX, cy), D2D1::Point2F(tailX, cy + half), brush, appearance_.strokeWidth);
    }
}

bool PagerArrows::HitTest(const PointF &point) const
{
    return HitTestPart(point) != Part::None;
}

bool PagerArrows::OnMouseDown(const POINT &point, WPARAM keyState)
{
    (void)keyState;
    if (!window_)
    {
        return false;
    }
    const Part part = HitTestPart(window_->ClientPixelsToDips(point));
    pressed_ = IsPartEnabled(part) ? part : Part::None;
    InvalidateVisual();
    // Swallow clicks on a disabled button too, so they do not fall through to what lies below.
    return part != Part::None;
}

bool PagerArrows::OnMouseUp(const POINT &point, WPARAM keyState)
{
    (void)keyState;
    if (!window_ || pressed_ == Part::None)
    {
        return false;
    }
    const Part pressed = pressed_;
    pressed_ = Part::None;
    InvalidateVisual();
    if (HitTestPart(window_->ClientPixelsToDips(point)) == pressed && IsPartEnabled(pressed) && onClick_)
    {
        // The handler may rebuild the scene that owns this control; keep it alive for the call.
        ClickHandler handler = onClick_;
        handler(pressed);
    }
    return true;
}

bool PagerArrows::OnMouseMove(const POINT &point, WPARAM keyState)
{
    (void)keyState;
    if (!window_ || !hoverEnabled_)
    {
        return false;
    }
    const Part part = HitTestPart(window_->ClientPixelsToDips(point));
    if (hovered_ != part)
    {
        hovered_ = part;
        InvalidateVisual();
    }
    return part != Part::None;
}

void PagerArrows::OnMouseLeave()
{
    // A press survives leaving: OnMouseUp only clicks when the release lands on the same button.
    if (hovered_ == Part::None)
    {
        return;
    }
    hovered_ = Part::None;
    InvalidateVisual();
}

HCURSOR PagerArrows::GetCursor() const
{
    return LoadCursor(nullptr, IDC_ARROW);
}
} // namespace msimeui
