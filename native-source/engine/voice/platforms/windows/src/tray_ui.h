#pragma once

#include <string>
#include <windows.h>

namespace tray_ui
{
struct TrayMenuConfig
{
    DWORD main_thread_id = 0;
    UINT msg_toggle_record = 0;
    UINT msg_exit = 0;
    std::wstring app_name = L"MetasequoiaVoiceInput";
};

bool InitializeTrayUi(HINSTANCE instance, const TrayMenuConfig &config);
void ShutdownTrayUi(HINSTANCE instance);
} // namespace tray_ui
