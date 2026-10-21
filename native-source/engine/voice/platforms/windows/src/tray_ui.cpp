#include "tray_ui.h"
#include "mvi_config.h"
#include "resource/resource.h"
#include <atomic>
#include <cstdio>
#include <objbase.h>
#include <shellapi.h>
#include <thread>

namespace tray_ui
{
namespace
{
constexpr UINT WM_APP_TRAY_ICON = WM_APP + 120;
constexpr UINT k_tray_icon_id = 1;
constexpr wchar_t k_tray_window_class[] = L"MetasequoiaVoiceInput.TrayWindow";
constexpr wchar_t k_tray_tooltip[] = L"MetasequoiaVoiceInput";
constexpr wchar_t k_project_url[] = L"https://github.com/metasequoiaime/MSIME-Engine/tree/main/voice";

enum TrayCommand : UINT
{
    k_cmd_settings = 1,
    k_cmd_about,
    k_cmd_exit,
};

struct TrayUiState
{
    TrayMenuConfig config{};
    HWND tray_window = nullptr;
    NOTIFYICONDATAW notify_icon{};
    bool notify_icon_added = false;
    HICON tray_icon = nullptr;
};

TrayUiState g_state;
std::atomic<bool> g_about_open{false};

std::wstring AnsiToWide(const std::string &text)
{
    if (text.empty())
    {
        return L"";
    }
    const int size = MultiByteToWideChar(CP_ACP, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0)
    {
        return L"";
    }
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_ACP, 0, text.c_str(), static_cast<int>(text.size()), result.data(), size);
    return result;
}

// There is no settings form: the menu opens config.toml in the user's editor. A missing file is
// first written with the defaults so there is something to edit.
void OpenConfigFile()
{
    const std::string config_path = mvi_config::GetConfigPath();
    if (config_path.empty())
    {
        printf("[SETTINGS] LOCALAPPDATA is empty; cannot locate config.toml.\n");
        fflush(stdout);
        return;
    }

    const std::wstring wide_path = AnsiToWide(config_path);
    if (GetFileAttributesW(wide_path.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        std::string error_message;
        if (!mvi_config::WriteConfigFromJson("{}", &error_message))
        {
            printf("[SETTINGS] Failed to create default config.toml: %s\n", error_message.c_str());
            fflush(stdout);
            return;
        }
    }

    const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", wide_path.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (result > 32)
    {
        return;
    }

    // .toml frequently has no association; fall back to Notepad.
    const std::wstring quoted = L"\"" + wide_path + L"\"";
    ShellExecuteW(nullptr, L"open", L"notepad.exe", quoted.c_str(), nullptr, SW_SHOWNORMAL);
}

// A MessageBox on the main thread would run a modal loop that drops the thread messages the
// keyboard hook posts, so the About box lives on its own short-lived thread.
void ShowAbout()
{
    bool expected = false;
    if (!g_about_open.compare_exchange_strong(expected, true))
    {
        return;
    }

    std::thread([]() {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        const int choice = MessageBoxW(nullptr,
                                       L"水杉记言 (Metasequoia Voice Input) 是一个面向中文输入场景的开源语音输入工具，"
                                       L"通过云端语音识别 (ASR) 将语音转换为文本，并可选接入文本润色。\n\n"
                                       L"设置项保存在 %LOCALAPPDATA%\\MetasequoiaVoiceInput\\config.toml。\n\n"
                                       L"是否在浏览器中打开项目主页？",
                                       L"关于 MetasequoiaVoiceInput", MB_YESNO | MB_ICONINFORMATION | MB_SETFOREGROUND);
        if (choice == IDYES)
        {
            ShellExecuteW(nullptr, L"open", k_project_url, nullptr, nullptr, SW_SHOWNORMAL);
        }
        if (SUCCEEDED(hr))
        {
            CoUninitialize();
        }
        g_about_open = false;
    }).detach();
}

void ShowTrayMenu(HWND hwnd)
{
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr)
    {
        return;
    }
    AppendMenuW(menu, MF_STRING, k_cmd_settings, L"设置（编辑 config.toml）");
    AppendMenuW(menu, MF_STRING, k_cmd_about, L"关于");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, k_cmd_exit, L"退出");

