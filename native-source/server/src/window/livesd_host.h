#pragma once

#include <windows.h>
#include <array>
#include <cstdint>

namespace LiveSDHost
{
inline constexpr UINT kActivityMessage = WM_APP + 0x3B0;
struct FrameTimingTelemetry
{
    std::uint64_t scheduled = 0;
    std::uint64_t rendered = 0;
    std::uint64_t missed = 0;
    std::uint64_t maxLatenessNs = 0;
    std::uint64_t maxRenderDurationNs = 0;
    bool highResolutionTimer = false;
    bool timerResolutionRaised = false;
    bool waitableTimer = false;
    std::uint64_t intervalSampleCount = 0;
    std::uint64_t intervalTotalCount = 0;
    std::array<std::uint64_t, 2048> intervalSamplesNs{};
    std::uint64_t renderDurationSampleCount = 0;
    std::uint64_t renderDurationTotalCount = 0;
    std::array<std::uint64_t, 2048> renderDurationsNs{};
};
inline constexpr UINT kInputModeMessage = WM_APP + 0x3B2;
struct Activity
{
    std::uint64_t client = 0;
    std::uint64_t epoch = 0;
    unsigned kind = 0; // Shared FanyImeLiveSDActivity kind, including ordered COMBO typing/reset.
    POINT caret{};     // physical screen pixels; never text
    HWND contextRootWindow = nullptr;
    std::uint32_t typingIntervalMs = 0;
    bool englishInputMode = false;
};
// Worker -> UI. No HWND is created and no painting occurs on the worker.
void PostActivity(const Activity &activity);
void PostInputModeState(std::uint64_t client, std::uint64_t epoch, bool englishInputMode);
bool HandleActivityMessage(LPARAM payload);
bool HandleInputModeStateMessage(LPARAM payload);
// UI-thread lifecycle. Candidate hiding only releases its anchor.
void SetCandidateAnchor(const RECT &cardPixels, float scale);
void ClearCandidateAnchor();
void SyncTopmost();
FrameTimingTelemetry GetFrameTimingTelemetry();
void Shutdown();
} // namespace LiveSDHost
