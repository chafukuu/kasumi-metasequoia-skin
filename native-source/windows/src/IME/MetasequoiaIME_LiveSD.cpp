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
constexpr unsigned MaxPendingPinRequests = 32;

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
                            uint64_t serial, unsigned pulses, unsigned enterActions, bool focusRequested,
                            const std::array<std::uint32_t, MaxPendingPulses> &intervals)
        : CEditSessionBase(service, context), focusToken_(focusToken), generation_(generation), serial_(serial),
          pulses_(pulses), enterActions_(enterActions), focusRequested_(focusRequested), intervals_(intervals)
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
                                               focusRequested_, intervals_, resolved, caret, contextRootWindow);
        return S_OK;
    }

  private:
    uint64_t focusToken_;
    uint64_t generation_;
    uint64_t serial_;
    unsigned pulses_;
    unsigned enterActions_;
    bool focusRequested_;
    std::array<std::uint32_t, MaxPendingPulses> intervals_{};
};

bool IsTypingKey(UINT key)
{
    return (key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9') || (key >= VK_NUMPAD0 && key <= VK_DIVIDE) ||
           key == VK_SPACE || (key >= VK_OEM_1 && key <= VK_OEM_3) || (key >= VK_OEM_4 && key <= VK_OEM_8) ||
           key == VK_OEM_102 || key == VK_PACKET;
}
} // namespace

void CMetasequoiaIME::_ObserveLiveSDTestResult(ITfContext *context, WPARAM key, LPARAM keyData, bool eaten,
                                               LONG messageTime)
{
    // An owned key will reach OnKeyDown. A preflight query may run repeatedly
    // with different message metadata and must not count before that action.
    // Uneaten keys have no actual callback, so observe their completed test.
    if (!eaten)
        _ObserveLiveSDKey(context, key, keyData, false, messageTime);
}

void CMetasequoiaIME::_ObserveLiveSDKey(ITfContext *context, WPARAM key, LPARAM keyData, bool actualKeyDown,
                                        std::optional<LONG> observedMessageTime)
{
    if (!context || !SupportsLiveSDActivity() || !IsNamedpipeFocusStateOwner(this) ||
        IsSelfGeneratedSendInputExtraInfo(static_cast<ULONG_PTR>(GetMessageExtraInfo())))
        return;
    INPUT_MESSAGE_SOURCE source{};
    if (GetCurrentInputMessageSource(&source) && source.originId == IMO_INJECTED)
        return;
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    const bool win = (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0 || (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;
    if (alt || win || _IsKeyboardDisabled())
        return;
    const UINT virtualKey = static_cast<UINT>(key);
    const LONG messageTime = observedMessageTime ? *observedMessageTime : GetMessageTime();
    // Test and actual callbacks are paired by phase, not by their incidental
    // message time/lParam. Motion cancellation must not erase this pairing.
    if (!_liveSDKeyObservation.Observe(virtualKey, static_cast<std::uint32_t>(messageTime),
                                       static_cast<std::uint64_t>(keyData), actualKeyDown))
        return;
    // Send in observed key order, before the caret-read queue groups motion
    // pulses and Enter actions. In particular, a -> Enter -> b must remain
    // increment -> reset -> increment even within one 16 ms motion batch.
    // No text is transmitted and older Servers never receive these new kinds.
    if (SupportsLiveSDCombo())
    {
        if (virtualKey == VK_BACK || virtualKey == VK_RETURN)
            SendLiveSDActivityToUIProcess(FanyImeLiveSDActivity::ComboReset,
                                          POINT{0, FanyImeLiveSDActivity::InvalidCaretY}, nullptr);
        else if (!ctrl && IsTypingKey(virtualKey))
            SendLiveSDActivityToUIProcess(FanyImeLiveSDActivity::ComboTyping,
                                          POINT{0, FanyImeLiveSDActivity::InvalidCaretY}, nullptr);
    }
    if (ctrl)
    {
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (virtualKey == 'Z' && !shift && (keyData & (static_cast<LPARAM>(1) << 30)) == 0)
            _QueueLiveSDActivity(false, 0, false, true, false);
        return;
    }
    _QueueLiveSDActivity(IsTypingKey(virtualKey), static_cast<DWORD>(messageTime), virtualKey == VK_RETURN);
}

void CMetasequoiaIME::_QueueLiveSDActivity(bool typingPulse, DWORD typingMessageTime, bool enterAction,
                                           bool pinPosition, bool requestFocus)
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
    if (pinPosition)
        _liveSDPinRequests = (std::min)(_liveSDPinRequests + 1U, MaxPendingPinRequests);
    _liveSDFocusRequested = _liveSDFocusRequested || requestFocus;
    if (!_liveSDTimerArmed && (!_liveSDReadPending || pinPosition))
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
    if (!_pThreadMgr || !SupportsLiveSDActivity() || !IsNamedpipeFocusStateOwner(this))
        return;
    const uint64_t activitySerial = _liveSDSerial;
    const uint64_t focusGeneration = _deferredKeyFocusGeneration;
    const uint64_t focusToken = _CaptureFocusSessionToken();
    BOOL hasThreadFocus = FALSE;
    const HRESULT threadFocusResult = _pThreadMgr->IsThreadFocus(&hasThreadFocus);
    const bool keyboardDisabled = _IsKeyboardDisabled();
    // COM focus/compartment queries can re-enter document-focus callbacks.
    // This timer must not consume requests queued by the replacement session.
    if (activitySerial != _liveSDSerial || focusGeneration != _deferredKeyFocusGeneration ||
        focusToken != _CaptureFocusSessionToken() || !IsNamedpipeFocusStateOwner(this))
        return;
    if (FAILED(threadFocusResult) || !hasThreadFocus || keyboardDisabled)
    {
        _CancelLiveSDActivity(true);
        return;
    }
    if (!_IsFocusSessionCurrent(focusToken))
    {
        _liveSDPulses = 0;
        _liveSDEnterActions = 0;
        _liveSDPinRequests = 0;
        _liveSDFocusRequested = false;
        _liveSDPulseIntervals.fill(0);
        _liveSDHasTypingMessageTime = false;
        return;
    }
    const unsigned pinRequests = _liveSDPinRequests;
    _liveSDPinRequests = 0;
    // Position pinning does not depend on the host granting a caret read lock.
    // A previous asynchronous edit session may still be waiting for that lock.
    for (unsigned i = 0; i < pinRequests; ++i)
        SendLiveSDActivityToUIProcess(FanyImeLiveSDActivity::PinPosition,
                                      POINT{0, FanyImeLiveSDActivity::InvalidCaretY}, nullptr);
    if (_liveSDReadPending)
        return;
    const bool focusRequested = _liveSDFocusRequested;
    _liveSDFocusRequested = false;
    if (!focusRequested && !_liveSDPulses && !_liveSDEnterActions)
        return;
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
                                                               serial, pulses, enterActions, focusRequested, intervals);
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
                                    focusRequested, intervals, false, POINT{0, FanyImeLiveSDActivity::InvalidCaretY},
                                    nullptr);
        }
    }
    context->Release();
}

