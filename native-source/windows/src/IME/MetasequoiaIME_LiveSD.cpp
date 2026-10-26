#include "Private.h"
#include "Globals.h"
#include "MetasequoiaIME.h"
#include "TfTextLayoutSink.h"
#include "Ipc.h"
#include "FanyDefines.h"
#include <algorithm>

namespace
{
constexpr UINT ActivityDelayMs = 16;
constexpr unsigned MaxPendingPulses = 32;
constexpr unsigned MaxPendingEnterActions = 32;

bool ResolveNativeCaretAnchor(POINT *anchor, HWND *contextRootWindow)
{
    if (!anchor)
        return false;
    GUITHREADINFO threadInfo{sizeof(threadInfo)};
    if (!GetGUIThreadInfo(GetCurrentThreadId(), &threadInfo) || !threadInfo.hwndCaret ||
        threadInfo.rcCaret.bottom <= threadInfo.rcCaret.top)
        return false;

    // GUITHREADINFO reports rcCaret in client coordinates of hwndCaret.
    POINT point{threadInfo.rcCaret.left, threadInfo.rcCaret.bottom};
    if (!ClientToScreen(threadInfo.hwndCaret, &point))
        return false;
    (void)LogicalToPhysicalPointForPerMonitorDPI(threadInfo.hwndCaret, &point);
    *anchor = point;

    if (contextRootWindow && !*contextRootWindow)
    {
        HWND focusWindow = threadInfo.hwndFocus ? threadInfo.hwndFocus : threadInfo.hwndCaret;
        HWND root = GetAncestor(focusWindow, GA_ROOTOWNER);
        *contextRootWindow = root ? root : focusWindow;
    }
    return true;
}

class CLiveSDCaretEditSession : public CEditSessionBase
{
  public:
    CLiveSDCaretEditSession(CMetasequoiaIME *service, ITfContext *context, uint64_t focusToken, uint64_t generation,
                            uint64_t serial, unsigned pulses, unsigned enterActions,
                            const std::array<std::uint32_t, MaxPendingPulses> &intervals)
        : CEditSessionBase(service, context), focusToken_(focusToken), generation_(generation), serial_(serial),
          pulses_(pulses), enterActions_(enterActions), intervals_(intervals)
    {
    }

    STDMETHODIMP DoEditSession(TfEditCookie ec) override
    {
        POINT caret{0, FanyImeLiveSDActivity::InvalidCaretY};
        HWND contextRootWindow = nullptr;
        // A host can defer this grant beyond a document/context or TIP switch.
        const bool current = _pTextService->_IsFocusSessionCurrent(focusToken_, _pContext);
        bool resolved = current && ResolveCollapsedSelectionAnchor(_pContext, ec, &caret, &contextRootWindow);
        // Some application-owned editors grant the read lock but do not expose
        // a collapsed TSF extent. Their native caret remains available through
        // the owner thread's GUI state and gives LiveSD an app-bound fallback.
        if (!resolved && current)
            resolved = ResolveNativeCaretAnchor(&caret, &contextRootWindow);
        _pTextService->_CompleteLiveSDActivity(_pContext, focusToken_, generation_, serial_, pulses_, enterActions_,
                                               intervals_, resolved, caret, contextRootWindow);
        return S_OK;
    }

