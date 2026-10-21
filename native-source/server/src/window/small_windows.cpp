#include "window/small_windows.h"

#include "config/ime_config.h"
#include "defines/globals.h"
#include "global/globals.h"
#include "log/ftb_diag_log.h"
#include "window/candidate_presenter.h"
#include "window/floating_toolbar_presenter.h"
#include "window/ime_windows.h"
#include "window/tray_menu_presenter.h"

namespace
{
bool smallWindowTopmostRequested = false;
bool smallWindowTopmostApplied = false;
// Guards against scheduling the staggered pin more than once.
bool smallWindowTopmostScheduled = false;

// The three small-window hosts enter the topmost band one at a time, on their
// own timers, rather than together, so no two hosts change bands in the same
// frame. The tray menu goes last because later HWND_TOPMOST wins within the
// band, which is the order an open menu needs. Steps must stay listed in firing
// order: the timer id is the enum value, and the delays are indexed by it.
enum class SmallWindowTopmostStep
{
    Candidate,
    FloatingToolbar,
    TrayMenu,
};
constexpr UINT_PTR kTopmostStepTimerIdBase = 9100;
constexpr UINT kTopmostStepCount = 3;
constexpr UINT kCandidateTopmostDelayMs = 1000;
constexpr UINT kFloatingToolbarTopmostDelayMs = 1200;
constexpr UINT kTrayMenuTopmostDelayMs = 1400;
HWND smallWindowTopmostTimerHost = nullptr;
// Counted down rather than finalizing in whichever case happens to be last, so
// reordering the steps cannot silently leave the gate open or strand a timer.
UINT smallWindowTopmostStepsPending = 0;

uint64_t candidateSkinReloadRevision = 0;

bool AreSmallWindowPresentersBound()
{
    return CandidatePresenter::Instance().IsBound() && TrayMenuPresenter::Instance().IsBound() &&
           FloatingToolbarPresenter::Instance().IsBound();
}

void PinHostTopmost(HWND hwnd)
{
    if (!hwnd)
    {
        return;
    }
    constexpr UINT flag = SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE;
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, flag);
}

void ApplySmallWindowTopmostStep(SmallWindowTopmostStep step)
{
    FTB_DIAG_LOGF(L"topmost step {} applying", step == SmallWindowTopmostStep::Candidate         ? L"candidate"
                                               : step == SmallWindowTopmostStep::FloatingToolbar ? L"floating-toolbar"
                                                                                                 : L"tray-menu");
    switch (step)
    {
    case SmallWindowTopmostStep::Candidate:
        PinHostTopmost(::global_hwnd);
        if (CandidatePresenter::Instance().IsBound() && ::is_global_wnd_cand_shown && ::global_hwnd)
        {
            CandidatePresenter::Instance().Present();
        }
        break;

    case SmallWindowTopmostStep::FloatingToolbar:
        PinHostTopmost(::global_hwnd_ftb);
        if (FloatingToolbarPresenter::Instance().IsBound())
        {
            FloatingToolbarPresenter::Instance().Present();
        }
        // The menu step lands a moment later and would fix the order anyway;
        // raising now keeps an already-open menu from being covered in between.
        if (IsTrayMenuOpenToUser())
        {
            RaiseTrayMenuAboveSmallWindows(L"after-staggered-topmost");
        }
        break;

    case SmallWindowTopmostStep::TrayMenu:
        PinHostTopmost(::global_hwnd_menu);
        if (TrayMenuPresenter::Instance().IsBound() && TrayMenuPresenter::Instance().IsOpenToUser())
        {
            TrayMenuPresenter::Instance().Present();
        }
        break;
    }

    if (smallWindowTopmostStepsPending > 0 && --smallWindowTopmostStepsPending == 0)
    {
        // Only now is the whole band in effect, and no timer is left to cancel.
        smallWindowTopmostApplied = true;
        smallWindowTopmostTimerHost = nullptr;
    }
}

void CALLBACK SmallWindowTopmostTimerProc(HWND hwnd, UINT, UINT_PTR timerId, DWORD)
{
    KillTimer(hwnd, timerId);
    ApplySmallWindowTopmostStep(static_cast<SmallWindowTopmostStep>(timerId - kTopmostStepTimerIdBase));
}