void CMetasequoiaIME::_CompleteLiveSDActivity(ITfContext *context, uint64_t focusToken, uint64_t focusGeneration,
                                              uint64_t serial, unsigned pulses, unsigned enterActions,
                                              bool focusRequested,
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
        _liveSDPinRequests = 0;
        _liveSDFocusRequested = false;
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
        if (focusRequested)
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
        if ((focusRequested || pulses || enterActions) && _liveSDCaretRetries++ < 2)
        {
            // Transient TS_E_NOLAYOUT is normal in Chromium; never substitute a
            // fabricated/default position or a point from a previous context.
            _QueueLiveSDActivity();
        }
    }
    if (_liveSDPulses || _liveSDEnterActions || _liveSDFocusRequested)
        _QueueLiveSDActivity();
    else if (_liveSDPinRequests && !_liveSDTimerArmed)
        _liveSDTimerArmed = SetTimer(_msgWndHandle, TIMER_LIVESD_ACTIVITY, ActivityDelayMs, nullptr) != 0;
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
        _liveSDPinRequests = 0;
    if (!keepPulses)
        _liveSDFocusRequested = false;
    if (!keepPulses)
    {
        _liveSDPulseIntervals.fill(0);
        _liveSDHasTypingMessageTime = false;
    }
    _liveSDCaretRetries = 0;
    if (sendBlur && IsNamedpipeFocusStateOwner(this))
        SendLiveSDActivityToUIProcess(FanyImeLiveSDActivity::Blur, POINT{0, FanyImeLiveSDActivity::InvalidCaretY},
                                      nullptr);
}

void CMetasequoiaIME::_OnLiveSDThreadFocusLost()
{
    // Thread focus can move to auxiliary UI without ending the text-field
    // session. Clear queued actions, but leave visibility to the authoritative
    // document-session and Server foreground-owner paths.
    _CancelLiveSDActivity(false);
}
