// 托盘语言栏菜单宿主：记录语言栏图标位置，以及 WndProcMenuWindow。菜单的绘制、尺寸与定位由 TrayMenuPresenter 负责。
#include "window/ime_windows_internal.h"
#include "global/globals.h"
#include "config/ime_config.h"
#include "ipc/ipc.h"
#include "ime_windows.h"
#include "window/tray_menu_presenter.h"
#include "defines/defines.h"
#include "defines/globals.h"
#include <memory>
#include "window_hook.h"

using namespace ime_windows_detail;

namespace
{
RECT g_trayMenuAnchor{};
} // namespace

RECT GetTrayMenuAnchorRect()
{
    return g_trayMenuAnchor;
}

LRESULT CALLBACK WndProcMenuWindow(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (TrayMenuPresenter::Instance().HandleMessage(message, wParam, lParam))
    {
        return message == WM_ERASEBKGND ? 1 : 0;
    }

    switch (message)
    {
    case WM_MOUSEACTIVATE:
        // Same contract as the candidate / floating-toolbar hosts: the tray
        // language-bar menu must never take foreground. Stealing activation
        // drops the focused app's TSF document and forces a full IME
        // disconnect/reconnect on every right-click.
        return MA_NOACTIVATE;

    case WM_ACTIVATE: {
        if (LOWORD(wParam) != WA_INACTIVE)
        {
            ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        }
        break;
    }

    case WM_LANGBAR_RIGHTCLICK: {
        // A fresh right-click carries the icon rect; a post with lParam 0 keeps
        // the anchor captured by the previous right-click.
        if (std::unique_ptr<RECT> icon{reinterpret_cast<RECT *>(lParam)})
        {
            g_trayMenuAnchor = *icon;
        }
        if (TrayMenuPresenter::Instance().IsBound())
        {
            TrayMenuPresenter::Instance().ShowFromLangBar();
            SetHostWindowCloaked(::global_hwnd_menu, false);
            if (!g_mouseHook)
            {
                g_mouseHook = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, nullptr, 0);
            }
        }
        break;
    }

    case WM_DPICHANGED: {
        if (TrayMenuPresenter::Instance().IsBound() && TrayMenuPresenter::Instance().IsOpenToUser())
        {
            TrayMenuPresenter::Instance().ShowFromLangBar();
        }
        return 0;
    }

    default: {
        return DefWindowProc(hwnd, message, wParam, lParam);
    }
    }
    return 0;
}
