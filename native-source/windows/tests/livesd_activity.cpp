#include <algorithm>
#include <array>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <functional>
#include <iostream>
#include <new>
#include <optional>
#include <vector>
#include "../../engine/contracts/windows_ipc.h"
#include "../src/Key/LiveSDKeyObservationGuard.h"

using UINT = unsigned;
using LONG = long;
using DWORD = std::uint32_t;
using WPARAM = std::uintptr_t;
using LPARAM = std::intptr_t;
using ULONG_PTR = std::uintptr_t;
using HWND = void *;
using BOOL = int;
using HRESULT = long;
struct POINT
{
    LONG x;
    LONG y;
};
struct INPUT_MESSAGE_SOURCE
{
    int originId = 0;
};
struct TF_STATUS
{
    DWORD dwDynamicFlags = 0;
};
constexpr BOOL FALSE = 0;
constexpr HRESULT S_OK = 0, E_FAIL = -1, TF_S_ASYNC = 1;
constexpr DWORD TF_SD_READONLY = 1, TF_ES_ASYNCDONTCARE = 1, TF_ES_READ = 2;
constexpr UINT VK_CONTROL = 0x11, VK_MENU = 0x12, VK_SHIFT = 0x10, VK_LWIN = 0x5b, VK_RWIN = 0x5c;
constexpr UINT VK_NUMPAD0 = 0x60, VK_DIVIDE = 0x6f, VK_SPACE = 0x20, VK_RETURN = 0x0d, VK_BACK = 0x08;
constexpr UINT VK_OEM_1 = 0xba, VK_OEM_3 = 0xc0, VK_OEM_4 = 0xdb, VK_OEM_8 = 0xdf;
constexpr UINT VK_OEM_102 = 0xe2, VK_PACKET = 0xe7, TIMER_LIVESD_ACTIVITY = 10;
constexpr UINT ActivityDelayMs = 16;
constexpr unsigned MaxPendingPulses = 32, MaxPendingEnterActions = 32, MaxPendingPinRequests = 32;
bool FAILED(HRESULT value)
{
    return value < 0;
}
bool SUCCEEDED(HRESULT value)
{
    return value >= 0;
}

