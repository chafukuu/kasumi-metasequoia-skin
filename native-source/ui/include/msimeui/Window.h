#pragma once

#include "DeviceResources.h"
#include "Scene.h"

#include <memory>
#include <string>

namespace msimeui
{
enum class WindowInitialPlacement
{
    Center,
    BottomCenter,
};

class Window
{
  public:
    Window(std::wstring className, std::wstring title, int width, int height);
    ~Window();

    bool Create();
    bool AdoptExistingHwnd(HWND hwnd);
    LRESULT DispatchImportedMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void SetStealFocusOnClick(bool enabled);
    int Run(int nCmdShow);

    HWND GetHandle() const;
    HINSTANCE GetInstance() const;
    Scene *GetScene() const;
    DeviceResources &GetDeviceResources();
    /// DPI override, mirrored into the window's own DeviceResources. Only for
    /// cases where the system window DPI diverges from the content's real
    /// scale (RDP client-scaling sync); see DeviceResources::SetDpiOverride.
    /// GetDpi() and every pixels<->DIPs conversion (hit testing, drag region,
    /// layout) honor it while set.
    void SetDpiOverride(FLOAT dpi);
    float GetDpi() const;
    PointF ClientPixelsToDips(const POINT &point) const;
    SizeF ClientPixelsToDips(const SIZE &size) const;
    RECT DipsToClientPixels(const RectF &rect) const;
    void Invalidate();
    void Invalidate(const RectF &rect);
    void InvalidateMeasure();
    void InvalidateMeasure(Visual *source);
    void InvalidateArrange();
    void InvalidateArrange(Visual *source);
    void Relayout();
    void FocusVisual(Visual *visual);
    void SetWindowStyle(DWORD style, DWORD extendedStyle = 0);
    void SetDragRegionHeight(float height);
    void SetRoundedCorners(bool enabled);
    void SetInitialPlacement(WindowInitialPlacement placement, int bottomMargin = 12);
    // Hosts that own the message loop (an adopted HWND in a larger application) keep
    // running after this window goes away.
    void SetQuitOnDestroy(bool quit);
    // Replaces ThemeManager's windowBackground as the color each frame is cleared to.
    void SetBackgroundColor(const D2D1_COLOR_F &color);
    void SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE mode);
    // Presents through a DirectComposition swap chain instead of an HWND render target.
    // Needed by windows whose frame is drawn by the app over DWM's extended frame, where
    // an HWND target's output is not composited; it also avoids the redirection copy.
    // `opaque` ignores alpha, which an opaque window needs for ClearType text.
    void SetCompositionRendering(bool enabled, bool opaque = false);
    // Caret blinking for TextBox. Create() starts it; an adopted HWND has to ask.
    void StartCaretTimer();

    // Schedules another frame after the current one. Animated visuals call this from
    // Render while they are still moving, so frames keep coming at the display rate
    // and stop on their own when everything has settled.
    void RequestAnimationFrame();
    // Timestamp of the frame being rendered (NowMs() outside of painting): when it will
    // reach the screen, as far as the compositor tells. Every animation sampled in one
    // frame sees the same time.
    double GetFrameTime() const;
    // Re-evaluates which visual is under the cursor, e.g. after content scrolled or
    // was replaced without the mouse moving.
    void RefreshHover();

    void SetScene(std::unique_ptr<Scene> scene);

  private:
    // Repaints when the next frame of an animated image is due.
    static constexpr UINT_PTR kImageAnimationTimerId = 0x4D49;

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void OnPaint();
    double NextFrameTimeMs() const;
    void OnSize();
    bool OnMouseWheel(WPARAM wParam, LPARAM lParam);
    LRESULT OnLeftButtonDown(WPARAM wParam, LPARAM lParam);
    void UpdateHover(const POINT &point);
    void SetFocusedVisual(Visual *visual);
    bool UpdateCursorForClientPoint(const POINT &point);

    std::wstring className_;
    std::wstring title_;
    int width_ = 0;
    int height_ = 0;
    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    FLOAT dpiOverride_ = 0.0f;
    DeviceResources deviceResources_;
    std::unique_ptr<Scene> scene_;
    Visual *focusedVisual_ = nullptr;
    Visual *hoveredVisual_ = nullptr;
    Visual *capturedVisual_ = nullptr;
    bool mouseLeaveTracking_ = false;
    DWORD windowStyle_ = WS_OVERLAPPEDWINDOW;
    DWORD extendedWindowStyle_ = 0;
    float dragRegionHeight_ = 0.0f;
    bool roundedCorners_ = false;
    WindowInitialPlacement initialPlacement_ = WindowInitialPlacement::Center;
    int initialBottomMargin_ = 12;
    bool stealFocusOnClick_ = true;
    bool adoptedHwnd_ = false;
    bool quitOnDestroy_ = true;
    bool hasBackgroundColor_ = false;
    D2D1_COLOR_F backgroundColor_ = {};
    D2D1_TEXT_ANTIALIAS_MODE textAntialiasMode_ = D2D1_TEXT_ANTIALIAS_MODE_DEFAULT;
    bool painting_ = false;
    bool composition_ = false;
    bool animationRequested_ = false;
    double frameTimeMs_ = 0.0;
};
} // namespace msimeui
