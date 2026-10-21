// Image: loads a bitmap through WIC and draws it with the configured stretch, opacity and interpolation.
#include "msimeui/Controls.h"

#include "msimeui/DeviceResources.h"

#include "ControlsInternal.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <wincodec.h>

namespace msimeui
{
using namespace controls_detail;

namespace
{
SizeF ReadImageSize(const std::wstring &filePath)
{
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    if (filePath.empty() ||
        FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(factory.GetAddressOf()))) ||
        FAILED(factory->CreateDecoderFromFilename(filePath.c_str(), nullptr, GENERIC_READ,
                                                  WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf())) ||
        FAILED(decoder->GetFrame(0, frame.GetAddressOf())))
    {
        return {};
    }

    // A GIF frame can be smaller than the animation; its logical screen is the image size.
    GUID container = {};
    ComPtr<IWICMetadataQueryReader> reader;
    if (SUCCEEDED(decoder->GetContainerFormat(&container)) && IsEqualGUID(container, GUID_ContainerFormatGif) &&
        SUCCEEDED(decoder->GetMetadataQueryReader(reader.GetAddressOf())))
    {
        PROPVARIANT width;
        PROPVARIANT height;
        PropVariantInit(&width);
        PropVariantInit(&height);
        SizeF size;
        if (SUCCEEDED(reader->GetMetadataByName(L"/logscrdesc/Width", &width)) && width.vt == VT_UI2 &&
            SUCCEEDED(reader->GetMetadataByName(L"/logscrdesc/Height", &height)) && height.vt == VT_UI2 &&
            width.uiVal > 0 && height.uiVal > 0)
        {
            size = {static_cast<float>(width.uiVal), static_cast<float>(height.uiVal)};
        }
        PropVariantClear(&width);
        PropVariantClear(&height);
        if (size.width > 0.0f)
        {
            return size;
        }
    }

    UINT width = 0;
    UINT height = 0;
    return SUCCEEDED(frame->GetSize(&width, &height)) ? SizeF{static_cast<float>(width), static_cast<float>(height)}
                                                      : SizeF{};
}

// At 1:1, fractional device origins blend neighbouring pixels. Returns the DIP offset that moves the origin onto the
// device pixel grid, or zero when the mapping is not 1:1 or the transform is not axis-aligned.
D2D1_POINT_2F NativeSizeSnapOffset(ID2D1RenderTarget *target, const RectF &destination, const D2D1_SIZE_F &sourcePixels)
{
    float dpiX = 0.0f;
    float dpiY = 0.0f;
    target->GetDpi(&dpiX, &dpiY);
    D2D1_MATRIX_3X2_F transform = {};
    target->GetTransform(&transform);
    if (dpiX <= 0.0f || dpiY <= 0.0f || transform._12 != 0.0f || transform._21 != 0.0f || transform._11 <= 0.0f ||
        transform._22 <= 0.0f)
    {
        return {};
    }

    const float dpiScaleX = dpiX / 96.0f;
    const float dpiScaleY = dpiY / 96.0f;
    const float pixelsPerDipX = transform._11 * dpiScaleX;
    const float pixelsPerDipY = transform._22 * dpiScaleY;
    // Tolerate only DIP conversion rounding
    if (std::fabs(destination.width * pixelsPerDipX - sourcePixels.width) >= 0.01f ||
        std::fabs(destination.height * pixelsPerDipY - sourcePixels.height) >= 0.01f)
    {
        return {};
    }

    const float deviceX = (destination.x * transform._11 + transform._31) * dpiScaleX;
    const float deviceY = (destination.y * transform._22 + transform._32) * dpiScaleY;
    return {(std::round(deviceX) - deviceX) / pixelsPerDipX, (std::round(deviceY) - deviceY) / pixelsPerDipY};
}
} // namespace

Image::Image(std::wstring filePath) : filePath_(std::move(filePath))
{
}

void Image::SetSource(std::wstring filePath)
{
    if (filePath_ == filePath)
    {
        return;
    }
    filePath_ = std::move(filePath);
    naturalSize_ = {};
    naturalSizeLoaded_ = false;
    InvalidateMeasure();
}

const std::wstring &Image::GetSource() const
{
    return filePath_;
}

void Image::SetStretch(ImageStretch stretch)
{
    if (stretch_ == stretch)
    {
        return;
    }
    stretch_ = stretch;
    InvalidateVisual();
}

