#include "tests/includes/test_framework.h"

#include "msimeui/DeviceResources.h"
#include "msimeui/Layout.h"

#include <d2d1effects.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
class HiddenWindow
{
  public:
    HiddenWindow()
    {
        REQUIRE(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)));
        hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, L"STATIC", L"Card shadow test", WS_POPUP, 0, 0, 64, 64,
                               nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        REQUIRE(hwnd != nullptr);
    }

    ~HiddenWindow()
    {
        DestroyWindow(hwnd);
        CoUninitialize();
    }

    HWND hwnd = nullptr;
};

const std::vector<msimeui::ShadowPass> kPasses = {
    {12.0f, 0.34f, 8.0f, 10.0f},
    {4.0f, 0.22f, 2.0f, 3.0f},
};

// The blur exactly as the card drew it before any caching: every pass straight onto the target.
void DrawReferenceShadow(ID2D1DeviceContext *dc, const msimeui::RectF &bounds, float radius)
{
    for (const auto &pass : kPasses)
    {
        const float pad = pass.sigma * 3.0f + 4.0f;
        ComPtr<ID2D1BitmapRenderTarget> compatible;
        REQUIRE(SUCCEEDED(dc->CreateCompatibleRenderTarget({bounds.width + pad * 2.0f, bounds.height + pad * 2.0f},
                                                           compatible.GetAddressOf())));
        compatible->BeginDraw();
        compatible->Clear(D2D1::ColorF(0, 0.0f));
        ComPtr<ID2D1SolidColorBrush> fill;
        REQUIRE(SUCCEEDED(compatible->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, pass.alpha), fill.GetAddressOf())));
        compatible->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(pad, pad, pad + bounds.width, pad + bounds.height), radius, radius),
            fill.Get());
        REQUIRE(SUCCEEDED(compatible->EndDraw()));
        ComPtr<ID2D1Bitmap> bitmap;
        REQUIRE(SUCCEEDED(compatible->GetBitmap(bitmap.GetAddressOf())));
        ComPtr<ID2D1Effect> blur;
        REQUIRE(SUCCEEDED(dc->CreateEffect(CLSID_D2D1GaussianBlur, blur.GetAddressOf())));
        blur->SetInput(0, bitmap.Get());
        blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, pass.sigma);
        blur->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_SOFT);
        blur->SetValue(D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION, D2D1_GAUSSIANBLUR_OPTIMIZATION_QUALITY);
        dc->DrawImage(blur.Get(), D2D1::Point2F(bounds.x - pad + pass.offsetX, bounds.y - pad + pass.offsetY));
    }
}

class Canvas
{
  public:
    Canvas(ID2D1DeviceContext *dc, UINT width, UINT height, float dpi) : dc_(dc), width_(width), height_(height)
    {
        const auto format = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
        REQUIRE(SUCCEEDED(dc->CreateBitmap(D2D1::SizeU(width, height), nullptr, 0,
                                           D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, format, dpi, dpi),
                                           target_.GetAddressOf())));
        REQUIRE(SUCCEEDED(dc->CreateBitmap(
            D2D1::SizeU(width, height), nullptr, 0,
            D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, format, dpi, dpi),
            readback_.GetAddressOf())));
    }

    template <typename Draw> std::vector<uint8_t> Render(Draw &&draw)
    {
        ComPtr<ID2D1Image> previous;
        dc_->GetTarget(previous.GetAddressOf());
        dc_->SetTarget(target_.Get());
        dc_->BeginDraw();
        dc_->Clear(D2D1::ColorF(0, 0.0f));
        draw();
        REQUIRE(SUCCEEDED(dc_->EndDraw()));
        dc_->SetTarget(previous.Get());

        REQUIRE(SUCCEEDED(readback_->CopyFromBitmap(nullptr, target_.Get(), nullptr)));
        D2D1_MAPPED_RECT mapped{};
        REQUIRE(SUCCEEDED(readback_->Map(D2D1_MAP_OPTIONS_READ, &mapped)));
        std::vector<uint8_t> pixels(static_cast<size_t>(width_) * height_ * 4);
        for (UINT row = 0; row < height_; ++row)
        {
            std::copy_n(mapped.bits + static_cast<size_t>(row) * mapped.pitch, width_ * 4,
                        pixels.begin() + static_cast<size_t>(row) * width_ * 4);
        }
        readback_->Unmap();
        return pixels;
    }

  private:
    ID2D1DeviceContext *dc_ = nullptr;
    UINT width_ = 0;
    UINT height_ = 0;
    ComPtr<ID2D1Bitmap1> target_;
    ComPtr<ID2D1Bitmap1> readback_;
};
} // namespace

// The card reuses one blurred bitmap for every size and stretches its straight
// middle; whatever size the card takes, the shadow must still look like a blur
// of that exact size.
TEST_CASE(card_shadow_matches_a_full_blur_at_every_size)
{
    HiddenWindow window;
    msimeui::DeviceResources resources;
    REQUIRE(resources.EnsureForComposition(window.hwnd));
    ID2D1DeviceContext *dc = resources.GetDeviceContext();
    REQUIRE(dc != nullptr);

    const msimeui::RectF sizes[] = {
        {40.3f, 30.6f, 300.0f, 90.0f},  {40.0f, 30.0f, 161.5f, 240.25f}, {41.7f, 29.2f, 520.0f, 60.0f},
        {40.5f, 30.5f, 200.0f, 200.0f}, {40.0f, 30.0f, 80.0f, 40.0f},    {40.25f, 30.75f, 233.3f, 131.7f},
    };
    // The cache this replaced already landed within 6/255 of the per-frame blur
    // (its bitmap is rounded to whole pixels); a seam between slices would be
    // off by the shadow's full alpha, far above this.
    constexpr int kTolerance = 8;
    int worst = 0;
    for (const float dpi : {96.0f, 120.0f, 144.0f, 192.0f})
    {
        for (const float radius : {6.0f, 10.0f})
        {
            msimeui::Brush brush;
            brush.fill = D2D1::ColorF(0, 0.0f);
            brush.stroke = D2D1::ColorF(0, 0.0f);
            brush.strokeWidth = 1.0f;
            brush.radiusX = radius;
            brush.radiusY = radius;
            // One card through every size, so later sizes reuse the bitmap an earlier one built.
            msimeui::Card card(brush, 0.0f);
            card.SetShadowPasses(kPasses);
            for (const auto &bounds : sizes)
            {
                const UINT width = static_cast<UINT>(std::ceil((bounds.x + bounds.width + 80.0f) * dpi / 96.0f));
                const UINT height = static_cast<UINT>(std::ceil((bounds.y + bounds.height + 80.0f) * dpi / 96.0f));
                Canvas canvas(dc, width, height, dpi);
                card.Arrange(bounds);
                const auto actual = canvas.Render([&]() { card.Render(resources); });
                const auto expected = canvas.Render([&]() { DrawReferenceShadow(dc, bounds, radius); });
                int maxDiff = 0;
                for (size_t i = 0; i < actual.size(); ++i)
                {
                    maxDiff =
                        (std::max)(maxDiff, std::abs(static_cast<int>(actual[i]) - static_cast<int>(expected[i])));
                }
                worst = (std::max)(worst, maxDiff);
                if (maxDiff > kTolerance)
                {
                    std::printf("shadow mismatch dpi=%.0f radius=%.0f size=%.2fx%.2f max=%d\n", dpi, radius,
                                bounds.width, bounds.height, maxDiff);
                }
                REQUIRE(maxDiff <= kTolerance);
            }
        }
    }
    std::printf("card shadow worst channel difference %d/255\n", worst);
}
