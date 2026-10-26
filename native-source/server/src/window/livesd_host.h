#pragma once

#include <windows.h>
#include <cstdint>

namespace LiveSDHost
{
inline constexpr UINT kActivityMessage = WM_APP + 0x3B0;
struct Activity
{
    std::uint64_t client = 0;
    std::uint64_t epoch = 0;
    unsigned kind = 0; // focus/caret = 0, typing pulse = 1, blur = 2, Enter action = 3
    POINT caret{};     // physical screen pixels; never text
    HWND contextRootWindow = nullptr;
    std::uint32_t typingIntervalMs = 0;
};
// Worker -> UI. No HWND is created and no painting occurs on the worker.
void PostActivity(const Activity &activity);
bool HandleActivityMessage(LPARAM payload);
// UI-thread lifecycle. Candidate hiding only releases its anchor.
void SetCandidateAnchor(const RECT &cardPixels, float scale);
void ClearCandidateAnchor();
void SyncTopmost();
void Shutdown();
} // namespace LiveSDHost