// Returns false when there is no host window to hang the timers on, leaving the
// caller to pin inline rather than never.
bool ScheduleStaggeredTopmost()
{
    const HWND timer_host = ::global_hwnd_ftb ? ::global_hwnd_ftb : ::global_hwnd;
    if (!timer_host)
    {
        return false;
    }
    const UINT delays[kTopmostStepCount] = {kCandidateTopmostDelayMs, kFloatingToolbarTopmostDelayMs,
                                            kTrayMenuTopmostDelayMs};
    UINT scheduled = 0;
    for (UINT_PTR step = 0; step < kTopmostStepCount; ++step)
    {
        if (SetTimer(timer_host, kTopmostStepTimerIdBase + step, delays[step], SmallWindowTopmostTimerProc) != 0)
        {
            ++scheduled;
        }
    }
    // Counting what was armed rather than kTopmostStepCount keeps the countdown
    // able to reach zero if SetTimer fails for one of the steps.
    smallWindowTopmostStepsPending = scheduled;
    smallWindowTopmostTimerHost = scheduled > 0 ? timer_host : nullptr;
    return scheduled > 0;
}

void TryApplyPendingLazyTopmost(const wchar_t *reason)
{
    if (!smallWindowTopmostRequested || smallWindowTopmostApplied)
    {
        return;
    }
    if (!AreSmallWindowPresentersBound())
    {
        return;
    }
    // Already scheduled: pinning again from whatever is running right now would
    // only repeat the band transitions the timers are about to perform.
    if (smallWindowTopmostScheduled)
    {
        return;
    }
    smallWindowTopmostScheduled = true;
    if (ScheduleStaggeredTopmost())
    {
        FTB_DIAG_LOGF(L"topmost staggered from reason={}: candidate +{}ms, floating-toolbar +{}ms, "
                      L"tray-menu +{}ms",
                      reason, kCandidateTopmostDelayMs, kFloatingToolbarTopmostDelayMs, kTrayMenuTopmostDelayMs);
        return;
    }
    FTB_DIAG_LOGF(L"topmost timers unavailable for reason={}, pinning inline", reason);
    smallWindowTopmostStepsPending = kTopmostStepCount;
    ApplySmallWindowTopmostStep(SmallWindowTopmostStep::Candidate);
    ApplySmallWindowTopmostStep(SmallWindowTopmostStep::FloatingToolbar);
    ApplySmallWindowTopmostStep(SmallWindowTopmostStep::TrayMenu);
}

//
// 悬浮工具栏的状态缓存
//
struct FloatingToolbarState
{
    // Keep these defaults aligned with the TSF compartment defaults:
    // Chinese input, half-width characters, and Chinese punctuation.
    int cn_en = 1;
    int double_single_byte = 0;
    int punctuation = 1;
    int english_input_mode = 0;
    int caps_lock = 0;
    int japanese_input_mode = 0;
};

FloatingToolbarState floatingToolbarState;

bool UpdateBinaryState(int value, int &state)
{
    // The state contract only defines 0 and 1. Ignore malformed values instead
    // of replacing a known-good cached state.
    if ((value == 0 || value == 1) && value != state)
    {
        state = value;
        return true;
    }
    return false;
}

void RenderFloatingToolbarState()
{
    if (FloatingToolbarPresenter::Instance().IsBound())
    {
        FloatingToolbarPresenter::Instance().SyncUi(
            floatingToolbarState.cn_en, floatingToolbarState.double_single_byte, floatingToolbarState.punctuation,
            floatingToolbarState.english_input_mode, floatingToolbarState.caps_lock,
            floatingToolbarState.japanese_input_mode);
    }
}
} // namespace

bool EnsureSmallWindowsTopmost(const wchar_t *reason)
{
    smallWindowTopmostRequested = true;
    if (smallWindowTopmostApplied)
    {
        return true;
    }
    if (!AreSmallWindowPresentersBound())
    {
        return false;
    }
    TryApplyPendingLazyTopmost(reason);
    return smallWindowTopmostApplied;
}

bool AreSmallWindowsTopmostApplied()
{
    return smallWindowTopmostApplied;
}

void RaiseTrayMenuAboveSmallWindows(const wchar_t * /*reason*/)
{
    if (!::global_hwnd_menu || !TrayMenuPresenter::Instance().IsBound())
    {
        return;
    }
    // A second HWND_TOPMOST is what actually lifts the menu within the topmost
    // band; HWND_TOP alone is unreliable with WS_EX_NOACTIVATE layered hosts.
    constexpr UINT flag = SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE;
    SetWindowPos(::global_hwnd_menu, HWND_TOPMOST, 0, 0, 0, 0, flag);
    if (TrayMenuPresenter::Instance().IsOpenToUser())
    {
        TrayMenuPresenter::Instance().Present();
    }
}

bool IsTrayMenuOpenToUser()
{
    return TrayMenuPresenter::Instance().IsBound() && TrayMenuPresenter::Instance().IsOpenToUser();
}

