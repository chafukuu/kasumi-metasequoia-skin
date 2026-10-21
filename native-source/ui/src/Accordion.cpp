// Accordion: collapsible sections, optionally allowing several to be expanded at once.
#include "msimeui/Controls.h"

#include "msimeui/DeviceResources.h"
#include "msimeui/Window.h"

#include "ControlsInternal.h"

#include <algorithm>
#include <utility>

namespace msimeui
{
using namespace controls_detail;

namespace
{
constexpr float kAccordionCornerRadius = 14.0f;
constexpr float kAccordionHeaderHorizontalPadding = 20.0f;
} // namespace

Accordion::Accordion(float headerHeight) : headerHeight_(headerHeight)
{
}

void Accordion::AddSection(std::wstring title, std::shared_ptr<Visual> content, bool expanded)
{
    AdoptChild(content);
    sections_.push_back({std::move(title), std::move(content), expanded});
    if (!allowMultipleExpanded_ && expanded)
    {
        CollapseOtherSections(sections_.size() - 1);
    }
    InvalidateMeasure();
}

void Accordion::ClearSections()
{
    for (auto &section : sections_)
    {
        ReleaseChild(section.content);
    }
    sections_.clear();
    headerRects_.clear();
    contentRects_.clear();
    pressed_ = false;
    pressedIndex_ = static_cast<size_t>(-1);
    InvalidateMeasure();
}

void Accordion::SetAllowMultipleExpanded(bool allowMultipleExpanded)
{
    allowMultipleExpanded_ = allowMultipleExpanded;
    if (!allowMultipleExpanded_)
    {
        size_t firstExpanded = static_cast<size_t>(-1);
        for (size_t index = 0; index < sections_.size(); ++index)
        {
            if (sections_[index].expanded)
            {
                firstExpanded = index;
                break;
            }
        }
        if (firstExpanded != static_cast<size_t>(-1))
        {
            CollapseOtherSections(firstExpanded);
        }
    }
    InvalidateMeasure();
}

SizeF Accordion::Measure(const SizeF &availableSize)
{
    float height = 0.0f;
    float width = availableSize.width;
    for (const auto &section : sections_)
    {
        height += headerHeight_;
        if (section.expanded && section.content)
        {
            const SizeF contentSize = section.content->MeasureInLayout(
                {availableSize.width, std::max(availableSize.height - headerHeight_, 0.0f)});
            height += contentSize.height + 8.0f;
            width = std::max(width, contentSize.width);
        }
        height += 8.0f;
    }

    return {std::min(width, availableSize.width), std::min(height, availableSize.height)};
}

void Accordion::Arrange(const RectF &finalRect)
{
    bounds_ = finalRect;
    headerRects_.assign(sections_.size(), {});
    contentRects_.assign(sections_.size(), {});

    float cursorY = finalRect.y;
    for (size_t index = 0; index < sections_.size(); ++index)
    {
        headerRects_[index] = {finalRect.x, cursorY, finalRect.width, headerHeight_};
        cursorY += headerHeight_;
        if (sections_[index].expanded && sections_[index].content)
        {
            const float contentHeight =
                sections_[index]
                    .content
                    ->MeasureInLayout({finalRect.width, std::max(finalRect.height - (cursorY - finalRect.y), 0.0f)})
                    .height;
            contentRects_[index] = {finalRect.x, cursorY + 8.0f, finalRect.width, contentHeight};
            sections_[index].content->ArrangeInLayout(contentRects_[index]);
            cursorY += 8.0f + contentHeight;
        }
        cursorY += 8.0f;
    }
}

void Accordion::Render(DeviceResources &deviceResources)
{
    ID2D1RenderTarget *target = deviceResources.GetRenderTarget();
    if (!target)
    {
        return;
    }

    ID2D1SolidColorBrush *chevronBrush = deviceResources.GetSolidColorBrush(D2D1::ColorF(0x64748B));
    if (!chevronBrush)
    {
        return;
    }

    for (size_t index = 0; index < sections_.size(); ++index)
    {
        const bool pressed = pressed_ && index == pressedIndex_;
        FillRoundedRect(deviceResources, headerRects_[index], kAccordionCornerRadius,
                        pressed ? D2D1::ColorF(0xE0F2FE) : D2D1::ColorF(0xF8FAFC), D2D1::ColorF(0xCBD5E1), 1.0f);
        RectF titleRect = headerRects_[index];
        titleRect.x += kAccordionHeaderHorizontalPadding;
        titleRect.width = std::max(0.0f, titleRect.width - kAccordionHeaderHorizontalPadding - 36.0f);
        DrawLabel(deviceResources, sections_[index].title, 15.0f, true, D2D1::ColorF(0x0F172A), titleRect,
                  DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        const float centerX = headerRects_[index].x + headerRects_[index].width - 22.0f;
        const float centerY = headerRects_[index].y + headerRects_[index].height * 0.5f;
        if (sections_[index].expanded)
        {
            target->DrawLine(D2D1::Point2F(centerX - 4.0f, centerY - 2.0f), D2D1::Point2F(centerX, centerY + 2.0f),
                             chevronBrush, 1.8f);
            target->DrawLine(D2D1::Point2F(centerX, centerY + 2.0f), D2D1::Point2F(centerX + 4.0f, centerY - 2.0f),
                             chevronBrush, 1.8f);
        }
        else
        {
            target->DrawLine(D2D1::Point2F(centerX - 2.0f, centerY - 4.0f), D2D1::Point2F(centerX + 2.0f, centerY),
                             chevronBrush, 1.8f);
            target->DrawLine(D2D1::Point2F(centerX + 2.0f, centerY), D2D1::Point2F(centerX - 2.0f, centerY + 4.0f),
                             chevronBrush, 1.8f);
        }

        if (sections_[index].expanded && sections_[index].content)
        {
            sections_[index].content->Render(deviceResources);
        }
    }
}

void Accordion::Attach(Window *window)
{
    Visual::Attach(window);
    for (auto &section : sections_)
    {
        if (section.content)
        {
            section.content->Attach(window);
        }
    }
}

Visual *Accordion::FindVisualAt(const PointF &point)
{
    if (!HitTest(point))
    {
        return nullptr;
    }

    for (size_t index = 0; index < sections_.size(); ++index)
    {
        if (PointInRect(headerRects_[index], point))
        {
            return this;
        }
        if (sections_[index].expanded && sections_[index].content)
        {
            if (Visual *hit = sections_[index].content->FindVisualAt(point))
            {
                return hit;
            }
        }
    }

    return this;
}

Visual *Accordion::FindFocusableAt(const PointF &point)
{
    if (!HitTest(point))
    {
        return nullptr;
    }

    for (size_t index = 0; index < sections_.size(); ++index)
    {
        if (sections_[index].expanded && sections_[index].content)
        {
            if (Visual *focusable = sections_[index].content->FindFocusableAt(point))
            {
                return focusable;
            }
        }
    }

    return nullptr;
}

Visual *Accordion::FindFirstFocusableDescendant()
{
    for (auto &section : sections_)
    {
        if (section.expanded && section.content)
        {
            if (Visual *focusable = section.content->FindFirstFocusableDescendant())
            {
                return focusable;
            }
        }
    }

    return nullptr;
}

bool Accordion::HitTest(const PointF &point) const
{
    return PointInRect(bounds_, point);
}

bool Accordion::OnMouseDown(const POINT &point, WPARAM keyState)
{
    (void)keyState;
    if (!window_)
    {
        return false;
    }

    const size_t hit = HitTestHeader(window_->ClientPixelsToDips(point));
    if (hit == static_cast<size_t>(-1))
    {
        return false;
    }

    pressed_ = true;
    pressedIndex_ = hit;
    InvalidateVisual();
    return true;
}

bool Accordion::OnMouseUp(const POINT &point, WPARAM keyState)
{
    (void)keyState;
    if (!window_ || !pressed_)
    {
        return false;
    }

    const size_t hit = HitTestHeader(window_->ClientPixelsToDips(point));
    const size_t pressedIndex = pressedIndex_;
    pressed_ = false;
    pressedIndex_ = static_cast<size_t>(-1);

    if (hit != static_cast<size_t>(-1) && hit == pressedIndex)
    {
        const bool willExpand = !sections_[hit].expanded;
        sections_[hit].expanded = willExpand;
        if (willExpand && !allowMultipleExpanded_)
        {
            CollapseOtherSections(hit);
        }
        InvalidateMeasure();
    }

    return true;
}

HCURSOR Accordion::GetCursor() const
{
    return LoadCursor(nullptr, IDC_HAND);
}

void Accordion::CollapseOtherSections(size_t keepExpandedIndex)
{
    for (size_t index = 0; index < sections_.size(); ++index)
    {
        if (index != keepExpandedIndex)
        {
            sections_[index].expanded = false;
        }
    }
}

size_t Accordion::HitTestHeader(const PointF &point) const
{
    for (size_t index = 0; index < headerRects_.size(); ++index)
    {
        if (PointInRect(headerRects_[index], point))
        {
            return index;
        }
    }

    return static_cast<size_t>(-1);
}
} // namespace msimeui