    POINT cursor{};
    GetCursorPos(&cursor);

    // The foreground/WM_NULL pair is what makes the menu dismiss on an outside click.
    SetForegroundWindow(hwnd);
    UINT flags = TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY;
    flags |= GetSystemMetrics(SM_MENUDROPALIGNMENT) != 0 ? TPM_RIGHTALIGN : TPM_LEFTALIGN;
    const UINT command = static_cast<UINT>(TrackPopupMenuEx(menu, flags, cursor.x, cursor.y, hwnd, nullptr));
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);

    switch (command)
    {
    case k_cmd_settings:
        OpenConfigFile();
        break;
    case k_cmd_about:
        ShowAbout();
        break;
    case k_cmd_exit:
        printf("[TRAY] Exit selected from tray menu.\n");
        fflush(stdout);
        PostThreadMessageW(g_state.config.main_thread_id, g_state.config.msg_exit, 0, 0);
        break;
    default:
        break;
    }
}

LRESULT CALLBACK TrayWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_APP_TRAY_ICON)
    {
        const UINT event = LOWORD(lParam);
        if (event == WM_RBUTTONUP || event == WM_CONTEXTMENU)
        {
            ShowTrayMenu(hwnd);
            return 0;
        }
        if (event == WM_LBUTTONDBLCLK && g_state.config.msg_toggle_record != 0)
        {
            PostThreadMessageW(g_state.config.main_thread_id, g_state.config.msg_toggle_record, 0, 0);
            return 0;
        }
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}
} // namespace

bool InitializeTrayUi(HINSTANCE instance, const TrayMenuConfig &config)
{
    g_state = {};
    g_state.config = config;

    WNDCLASSW wc{};
    wc.lpfnWndProc = TrayWindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = k_tray_window_class;
    RegisterClassW(&wc);

    // A hidden top-level window rather than HWND_MESSAGE: TrackPopupMenu needs an owner that can
    // become the foreground window, which a message-only window cannot.
    g_state.tray_window = CreateWindowExW(WS_EX_TOOLWINDOW, k_tray_window_class, L"MetasequoiaVoiceInput.TrayHost", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    if (g_state.tray_window == nullptr)
    {
        printf("[TRAY] Failed to create tray host window: %lu\n", GetLastError());
        fflush(stdout);
        return false;
    }

    g_state.tray_icon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
    if (g_state.tray_icon == nullptr)
    {
        g_state.tray_icon = LoadIconW(nullptr, IDI_APPLICATION);
    }

    g_state.notify_icon = {};
    g_state.notify_icon.cbSize = sizeof(NOTIFYICONDATAW);
    g_state.notify_icon.hWnd = g_state.tray_window;
    g_state.notify_icon.uID = k_tray_icon_id;
    g_state.notify_icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_state.notify_icon.uCallbackMessage = WM_APP_TRAY_ICON;
    g_state.notify_icon.hIcon = g_state.tray_icon;
    wcscpy_s(g_state.notify_icon.szTip, k_tray_tooltip);

    if (!Shell_NotifyIconW(NIM_ADD, &g_state.notify_icon))
    {
        printf("[TRAY] Failed to add tray icon.\n");
        fflush(stdout);
        return false;
    }

    g_state.notify_icon_added = true;
    g_state.notify_icon.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &g_state.notify_icon);

    printf("[TRAY] Tray icon initialized.\n");
    fflush(stdout);
    return true;
}

void ShutdownTrayUi(HINSTANCE instance)
{
    if (g_state.notify_icon_added)
    {
        Shell_NotifyIconW(NIM_DELETE, &g_state.notify_icon);
        g_state.notify_icon_added = false;
    }
    if (g_state.tray_window != nullptr)
    {
        DestroyWindow(g_state.tray_window);
        g_state.tray_window = nullptr;
    }
    UnregisterClassW(k_tray_window_class, instance);
}
} // namespace tray_ui