void Image::SetOpacity(float opacity)
{
    const float clamped = std::clamp(opacity, 0.0f, 1.0f);
    if (opacity_ == clamped)
    {
        return;
    }
    opacity_ = clamped;
    InvalidateVisual();
}

void Image::SetInterpolationMode(D2D1_BITMAP_INTERPOLATION_MODE interpolationMode)
{
    if (interpolationMode_ == interpolationMode)
    {
        return;
    }
    interpolationMode_ = interpolationMode;
    InvalidateVisual();
}

void Image::LoadNaturalSize()
{
    if (!naturalSizeLoaded_)
    {
        naturalSize_ = ReadImageSize(filePath_);
        naturalSizeLoaded_ = true;
    }
}

SizeF Image::Measure(const SizeF &availableSize)
{
    LoadNaturalSize();
    float width = naturalSize_.width;
    float height = naturalSize_.height;
    if (HasExplicitHeight() && !HasExplicitWidth() && naturalSize_.height > 0.0f)
    {
        width = naturalSize_.width * (explicitHeight_ / naturalSize_.height);
    }
    else if (HasExplicitWidth() && !HasExplicitHeight() && naturalSize_.width > 0.0f)
    {
        height = naturalSize_.height * (explicitWidth_ / naturalSize_.width);
    }
    return {std::min(width, availableSize.width), std::min(height, availableSize.height)};
}

void Image::Arrange(const RectF &finalRect)
{
    bounds_ = finalRect;
}

void Image::Render(DeviceResources &deviceResources)
{
    D2D1_SIZE_F sourceSize = {};
    ID2D1Bitmap *bitmap = deviceResources.GetImageFrame(filePath_, &sourceSize);
    ID2D1RenderTarget *target = deviceResources.GetRenderTarget();
    if (!bitmap || !target || sourceSize.width <= 0.0f || sourceSize.height <= 0.0f || bounds_.width <= 0.0f ||
        bounds_.height <= 0.0f)
    {
        return;
    }

    RectF destination = bounds_;
    D2D1_RECT_F sourceRect = D2D1::RectF(0.0f, 0.0f, sourceSize.width, sourceSize.height);
    if (stretch_ == ImageStretch::None)
    {
        destination.width = std::min(sourceSize.width, bounds_.width);
        destination.height = std::min(sourceSize.height, bounds_.height);
        // Crop rather than squeeze when the bounds are smaller than the image
        sourceRect.right = destination.width;
        sourceRect.bottom = destination.height;
    }
    else if (stretch_ == ImageStretch::Uniform || stretch_ == ImageStretch::UniformToFill)
    {
        const float scaleX = bounds_.width / sourceSize.width;
        const float scaleY = bounds_.height / sourceSize.height;
        const float scale = stretch_ == ImageStretch::Uniform ? std::min(scaleX, scaleY) : std::max(scaleX, scaleY);
        destination.width = sourceSize.width * scale;
        destination.height = sourceSize.height * scale;
        destination.x = bounds_.x + (bounds_.width - destination.width) * 0.5f;
        destination.y = bounds_.y + (bounds_.height - destination.height) * 0.5f;
    }

    const D2D1_SIZE_U bitmapPixels = bitmap->GetPixelSize();
    const D2D1_SIZE_F sourcePixels = {(sourceRect.right - sourceRect.left) * bitmapPixels.width / sourceSize.width,
                                      (sourceRect.bottom - sourceRect.top) * bitmapPixels.height / sourceSize.height};
    const D2D1_POINT_2F snap = NativeSizeSnapOffset(target, destination, sourcePixels);
    destination.x += snap.x;
    destination.y += snap.y;

    const auto destinationRect = D2D1::RectF(destination.x, destination.y, destination.x + destination.width,
                                             destination.y + destination.height);
    if (stretch_ == ImageStretch::UniformToFill || stretch_ == ImageStretch::None)
    {
        // The clip moves with the snapped image so its edges are not shaved by half a pixel
        target->PushAxisAlignedClip(D2D1::RectF(bounds_.x + snap.x, bounds_.y + snap.y,
                                                bounds_.x + snap.x + bounds_.width,
                                                bounds_.y + snap.y + bounds_.height),
                                    D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    }
    target->DrawBitmap(bitmap, destinationRect, opacity_, interpolationMode_, &sourceRect);
    if (stretch_ == ImageStretch::UniformToFill || stretch_ == ImageStretch::None)
    {
        target->PopAxisAlignedClip();
    }
}
} // namespace msimeui