  private:
    uint64_t focusToken_;
    uint64_t generation_;
    uint64_t serial_;
    unsigned pulses_;
    unsigned enterActions_;
    std::array<std::uint32_t, MaxPendingPulses> intervals_{};
};

bool IsTypingKey(UINT key)
{
    return (key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9') || (key >= VK_NUMPAD0 && key <= VK_DIVIDE) ||
           key == VK_SPACE || (key >= VK_OEM_1 && key <= VK_OEM_3) || (key >= VK_OEM_4 && key <= VK_OEM_8) ||
           key == VK_OEM_102 || key == VK_PACKET;
}
} // namespace

void CMetasequoiaIME::_ObserveLiveSDKey(ITfContext *context, WPARAM key, LPARAM keyData)
{
    if (!context || !SupportsLiveSDActivity() || !IsNamedpipeFocusStateOwner(this) ||
        IsSelfGeneratedSendInputExtraInfo(static_cast<ULONG_PTR>(GetMessageExtraInfo())))
        return;
    INPUT_MESSAGE_SOURCE source{};
    if (GetCurrentInputMessageSource(&source) && source.originId == IMO_INJECTED)
        return;
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    if (ctrl || alt || (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0 || (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0 ||
        _IsKeyboardDisabled())
        return;
    const UINT virtualKey = static_cast<UINT>(key);
    const LONG messageTime = GetMessageTime();
    // A TestKeyDown query may repeat; the real key is counted only here, never
    // again by OnKeyDown or a replay of the IME-owned deferred queue.
    if (_liveSDLastKey == virtualKey && _liveSDLastKeyTime == messageTime && _liveSDLastKeyData == keyData)
        return;
    _liveSDLastKey = virtualKey;
    _liveSDLastKeyTime = messageTime;
    _liveSDLastKeyData = keyData;
    _QueueLiveSDActivity(IsTypingKey(virtualKey), static_cast<DWORD>(messageTime), virtualKey == VK_RETURN);
}

void CMetasequoiaIME::_QueueLiveSDActivity(bool typingPulse, DWORD typingMessageTime, bool enterAction)
{
    if (!_msgWndHandle || !IsNamedpipeFocusStateOwner(this) || !Global::g_connected || !SupportsLiveSDActivity() ||
        Global::IsUiLessMode())
        return;
    if (typingPulse)
    {
        if (_liveSDPulses < MaxPendingPulses)
        {
            const std::uint32_t intervalMs =
                _liveSDHasTypingMessageTime ? typingMessageTime - _liveSDLastTypingMessageTime : 0;
            _liveSDPulseIntervals[_liveSDPulses] =
                (std::min)(intervalMs, FanyImeLiveSDActivity::MaximumTypingIntervalMs);
            ++_liveSDPulses;
        }
        _liveSDLastTypingMessageTime = typingMessageTime;
        _liveSDHasTypingMessageTime = true;
    }
    if (enterAction)
        _liveSDEnterActions = (std::min)(_liveSDEnterActions + 1U, MaxPendingEnterActions);
    if (!_liveSDTimerArmed && !_liveSDReadPending)
    {
        // WM_TIMER runs after the host's queued key and paint messages, so
        // ordinary English gets the caret after the application inserted it.
        _liveSDTimerArmed = SetTimer(_msgWndHandle, TIMER_LIVESD_ACTIVITY, ActivityDelayMs, nullptr) != 0;
    }
}

void CMetasequoiaIME::_RunLiveSDActivity()
{
    KillTimer(_msgWndHandle, TIMER_LIVESD_ACTIVITY);
    _liveSDTimerArmed = false;
    if (_liveSDReadPending || !_pThreadMgr || !SupportsLiveSDActivity() || !IsNamedpipeFocusStateOwner(this))
        return;
    BOOL hasThreadFocus = FALSE;
    if (FAILED(_pThreadMgr->IsThreadFocus(&hasThreadFocus)) || !hasThreadFocus || _IsKeyboardDisabled())
    {
        _CancelLiveSDActivity(true);
        return;
    }
    const uint64_t focusToken = _CaptureFocusSessionToken();
    if (!_IsFocusSessionCurrent(focusToken))
    {
        _liveSDPulses = 0;
        _liveSDEnterActions = 0;
        _liveSDPulseIntervals.fill(0);
        _liveSDHasTypingMessageTime = false;
        return;
    }
    ITfDocumentMgr *document = nullptr;
    ITfContext *context = nullptr;
    if (SUCCEEDED(_pThreadMgr->GetFocus(&document)) && document)
    {
        (void)document->GetTop(&context);
        document->Release();
    }
    if (!context)
    {
        _CancelLiveSDActivity(true);
        return;
    }
    TF_STATUS status{};
    if (FAILED(context->GetStatus(&status)) || (status.dwDynamicFlags & TF_SD_READONLY) != 0)
    {
        context->Release();
        _CancelLiveSDActivity(true);
        return;
    }
    const unsigned pulses = _liveSDPulses;
    const unsigned enterActions = _liveSDEnterActions;
    const auto intervals = _liveSDPulseIntervals;
    const uint64_t serial = _liveSDSerial;
    auto *session = new (std::nothrow) CLiveSDCaretEditSession(this, context, focusToken, _deferredKeyFocusGeneration,
                                                               serial, pulses, enterActions, intervals);
    if (session)
    {
        _liveSDReadPending = true;
        HRESULT sessionResult = E_FAIL;
        const HRESULT requested =
            context->RequestEditSession(_tfClientId, session, TF_ES_ASYNCDONTCARE | TF_ES_READ, &sessionResult);
        session->Release();
        if (serial == _liveSDSerial && _liveSDReadPending && (requested != S_OK || sessionResult != TF_S_ASYNC))
        {
            _CompleteLiveSDActivity(context, focusToken, _deferredKeyFocusGeneration, serial, pulses, enterActions,
                                    intervals, false, POINT{0, FanyImeLiveSDActivity::InvalidCaretY}, nullptr);
        }
    }
    context->Release();
}

void CMetasequoiaIME::_CompleteLiveSDActivity(ITfContext *context, uint64_t focusToken, uint64_t focusGeneration,
                                              uint64_t serial, unsigned pulses, unsigned enterActions,
                                              const std::array<std::uint32_t, MaxPendingPulses> &intervals,
                                              bool resolved, POINT caret, HWND contextRootWindow)
{
    if (serial != _liveSDSerial)
        return;
    _liveSDReadPending = false;
    BOOL hasThreadFocus = FALSE;
    if (focusGeneration != _deferredKeyFocusGeneration || !IsNamedpipeFocusStateOwner(this) ||
        !_IsFocusSessionCurrent(focusToken, context) || !_pThreadMgr ||
        FAILED(_pThreadMgr->IsThreadFocus(&hasThreadFocus)) || !hasThreadFocus)
    {
        _liveSDPulses = 0;
        _liveSDEnterActions = 0;
        _liveSDPulseIntervals.fill(0);
        _liveSDHasTypingMessageTime = false;
        return;
    }
    // The COM focus queries above may re-enter a focus callback.
    if (serial != _liveSDSerial || focusGeneration != _deferredKeyFocusGeneration)
        return;
    if (resolved)
    {
        _liveSDCaretRetries = 0;
        const unsigned consumed = (std::min)(_liveSDPulses, pulses);
        for (unsigned index = consumed; index < _liveSDPulses; ++index)
            _liveSDPulseIntervals[index - consumed] = _liveSDPulseIntervals[index];
        _liveSDPulses -= consumed;
        const unsigned consumedEnterActions = (std::min)(_liveSDEnterActions, enterActions);
        _liveSDEnterActions -= consumedEnterActions;
        SendLiveSDActivityToUIProcess(FanyImeLiveSDActivity::Focus, caret, contextRootWindow);
        for (unsigned i = 0; i < pulses; ++i)
            SendLiveSDActivityToUIProcess(FanyImeLiveSDActivity::TypingPulse, caret, contextRootWindow, intervals[i]);
        for (unsigned i = 0; i < consumedEnterActions; ++i)
            SendLiveSDActivityToUIProcess(FanyImeLiveSDActivity::EnterAction, caret, contextRootWindow);
    }
    else
    {
        // A pulse remains useful when this read session cannot resolve a caret:
        // the Server may already have a candidate-anchored host to animate.
        // The invalid point tells it not to create or reposition a host. Focus
        // still requires a real caret and is retried below.
        const unsigned consumed = (std::min)(_liveSDPulses, pulses);
        for (unsigned index = consumed; index < _liveSDPulses; ++index)
            _liveSDPulseIntervals[index - consumed] = _liveSDPulseIntervals[index];
        _liveSDPulses -= consumed;
        const unsigned consumedEnterActions = (std::min)(_liveSDEnterActions, enterActions);
        _liveSDEnterActions -= consumedEnterActions;
        for (unsigned i = 0; i < pulses; ++i)
            SendLiveSDActivityToUIProcess(FanyImeLiveSDActivity::TypingPulse,
                                          POINT{0, FanyImeLiveSDActivity::InvalidCaretY}, nullptr, intervals[i]);
        for (unsigned i = 0; i < consumedEnterActions; ++i)
            SendLiveSDActivityToUIProcess(FanyImeLiveSDActivity::EnterAction,
                                          POINT{0, FanyImeLiveSDActivity::InvalidCaretY}, nullptr);

        if (_liveSDCaretRetries++ < 2)
        {
            // Transient TS_E_NOLAYOUT is normal in Chromium; never substitute a
            // fabricated/default position or a point from a previous context.
            _QueueLiveSDActivity();
        }
    }
    if (_liveSDPulses || _liveSDEnterActions)
        _QueueLiveSDActivity();
}

void CMetasequoiaIME::_CancelLiveSDActivity(bool sendBlur, bool keepPulses)
{
    if (_msgWndHandle)
        KillTimer(_msgWndHandle, TIMER_LIVESD_ACTIVITY);
    if (++_liveSDSerial == 0)
        ++_liveSDSerial;
    _liveSDTimerArmed = false;
    _liveSDReadPending = false;
    if (!keepPulses)
        _liveSDPulses = 0;
    if (!keepPulses)
        _liveSDEnterActions = 0;
    if (!keepPulses)
    {
        _liveSDPulseIntervals.fill(0);
        _liveSDHasTypingMessageTime = false;
    }
    _liveSDCaretRetries = 0;
    if (!keepPulses)
        _liveSDLastKey = 0;
    if (sendBlur && IsNamedpipeFocusStateOwner(this))
        SendLiveSDActivityToUIProcess(FanyImeLiveSDActivity::Blur, POINT{0, FanyImeLiveSDActivity::InvalidCaretY},
                                      nullptr);
}
