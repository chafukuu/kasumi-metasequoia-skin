#include "tests/includes/test_framework.h"

#include "msimeui/Controls.h"
#include "msimeui/DeviceResources.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <wincodec.h>

using Microsoft::WRL::ComPtr;

namespace
{
constexpr UINT kImageWidth = 132;
constexpr UINT kImageHeight = 106;

// Members clean up even when a later REQUIRE in the fixture constructor throws.
class ComApartment
{
  public:
    ComApartment() : hr_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))
    {
        // WIC and D2D work in either apartment; only a real failure matters.
        REQUIRE(SUCCEEDED(hr_) || hr_ == RPC_E_CHANGED_MODE);
    }

    ~ComApartment()
    {
        if (SUCCEEDED(hr_))
        {
            CoUninitialize();
        }
    }

  private:
    HRESULT hr_;
};

class TestWindow
{
  public:
    TestWindow()
        : hwnd(CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, L"STATIC", L"Image pixel test", WS_POPUP, 0, 0, 320, 256,
                               nullptr, nullptr, GetModuleHandleW(nullptr), nullptr))
    {
        REQUIRE(hwnd != nullptr);
    }

    ~TestWindow()
    {
        DestroyWindow(hwnd);
    }

    HWND hwnd;
};

class TempFile
{
  public:
    TempFile()
    {
        wchar_t directory[MAX_PATH] = {};
        REQUIRE(GetTempPathW(MAX_PATH, directory) != 0);
        path = std::wstring(directory) + L"msimeui-image-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
               std::to_wstring(GetTickCount64()) + L".png";
    }

    ~TempFile()
    {
        DeleteFileW(path.c_str());
    }

    std::wstring path;
};

class ImageFixture
{
  public:
    ImageFixture()
    {
        ComPtr<IWICImagingFactory> factory;
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapEncoder> encoder;
        ComPtr<IWICBitmapFrameEncode> frame;
        REQUIRE(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                           IID_PPV_ARGS(factory.GetAddressOf()))));
        REQUIRE(SUCCEEDED(factory->CreateStream(stream.GetAddressOf())));
        REQUIRE(SUCCEEDED(stream->InitializeFromFilename(file.path.c_str(), GENERIC_WRITE)));
        REQUIRE(SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf())));
        REQUIRE(SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)));
        REQUIRE(SUCCEEDED(encoder->CreateNewFrame(frame.GetAddressOf(), nullptr)));
        REQUIRE(SUCCEEDED(frame->Initialize(nullptr)));
        REQUIRE(SUCCEEDED(frame->SetSize(kImageWidth, kImageHeight)));
        REQUIRE(SUCCEEDED(frame->SetResolution(96.0, 96.0)));
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        REQUIRE(SUCCEEDED(frame->SetPixelFormat(&format)));
        REQUIRE(IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA));
        // Opaque out to the edges so a clip that shaves the outermost pixels shows up
        std::vector<uint8_t> pixels(kImageWidth * kImageHeight * 4, 0);
        for (UINT y = 0; y < kImageHeight; ++y)
        {
            for (UINT x = 0; x < kImageWidth; ++x)
            {
                const size_t offset = (y * kImageWidth + x) * 4;
                const bool border = x < 2 || y < 2 || x >= kImageWidth - 2 || y >= kImageHeight - 2;
                pixels[offset] = border ? 0 : (x + y) % 2 ? 255 : 0;
                pixels[offset + 1] = border ? 0 : (x + y) % 2 ? 0 : 255;
                pixels[offset + 2] = border ? 255 : 0;
                pixels[offset + 3] = 255;
            }
        }
        REQUIRE(SUCCEEDED(
            frame->WritePixels(kImageHeight, kImageWidth * 4, static_cast<UINT>(pixels.size()), pixels.data())));
        REQUIRE(SUCCEEDED(frame->Commit()));
        REQUIRE(SUCCEEDED(encoder->Commit()));
    }

    ComApartment com;
    TestWindow window;
    TempFile file;
};

