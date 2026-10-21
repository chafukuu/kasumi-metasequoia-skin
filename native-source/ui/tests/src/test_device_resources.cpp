#include "tests/includes/test_framework.h"

#include "msimeui/DeviceResources.h"

#include <cmath>
#include <cstdio>

namespace
{
class HiddenWindow
{
  public:
    HiddenWindow()
    {
        REQUIRE(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)));
        hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, L"STATIC", L"DPI resource test", WS_POPUP, 0, 0, 256, 128,
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

void CheckReusedTargetDpi(bool composition)
{
    HiddenWindow window;
    msimeui::DeviceResources resources;
    const auto ensure = [&]() {
        return composition ? resources.EnsureForComposition(window.hwnd) : resources.EnsureForWindow(window.hwnd);
    };
    REQUIRE(ensure());
    ID2D1RenderTarget *const target = resources.GetRenderTarget();
    REQUIRE(target != nullptr);
    const float windowDpi = static_cast<float>(GetDpiForWindow(window.hwnd));

    // Reproduce the retained render target from the previous monitor DPI without
    // changing the user's display settings. Reuse must refresh DPI, not recreate it.
    for (const float previousDpi : {windowDpi / 2.0f, 96.0f, 120.0f, 144.0f, 192.0f, windowDpi * 2.0f})
    {
        target->SetDpi(previousDpi, previousDpi);
        REQUIRE(ensure());
        REQUIRE(resources.GetRenderTarget() == target);
        float dpiX = 0.0f;
        float dpiY = 0.0f;
        target->GetDpi(&dpiX, &dpiY);
        std::printf("%s retained DPI %.0f -> window %.0f, target %.0f/%.0f\n", composition ? "composition" : "HWND",
                    previousDpi, windowDpi, dpiX, dpiY);
        REQUIRE_NEAR(dpiX, windowDpi);
        REQUIRE_NEAR(dpiY, windowDpi);
        const D2D1_SIZE_F size = target->GetSize();
        const D2D1_SIZE_U pixels = target->GetPixelSize();
        REQUIRE_NEAR(size.width, pixels.width * 96.0f / windowDpi);
        REQUIRE_NEAR(size.height, pixels.height * 96.0f / windowDpi);
    }

    // A smaller client area also reuses the larger composition surface.
    REQUIRE(SetWindowPos(window.hwnd, nullptr, 0, 0, 128, 64, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE));
    target->SetDpi(windowDpi * 1.25f, windowDpi * 1.25f);
    REQUIRE(ensure());
    REQUIRE(resources.GetRenderTarget() == target);
    float dpiX = 0.0f;
    float dpiY = 0.0f;
    target->GetDpi(&dpiX, &dpiY);
    REQUIRE_NEAR(dpiX, windowDpi);
    REQUIRE_NEAR(dpiY, windowDpi);
}
} // namespace

TEST_CASE(hwnd_target_refreshes_dpi_when_reused)
{
    CheckReusedTargetDpi(false);
}

TEST_CASE(composition_target_refreshes_dpi_when_reused)
{
    CheckReusedTargetDpi(true);
}

// Backdrop composition draws into a Windows.UI.Composition drawing surface: its device context
// only exists inside a frame and draws at the window DPI, the surface survives the window
// growing, and the window can go back to a swap chain afterwards.
TEST_CASE(backdrop_composition_draws_through_a_drawing_surface)
{
    HiddenWindow window;
    msimeui::DeviceResources resources;
    resources.SetBackdropComposition(true);
    REQUIRE(resources.EnsureForComposition(window.hwnd));
    REQUIRE(resources.UsesComposition());
    const float windowDpi = static_cast<float>(GetDpiForWindow(window.hwnd));

    for (const int width : {256, 600})
    {
        REQUIRE(SetWindowPos(window.hwnd, nullptr, 0, 0, width, 128, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE));
        REQUIRE(resources.EnsureForComposition(window.hwnd));
        resources.Resize(static_cast<UINT>(width), 128);
        REQUIRE(resources.BeginDraw());
        ID2D1RenderTarget *target = resources.GetRenderTarget();
        REQUIRE(target != nullptr);
        float dpiX = 0.0f;
        float dpiY = 0.0f;
        target->GetDpi(&dpiX, &dpiY);
        REQUIRE_NEAR(dpiX, windowDpi);
        target->Clear(D2D1::ColorF(0, 0.0f));
        ID2D1SolidColorBrush *brush = resources.GetSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Red));
        REQUIRE(brush != nullptr);
        target->FillRectangle(D2D1::RectF(0.0f, 0.0f, 10.0f, 10.0f), brush);
        resources.SetBackdrop({4.0f, 4.0f, static_cast<float>(width) / 2.0f, 50.0f}, 8.0f);
        REQUIRE(SUCCEEDED(resources.EndDraw()));
        REQUIRE(SUCCEEDED(resources.Present()));
        // Between frames resources are still created on the device's own context.
        REQUIRE(resources.GetRenderTarget() != nullptr);
        REQUIRE(resources.GetRenderTarget() != target);
    }

    // Materials build their effect graph on the live surface; a bad graph would drop the surface.
    for (const auto source :
         {msimeui::BackdropMaterial::Source::WindowsBehind, msimeui::BackdropMaterial::Source::Wallpaper})
    {
        msimeui::BackdropMaterial material;
        material.source = source;
        material.tint = D2D1::ColorF(0x202020);
        material.tintOpacity = 0.8f;
        material.luminosityOpacity = 1.0f;
        resources.SetBackdropMaterial(material);
        REQUIRE(resources.EnsureForComposition(window.hwnd));
        REQUIRE(resources.BeginDraw());
        resources.GetRenderTarget()->Clear(D2D1::ColorF(0, 0.0f));
        resources.SetBackdrop({4.0f, 4.0f, 120.0f, 50.0f}, 8.0f);
        REQUIRE(SUCCEEDED(resources.EndDraw()));
    }

    resources.SetBackdropComposition(false);
    REQUIRE(resources.EnsureForComposition(window.hwnd));
    REQUIRE(resources.BeginDraw());
    resources.GetRenderTarget()->Clear(D2D1::ColorF(0, 0.0f));
    REQUIRE(SUCCEEDED(resources.EndDraw()));
    REQUIRE(SUCCEEDED(resources.Present()));
}
