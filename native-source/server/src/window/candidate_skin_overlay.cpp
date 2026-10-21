#include "window/candidate_skin_overlay.h"

#include "msimeui/DeviceResources.h"

#include <algorithm>

void CandidateSkinOverlay::SetItems(std::vector<Item> items)
{
    items_ = std::move(items);
    InvalidateVisual();
}

msimeui::SizeF CandidateSkinOverlay::Measure(const msimeui::SizeF & /*availableSize*/)
{
    // 挂件是浮在卡片外面的装饰，不参与测量：测量为 0 时父级网格的格子仍由卡片决定，
    // 卡片不会被立绘撑大或被挤偏。
    return {0.0f, 0.0f};
}

void CandidateSkinOverlay::Arrange(const msimeui::RectF &finalRect)
{
    bounds_ = finalRect;
}

bool CandidateSkinOverlay::HitTest(const msimeui::PointF & /*point*/) const
{
    return false;
}

void CandidateSkinOverlay::Render(msimeui::DeviceResources &deviceResources)
{
    if (items_.empty() || bounds_.width <= 0.0f || bounds_.height <= 0.0f)
    {
        return;
    }
    ID2D1RenderTarget *target = deviceResources.GetRenderTarget();
    if (!target)
    {
        return;
    }
    for (const Item &item : items_)
    {
        // 动图按动画时钟取当前帧，并把下一次换帧时间报给宿主。
        D2D1_SIZE_F sourceSize = {};
        ID2D1Bitmap *bitmap = deviceResources.GetImageFrame(item.path, &sourceSize);
        if (!bitmap || sourceSize.width <= 0.0f || sourceSize.height <= 0.0f)
        {
            continue;
        }
        const float width = item.widthDip > 0.0f ? item.widthDip : sourceSize.width;
        const float height = item.heightDip > 0.0f ? item.heightDip : sourceSize.height;
        const float right = bounds_.x + bounds_.width;
        const float bottom = bounds_.y + bounds_.height;
        float x = right - width;
        float y = bounds_.y + (bounds_.height - height) * 0.5f;
        const std::string &anchor = item.anchor;
        if (anchor == "top-left")
        {
            x = bounds_.x;
            y = bounds_.y;
        }
        else if (anchor == "top-right")
        {
            x = right - width;
            y = bounds_.y;
        }
        else if (anchor == "bottom-left")
        {
            x = bounds_.x;
            y = bottom - height;
        }
        else if (anchor == "bottom-right")
        {
            x = right - width;
            y = bottom - height;
        }
        else if (anchor == "left")
        {
            x = bounds_.x;
        }
        else if (anchor == "top")
        {
            x = bounds_.x + (bounds_.width - width) * 0.5f;
            y = bounds_.y;
        }
        else if (anchor == "bottom")
        {
            x = bounds_.x + (bounds_.width - width) * 0.5f;
            y = bottom - height;
        }
        else if (anchor == "center")
        {
            x = bounds_.x + (bounds_.width - width) * 0.5f;
            y = bounds_.y + (bounds_.height - height) * 0.5f;
        }
        x += item.offsetXDip;
        y += item.offsetYDip;
        // 图片在 width × height 的框里等比缩放、居中（CSS background-size: contain），比例不同也不变形。
        const float scale = (std::min)(width / sourceSize.width, height / sourceSize.height);
        const float drawWidth = sourceSize.width * scale;
        const float drawHeight = sourceSize.height * scale;
        const float left = x + (width - drawWidth) * 0.5f;
        const float top = y + (height - drawHeight) * 0.5f;
        const D2D1_RECT_F destination = D2D1::RectF(left, top, left + drawWidth, top + drawHeight);
        const D2D1_RECT_F source = D2D1::RectF(0.0f, 0.0f, sourceSize.width, sourceSize.height);
        target->DrawBitmap(bitmap, destination, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &source);
    }
}