namespace Global
{
bool g_connected = true;
bool IsUiLessMode()
{
    return false;
}
} // namespace Global
class CMetasequoiaIME;
bool ctrl = false, shift = false, ownsFocus = true;
LONG messageTime = 1;
unsigned timerSets = 0;
std::vector<UINT> events;
bool SupportsLiveSDActivity()
{
    return true;
}
bool comboSupported = false;
bool SupportsLiveSDCombo()
{
    return comboSupported;
}
bool IsNamedpipeFocusStateOwner(CMetasequoiaIME *)
{
    return ownsFocus;
}
bool IsSelfGeneratedSendInputExtraInfo(ULONG_PTR)
{
    return false;
}
ULONG_PTR GetMessageExtraInfo()
{
    return 0;
}
bool GetCurrentInputMessageSource(INPUT_MESSAGE_SOURCE *)
{
    return false;
}
constexpr int IMO_INJECTED = 1;
int GetKeyState(UINT key)
{
    return ((key == VK_CONTROL && ctrl) || (key == VK_SHIFT && shift)) ? 0x8000 : 0;
}
int GetAsyncKeyState(UINT)
{
    return 0;
}
LONG GetMessageTime()
{
    return messageTime;
}
bool KillTimer(HWND, UINT)
{
    return true;
}
unsigned SetTimer(HWND, UINT, UINT, void *)
{
    return ++timerSets;
}
bool SendLiveSDActivityToUIProcess(UINT kind, POINT, HWND, UINT = 0)
{
    events.push_back(kind);
    return true;
}
struct CLiveSDCaretEditSession
{
    template <typename... Args> explicit CLiveSDCaretEditSession(Args...)
    {
    }
    void Release()
    {
        delete this;
    }
};
struct ITfContext
{
    unsigned reads = 0;
    HRESULT GetStatus(TF_STATUS *)
    {
        return S_OK;
    }
    HRESULT RequestEditSession(unsigned, CLiveSDCaretEditSession *, DWORD, HRESULT *result)
    {
        ++reads;
        *result = TF_S_ASYNC;
        return S_OK;
    }
    void Release()
    {
    }
};
struct ITfDocumentMgr
{
    ITfContext context;
    HRESULT GetTop(ITfContext **result)
    {
        *result = &context;
        return S_OK;
    }
    void Release()
    {
    }
};
struct ITfThreadMgr
{
    ITfDocumentMgr document;
    std::function<void()> onThreadFocus;
    HRESULT IsThreadFocus(BOOL *result)
    {
        auto callback = onThreadFocus;
        onThreadFocus = nullptr;
        if (callback)
            callback();
        *result = 1;
        return S_OK;
    }
    HRESULT GetFocus(ITfDocumentMgr **result)
    {
        *result = &document;
        return S_OK;
    }
};
class CMetasequoiaIME
{
  public:
    void _ObserveLiveSDKey(ITfContext *, WPARAM, LPARAM, bool = false, std::optional<LONG> = std::nullopt);
    void _ObserveLiveSDTestResult(ITfContext *, WPARAM, LPARAM, bool, LONG);
    void _QueueLiveSDActivity(bool = false, DWORD = 0, bool = false, bool = false, bool = true);
    void _RunLiveSDActivity();
    void _CompleteLiveSDActivity(ITfContext *, std::uint64_t, std::uint64_t, std::uint64_t, unsigned, unsigned, bool,
                                 const std::array<std::uint32_t, 32> &, bool, POINT, HWND);
    void _CancelLiveSDActivity(bool, bool = false);
    void _OnLiveSDThreadFocusLost();
    bool _IsKeyboardDisabled() const
    {
        return false;
    }
    std::uint64_t _CaptureFocusSessionToken() const
    {
        return 1;
    }
    bool _IsFocusSessionCurrent(std::uint64_t, ITfContext * = nullptr) const
    {
        return ownsFocus;
    }
    HWND _msgWndHandle = reinterpret_cast<HWND>(1);
    ITfThreadMgr manager;
    ITfThreadMgr *_pThreadMgr = &manager;
    unsigned _tfClientId = 1;
    std::uint64_t _deferredKeyFocusGeneration = 1, _liveSDSerial = 1;
    bool _liveSDTimerArmed = false, _liveSDReadPending = false;
    unsigned _liveSDPulses = 0, _liveSDEnterActions = 0, _liveSDPinRequests = 0;
    bool _liveSDFocusRequested = false;
    std::array<std::uint32_t, 32> _liveSDPulseIntervals{};
    bool _liveSDHasTypingMessageTime = false;
    DWORD _liveSDLastTypingMessageTime = 0;
    unsigned _liveSDCaretRetries = 0;
    LiveSDKeyObservationGuard _liveSDKeyObservation;
};

#include "livesd_activity_source.h"