class Canvas
{
  public:
    Canvas(ID2D1DeviceContext *dc, float dpi) : dc_(dc)
    {
        const auto format = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
        REQUIRE(SUCCEEDED(dc->CreateBitmap(D2D1::SizeU(320, 256), nullptr, 0,
                                           D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, format, dpi, dpi),
                                           target_.GetAddressOf())));
        REQUIRE(SUCCEEDED(dc->CreateBitmap(
            D2D1::SizeU(320, 256), nullptr, 0,
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
        dc_->SetTransform(D2D1::Matrix3x2F::Identity());
        REQUIRE(SUCCEEDED(dc_->EndDraw()));
        dc_->SetTarget(previous.Get());
        REQUIRE(SUCCEEDED(readback_->CopyFromBitmap(nullptr, target_.Get(), nullptr)));
        D2D1_MAPPED_RECT mapped{};
        REQUIRE(SUCCEEDED(readback_->Map(D2D1_MAP_OPTIONS_READ, &mapped)));
        std::vector<uint8_t> pixels(320 * 256 * 4);
        for (UINT row = 0; row < 256; ++row)
        {
            std::copy_n(mapped.bits + static_cast<size_t>(row) * mapped.pitch, 320 * 4,
                        pixels.begin() + static_cast<size_t>(row) * 320 * 4);
        }
        readback_->Unmap();
        return pixels;
    }

  private:
    ID2D1DeviceContext *dc_;
    ComPtr<ID2D1Bitmap1> target_;
    ComPtr<ID2D1Bitmap1> readback_;
};

struct Scenario
{
    msimeui::ImageStretch stretch = msimeui::ImageStretch::Uniform;
    std::vector<float> dpis;
    // Bounds size in device pixels, before the scene transform.
    std::vector<D2D1_SIZE_F> boundsPixels;
    // Axis-aligned scene transform in DIPs, applied the way TextBox, Menu and EmojiPanel nest their content.
    std::vector<D2D1_MATRIX_3X2_F> transforms;
};

int CheckImagePixels(const Scenario &scenario)
{
    ImageFixture fixture;
    msimeui::DeviceResources resources;
    REQUIRE(resources.EnsureForComposition(fixture.window.hwnd));
    ID2D1DeviceContext *dc = resources.GetDeviceContext();
    ID2D1Bitmap *bitmap = resources.GetBitmapFromFile(fixture.file.path);
    REQUIRE(bitmap != nullptr);
    REQUIRE(bitmap->GetPixelSize().width == kImageWidth);
    REQUIRE(bitmap->GetPixelSize().height == kImageHeight);
    msimeui::Image image(fixture.file.path);
    image.SetStretch(scenario.stretch);
    int worst = 0;
    for (const float dpi : scenario.dpis)
    {
        dc->SetDpi(dpi, dpi);
        float dpiX = 0.0f;
        float dpiY = 0.0f;
        dc->GetDpi(&dpiX, &dpiY);
        REQUIRE_NEAR(dpiX, dpi);
        REQUIRE_NEAR(dpiY, dpi);
        Canvas canvas(dc, dpi);
        const float pixelsPerDip = dpi / 96.0f;
        for (const auto &transform : scenario.transforms)
        {
            // Device pixels per local DIP under this transform
            const float scale = transform._11 * pixelsPerDip;
            for (const auto &boundsPixels : scenario.boundsPixels)
            {
                for (const float offset : {0.25f, 0.5f, 0.75f})
                {
                    const float boundsX = 8.0f + offset;
                    const float boundsY = 10.0f + offset;
                    const msimeui::RectF bounds{(boundsX - transform._31 * pixelsPerDip) / scale,
                                                (boundsY - transform._32 * pixelsPerDip) / scale,
                                                boundsPixels.width / scale, boundsPixels.height / scale};
                    image.Arrange(bounds);
                    const auto actual = canvas.Render([&]() {
                        dc->SetTransform(transform);
                        image.Render(resources);
                    });

                    // Reference drawn in device pixels under an identity transform
                    float imageWidth = static_cast<float>(kImageWidth);
                    float imageHeight = static_cast<float>(kImageHeight);
                    float x = boundsX;
                    float y = boundsY;
                    if (scenario.stretch == msimeui::ImageStretch::Uniform)
                    {
                        const float fit =
                            (std::min)(boundsPixels.width / imageWidth, boundsPixels.height / imageHeight);
                        imageWidth *= fit;
                        imageHeight *= fit;
                        x += (boundsPixels.width - imageWidth) * 0.5f;
                        y += (boundsPixels.height - imageHeight) * 0.5f;
                    }
                    const float visibleWidth = (std::min)(imageWidth, boundsPixels.width);
                    const float visibleHeight = (std::min)(imageHeight, boundsPixels.height);
                    if (std::fabs(imageWidth - kImageWidth) < 0.01f && std::fabs(imageHeight - kImageHeight) < 0.01f)
                    {
                        x = std::round(x);
                        y = std::round(y);
                    }
                    const auto expected = canvas.Render([&]() {
                        ID2D1RenderTarget *target = resources.GetRenderTarget();
                        const bool clip = scenario.stretch != msimeui::ImageStretch::Uniform;
                        if (clip)
                        {
                            target->PushAxisAlignedClip(D2D1::RectF(x / pixelsPerDip, y / pixelsPerDip,
                                                                    (x + visibleWidth) / pixelsPerDip,
                                                                    (y + visibleHeight) / pixelsPerDip),
                                                        D2D1_ANTIALIAS_MODE_ALIASED);
                        }
                        target->DrawBitmap(bitmap,
                                           D2D1::RectF(x / pixelsPerDip, y / pixelsPerDip,
                                                       (x + imageWidth) / pixelsPerDip,
                                                       (y + imageHeight) / pixelsPerDip),
                                           1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                        if (clip)
                        {
                            target->PopAxisAlignedClip();
                        }
                    });
                    int difference = 0;
                    for (size_t i = 0; i < actual.size(); ++i)
                    {
                        difference = (std::max)(difference, std::abs(static_cast<int>(actual[i]) - expected[i]));
                    }
                    worst = (std::max)(worst, difference);
                    if (difference > 1)
                    {
                        std::printf("image mismatch dpi=%.0f bounds=%.0fx%.0f scale=%.2f translate=%.2f offset=%.2f "
                                    "max=%d\n",
                                    dpi, boundsPixels.width, boundsPixels.height, transform._11, transform._31, offset,
                                    difference);
                    }
                }
            }
        }
    }
    return worst;
}

const D2D1_SIZE_F kNativePixels = {static_cast<float>(kImageWidth), static_cast<float>(kImageHeight)};
} // namespace

TEST_CASE(native_size_image_keeps_pixel_detail_at_fractional_origins_and_dpi_changes)
{
    Scenario scenario;
    scenario.dpis = {96.0f, 120.0f, 144.0f, 192.0f, 96.0f};
    scenario.boundsPixels = {kNativePixels};
    scenario.transforms = {D2D1::Matrix3x2F::Identity()};
    const int worst = CheckImagePixels(scenario);
    std::printf("native image worst channel difference %d/255\n", worst);
    REQUIRE(worst <= 1);
}

TEST_CASE(native_size_image_snaps_in_device_space_under_translation_and_scale)
{
    Scenario scenario;
    scenario.dpis = {96.0f, 144.0f};
    scenario.boundsPixels = {kNativePixels};
    scenario.transforms = {D2D1::Matrix3x2F::Translation(0.3f, 0.6f), D2D1::Matrix3x2F::Scale(1.25f, 1.25f),
                           D2D1::Matrix3x2F::Scale(1.25f, 1.25f) * D2D1::Matrix3x2F::Translation(0.3f, 0.6f)};
    const int worst = CheckImagePixels(scenario);
    std::printf("transformed native image worst channel difference %d/255\n", worst);
    REQUIRE(worst <= 1);
}

TEST_CASE(scaled_image_preserves_linear_interpolation)
{
    Scenario scenario;
    scenario.dpis = {96.0f, 120.0f, 144.0f, 192.0f, 96.0f};
    for (const float resize : {0.75f, 1.25f, 2.0f})
    {
        scenario.boundsPixels.push_back({kImageWidth * resize, kImageHeight * resize});
    }
    scenario.transforms = {D2D1::Matrix3x2F::Identity()};
    const int worst = CheckImagePixels(scenario);
    std::printf("scaled image worst channel difference %d/255\n", worst);
    REQUIRE(worst <= 1);
}

TEST_CASE(clipped_native_size_image_keeps_its_edges_after_snapping)
{
    // Bitmaps decode at 96 DPI, so None and UniformToFill are 1:1 only there.
    for (const auto stretch : {msimeui::ImageStretch::None, msimeui::ImageStretch::UniformToFill})
    {
        Scenario scenario;
        scenario.stretch = stretch;
        scenario.dpis = {96.0f};
        scenario.boundsPixels = {kNativePixels};
        scenario.transforms = {D2D1::Matrix3x2F::Identity()};
        const int worst = CheckImagePixels(scenario);
        std::printf("clipped native image worst channel difference %d/255\n", worst);
        REQUIRE(worst <= 1);
    }
}

TEST_CASE(unstretched_image_crops_instead_of_squeezing_into_smaller_bounds)
{
    Scenario scenario;
    scenario.stretch = msimeui::ImageStretch::None;
    scenario.dpis = {96.0f};
    scenario.boundsPixels = {{100.0f, 80.0f}};
    scenario.transforms = {D2D1::Matrix3x2F::Identity()};
    const int worst = CheckImagePixels(scenario);
    std::printf("cropped image worst channel difference %d/255\n", worst);
    REQUIRE(worst <= 1);
}