void ApplyConfiguredUiThemes()
{
    // 工具栏跟着候选窗皮肤走（外部皮肤还可能只声明了部分布局），是常驻窗口，要立刻重新套用。
    if (FloatingToolbarPresenter::Instance().IsBound())
    {
        FloatingToolbarPresenter::Instance().ApplyTheme();
    }
}

void ForceReloadConfiguredCandidateSkin()
{
    ++candidateSkinReloadRevision;
    ApplyConfiguredUiThemes();
}

uint64_t GetCandidateSkinReloadRevision()
{
    return candidateSkinReloadRevision;
}

/**
 * @brief 更新 floating toolbar 的中英文切换状态和标点切换状态
 *
 * @param cnEnState 1: 中文, 0: 英文
 * @param puncState 1: 中文标点, 0: 英文标点
 */
void UpdateFtbCnEnAndPuncState(int cnEnState, int puncState)
{
    bool changed = UpdateBinaryState(cnEnState, floatingToolbarState.cn_en);
    changed |= UpdateBinaryState(puncState, floatingToolbarState.punctuation);
    if (changed)
    {
        RenderFloatingToolbarState();
        RevealAutoHiddenFloatingToolbar(L"input-state-changed");
    }
}

/**
 * @brief 更新 floating toolbar 的中英文、全半角、标点与大写锁定状态
 *
 * @param cnEnState 1: 中文, 0: 英文
 * @param doubleSingleByteState 1: 全角, 0: 半角
 * @param puncState 1: 中文标点, 0: 英文标点
 */
void UpdateFtbCnEnAndDoubleSingleAndPuncState(int cnEnState, int doubleSingleByteState, int puncState,
                                              int capsLockState)
{
    bool changed = UpdateBinaryState(cnEnState, floatingToolbarState.cn_en);
    changed |= UpdateBinaryState(doubleSingleByteState, floatingToolbarState.double_single_byte);
    changed |= UpdateBinaryState(puncState, floatingToolbarState.punctuation);
    changed |= UpdateBinaryState(capsLockState, floatingToolbarState.caps_lock);
    if (changed)
    {
        RenderFloatingToolbarState();
        RevealAutoHiddenFloatingToolbar(L"input-state-changed");
    }
}

/**
 * @brief 更新 floating toolbar 的标点切换状态
 *
 * @param puncState 1: 中文标点, 0: 英文标点
 */
void UpdateFtbPuncState(int puncState)
{
    if (UpdateBinaryState(puncState, floatingToolbarState.punctuation))
    {
        RenderFloatingToolbarState();
        RevealAutoHiddenFloatingToolbar(L"input-state-changed");
    }
}

/**
 * @brief 更新 floating toolbar 的全角和半角状态
 *
 * @param doubleSingleByteState 0: 半角, 1: 全角
 */
void UpdateFtbDoubleSingleByteState(int doubleSingleByteState)
{
    if (UpdateBinaryState(doubleSingleByteState, floatingToolbarState.double_single_byte))
    {
        RenderFloatingToolbarState();
        RevealAutoHiddenFloatingToolbar(L"input-state-changed");
    }
}

void UpdateFtbEnglishInputModeState(int enabled)
{
    if (UpdateBinaryState(enabled, floatingToolbarState.english_input_mode))
    {
        RenderFloatingToolbarState();
        RevealAutoHiddenFloatingToolbar(L"input-state-changed");
    }
}

void UpdateFtbCapsLockState(int enabled)
{
    if (UpdateBinaryState(enabled, floatingToolbarState.caps_lock))
    {
        RenderFloatingToolbarState();
        RevealAutoHiddenFloatingToolbar(L"input-state-changed");
    }
}

void UpdateFtbCharacterSetState()
{
    RenderFloatingToolbarState();
    RevealAutoHiddenFloatingToolbar(L"input-state-changed");
}

void UpdateFtbInputModeState(int japaneseMode)
{
    if (japaneseMode != 0 && japaneseMode != 1)
    {
        return;
    }
    const bool changed = floatingToolbarState.japanese_input_mode != japaneseMode;
    floatingToolbarState.japanese_input_mode = japaneseMode;
    RenderFloatingToolbarState();
    if (changed)
    {
        RevealAutoHiddenFloatingToolbar(L"input-state-changed");
    }
}

void ApplyConfiguredFloatingToolbarItems()
{
    RenderFloatingToolbarState();
    ApplyConfiguredFloatingToolbarSize();
}

void SyncMenuFloatingToolbarToggle()
{
    if (!TrayMenuPresenter::Instance().IsBound())
    {
        return;
    }
    if (TrayMenuPresenter::Instance().IsOpenToUser())
    {
        TrayMenuPresenter::Instance().ShowFromLangBar();
    }
    else
    {
        TrayMenuPresenter::Instance().ApplyTheme();
    }
}