int main()
{
    ITfContext context;
    CMetasequoiaIME service;
    ctrl = true;
    service._ObserveLiveSDKey(&context, 'Z', 1);
    service._ObserveLiveSDKey(&context, 'Z', 1); // repeated TestKeyDown query
    ++messageTime;
    service._ObserveLiveSDKey(&context, 'Z', 1 | (static_cast<LPARAM>(1) << 30)); // OS auto-repeat
    assert(service._liveSDPinRequests == 1);
    assert(!service._liveSDFocusRequested);
    service._RunLiveSDActivity();
    assert(events == std::vector<UINT>{FanyImeLiveSDActivity::PinPosition});
    assert(service.manager.document.context.reads == 0);
    assert(!service._liveSDReadPending);

    // Ctrl+Shift+Z and Ctrl+Y belong to the application and must not toggle the pin.
    ++messageTime;
    shift = true;
    service._ObserveLiveSDKey(&context, 'Z', 1);
    shift = false;
    ++messageTime;
    service._ObserveLiveSDKey(&context, 'Y', 1);
    assert(service._liveSDPinRequests == 0);

    // A host delaying a previous caret lock cannot hold up the pin request.
    service._QueueLiveSDActivity(true, 100);
    service._RunLiveSDActivity();
    assert(service._liveSDReadPending);
    assert(service.manager.document.context.reads == 1);
    ++messageTime;
    service._ObserveLiveSDKey(&context, 'Z', 1);
    assert(service._liveSDTimerArmed);
    service._RunLiveSDActivity();
    assert(events.size() == 2 && events.back() == FanyImeLiveSDActivity::PinPosition);
    assert(service._liveSDReadPending && service.manager.document.context.reads == 1);
    const auto intervals = service._liveSDPulseIntervals;
    service._CompleteLiveSDActivity(&context, 1, 1, 1, 1, 0, true, intervals, false,
                                    POINT{0, FanyImeLiveSDActivity::InvalidCaretY}, nullptr);
    assert(events.size() == 3 && events.back() == FanyImeLiveSDActivity::TypingPulse);
    service._RunLiveSDActivity();
    service._CompleteLiveSDActivity(&context, 1, 1, 1, 0, 0, true, intervals, false,
                                    POINT{0, FanyImeLiveSDActivity::InvalidCaretY}, nullptr);
    assert(std::count(events.begin(), events.end(), FanyImeLiveSDActivity::PinPosition) == 2);

    // A pending request is discarded on focus loss rather than applying in the next app.
    ++messageTime;
    service._ObserveLiveSDKey(&context, 'Z', 1);
    service._CancelLiveSDActivity(false);
    service._RunLiveSDActivity();
    assert(service._liveSDPinRequests == 0);
    assert(std::count(events.begin(), events.end(), FanyImeLiveSDActivity::PinPosition) == 2);

    // ITfThreadFocusSink can report a transient auxiliary-UI transition.  Its
    // cancellation must clear queued English activity without manufacturing a
    // Blur, so the existing host can survive until thread focus returns.
    const auto eventsBeforeTransientThreadFocus = events.size();
    service._QueueLiveSDActivity(true, 200);
    service._OnLiveSDThreadFocusLost();
    assert(events.size() == eventsBeforeTransientThreadFocus);
    assert(service._liveSDPulses == 0 && !service._liveSDTimerArmed);

    // A focus callback during the COM query can replace both the session and
    // its queued pin. The old timer cannot consume the new session's request.
    service._QueueLiveSDActivity(false, 0, false, true, false);
    service.manager.onThreadFocus = [&service]() {
        service._CancelLiveSDActivity(false);
        ++service._deferredKeyFocusGeneration;
        service._QueueLiveSDActivity(false, 0, false, true, false);
    };
    service._RunLiveSDActivity();
    assert(service._liveSDPinRequests == 1 && service._liveSDTimerArmed);
    assert(std::count(events.begin(), events.end(), FanyImeLiveSDActivity::PinPosition) == 2);
    service._RunLiveSDActivity();
    assert(service._liveSDPinRequests == 0);
    assert(std::count(events.begin(), events.end(), FanyImeLiveSDActivity::PinPosition) == 3);
    // The new decoration stream is sent before asynchronous caret work, in
    // observed order. Test/actual duplicate callbacks must count only once.
    CMetasequoiaIME combo;
    events.clear();
    ctrl = false;
    comboSupported = true;
    const auto key = [&](UINT value, LPARAM data = 1) {
        ++messageTime;
        combo._ObserveLiveSDKey(&context, value, data);
    };
    key('A');
    combo._ObserveLiveSDKey(&context, 'A', 1, true); // same real key, actual callback
    key(VK_RETURN);
    key('B');
    assert((events == std::vector<UINT>{FanyImeLiveSDActivity::ComboTyping, FanyImeLiveSDActivity::ComboReset,
                                        FanyImeLiveSDActivity::ComboTyping}));
    // A caret read is still not required to count/reset, and Enter remains an
    // independent queued aerial action rather than another typing increment.
    assert(combo.manager.document.context.reads == 0);
    assert(combo._liveSDPulses == 2 && combo._liveSDEnterActions == 1);
    key(VK_BACK);
    key('C');
    key(VK_BACK); // reset also applies when the host has no text to delete
    assert(events[3] == FanyImeLiveSDActivity::ComboReset && events[4] == FanyImeLiveSDActivity::ComboTyping &&
           events[5] == FanyImeLiveSDActivity::ComboReset);
    key('C', 1 | (static_cast<LPARAM>(1) << 30)); // a distinct auto-repeat
    assert(events.back() == FanyImeLiveSDActivity::ComboTyping && events.size() == 7);
    ctrl = true;
    key('C'); // Ctrl+C is an application shortcut
    assert(events.size() == 7);
    key(VK_BACK); // modified Backspace still clears, without altering input
    assert(events.back() == FanyImeLiveSDActivity::ComboReset && events.size() == 8);
    ctrl = false;
    const auto beforeUnsupported = events.size();
    comboSupported = false;
    key('D');
    key(VK_RETURN);
    key(VK_BACK);
    assert(events.size() == beforeUnsupported); // old peers receive no new kind
    comboSupported = true;
    ownsFocus = false;
    key('E');
    key(VK_BACK);
    assert(events.size() == beforeUnsupported); // stale owners cannot count
    ownsFocus = true;

    {
        CMetasequoiaIME ownedKey;
        events.clear();
        for (int index = 0; index < 3; ++index)
        {
            messageTime = 80 + index;
            ownedKey._ObserveLiveSDTestResult(&context, 'A', 1 + index, true, messageTime);
        }
        assert(events.empty() && ownedKey._liveSDPulses == 0);
        messageTime = 90;
        ownedKey._ObserveLiveSDKey(&context, 'A', 0, true);
        assert(events == std::vector<UINT>{FanyImeLiveSDActivity::ComboTyping});
        assert(ownedKey._liveSDPulses == 1);
        messageTime = 91;
        ownedKey._ObserveLiveSDTestResult(&context, 'B', 1, false, 91);
        messageTime = 92; // classification pumped a different message
        ownedKey._ObserveLiveSDTestResult(&context, 'B', 1, false, 91);
        assert(events.size() == 2 && ownedKey._liveSDPulses == 2);
    }
    // Compile the production observer against host callback sequences. The
    // Test/Actual pair can cross a message pump, cancellation or another key.
    {
        CMetasequoiaIME changedMessage;
        events.clear();
        messageTime = 100;
        changedMessage._ObserveLiveSDKey(&context, 'A', 0x001e0001);
        messageTime = 101;
        changedMessage._ObserveLiveSDKey(&context, 'A', 1, true);
        assert(events == std::vector<UINT>{FanyImeLiveSDActivity::ComboTyping});
        assert(changedMessage._liveSDPulses == 1);
    }
    {
        CMetasequoiaIME cancelled;
        events.clear();
        messageTime = 200;
        cancelled._ObserveLiveSDKey(&context, 'A', 1);
        cancelled._OnLiveSDThreadFocusLost();
        messageTime = 201;
        cancelled._ObserveLiveSDKey(&context, 'A', 1, true);
        assert(events == std::vector<UINT>{FanyImeLiveSDActivity::ComboTyping});
        assert(cancelled._liveSDPulses == 0); // cancelled motion stays cancelled
    }
    {
        CMetasequoiaIME interleaved;
        events.clear();
        messageTime = 300;
        interleaved._ObserveLiveSDKey(&context, 'A', 1);
        ++messageTime;
        interleaved._ObserveLiveSDKey(&context, 'B', 1);
        ++messageTime;
        interleaved._ObserveLiveSDKey(&context, 'A', 0, true);
        ++messageTime;
        interleaved._ObserveLiveSDKey(&context, 'B', 0, true);
        assert((events == std::vector<UINT>{FanyImeLiveSDActivity::ComboTyping, FanyImeLiveSDActivity::ComboTyping}));
        assert(interleaved._liveSDPulses == 2);
    }
    {
        CMetasequoiaIME repeats;
        events.clear();
        const LPARAM repeat = 1 | (static_cast<LPARAM>(1) << 30);
        for (int index = 0; index < 3; ++index)
        {
            messageTime = 400 + index;
            repeats._ObserveLiveSDKey(&context, 'A', index ? repeat : 1);
        }
        for (int index = 0; index < 3; ++index)
        {
            messageTime = 410 + index;
            repeats._ObserveLiveSDKey(&context, 'A', 0, true);
        }
        assert(events.size() == 3 && repeats._liveSDPulses == 3);
        repeats._liveSDKeyObservation.KeyUp('A');
        messageTime = 400; // a fresh press can share the earlier tick and data
        repeats._ObserveLiveSDKey(&context, 'A', 1);
        repeats._ObserveLiveSDKey(&context, 'A', 0, true);
        assert(events.size() == 4 && repeats._liveSDPulses == 4);
    }
    {
        CMetasequoiaIME resetPair;
        events.clear();
        messageTime = 500;
        resetPair._ObserveLiveSDKey(&context, 'A', 1);
        ++messageTime;
        resetPair._ObserveLiveSDKey(&context, VK_RETURN, 1);
        ++messageTime;
        resetPair._ObserveLiveSDKey(&context, VK_RETURN, 0, true);
        ++messageTime;
        resetPair._ObserveLiveSDKey(&context, 'B', 1);
        ++messageTime;
        resetPair._ObserveLiveSDKey(&context, 'B', 0, true);
        assert((events == std::vector<UINT>{FanyImeLiveSDActivity::ComboTyping, FanyImeLiveSDActivity::ComboReset,
                                            FanyImeLiveSDActivity::ComboTyping}));
        assert(resetPair._liveSDPulses == 2 && resetPair._liveSDEnterActions == 1);
    }
    std::cout
        << "LiveSD pin, ordered combo, phase pairing, changed message metadata, cancellation, interleaving, repeat, "
           "modifiers and caret retry checks passed\n";
}
