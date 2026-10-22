#include "window/candidate_animation_scheduler.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <new>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using Clock = CandidateAnimationScheduler::Clock;
using namespace std::chrono_literals;
constexpr UINT kFrameMessage = WM_APP + 0x3E0;
constexpr UINT kOtherWorkMessage = WM_APP + 0x3E1;
constexpr UINT_PTR kBaselineTimer = 0x0A11;

void Require(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
}

double Milliseconds(Clock::duration value)
{
    return std::chrono::duration<double, std::milli>(value).count();
}

struct MessageWindow
{
    HWND handle = nullptr;

    MessageWindow()
    {
        handle = CreateWindowExW(0, L"STATIC", L"CandidateAnimationSchedulerSmoke", 0, 0, 0, 0, 0, HWND_MESSAGE,
                                 nullptr, GetModuleHandleW(nullptr), nullptr);
        Require(handle != nullptr, "message-only window creation");
    }

    ~MessageWindow()
    {
        DestroyWindow(handle);
    }
};

// Exercise real BeginPaint/EndPaint and destruction messages with a fully
// transparent, nonactivating tool window. WS_VISIBLE is needed for UpdateWindow.
// This verifies the presenter's Win32 contract without Direct2D fault injection.
struct PaintWindow
{
    HWND handle = nullptr;
    CandidateAnimationScheduler *scheduler = nullptr;
    int paints = 0;
    int destroys = 0;
    int nonClientDestroys = 0;
    bool invalidateWhilePainting = false;
    bool failPaint = false;

    PaintWindow()
    {
        static const ATOM windowClass = [] {
            WNDCLASSW value{};
            value.lpfnWndProc = &WindowProc;
            value.hInstance = GetModuleHandleW(nullptr);
            value.lpszClassName = L"CandidateAnimationPaintSmoke";
            return RegisterClassW(&value);
        }();
        Require(windowClass != 0, "transparent paint window class registered");
        handle = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"CandidateAnimationPaintSmoke",
                                 L"CandidateAnimationPaintSmoke", WS_POPUP, 0, 0, 64, 64, nullptr, nullptr,
                                 GetModuleHandleW(nullptr), this);
        Require(handle != nullptr, "transparent paint window creation");
        Require(SetLayeredWindowAttributes(handle, 0, 0, LWA_ALPHA) != FALSE, "paint fixture made fully transparent");
        ShowWindow(handle, SW_SHOWNOACTIVATE);
    }

    ~PaintWindow()
    {
        if (handle)
            DestroyWindow(handle);
    }

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto *self = reinterpret_cast<PaintWindow *>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE)
        {
            self = static_cast<PaintWindow *>(reinterpret_cast<CREATESTRUCTW *>(lParam)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self)
        {
            if (message == WM_PAINT)
            {
                PAINTSTRUCT paint{};
                BeginPaint(window, &paint);
                ++self->paints;
                if (self->invalidateWhilePainting)
                {
                    self->invalidateWhilePainting = false;
                    InvalidateRect(window, nullptr, FALSE);
                }
                EndPaint(window, &paint);
                if (self->failPaint)
                    InvalidateRect(window, nullptr, FALSE);
                return 0;
            }
            if (message == WM_DESTROY || message == WM_NCDESTROY)
            {
                if (self->scheduler)
                    self->scheduler->Bind(nullptr, 0);
                if (message == WM_DESTROY)
                    ++self->destroys;
                else
                {
                    ++self->nonClientDestroys;
                    self->handle = nullptr;
                    SetWindowLongPtrW(window, GWLP_USERDATA, 0);
                }
            }
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }
};

bool TakeMessage(HWND window, UINT type, MSG &message, std::chrono::milliseconds timeout, UINT remove = PM_REMOVE)
{
    const auto end = Clock::now() + timeout;
    for (;;)
    {
        if (PeekMessageW(&message, window, type, type, remove))
            return true;
        const auto left = std::chrono::ceil<std::chrono::milliseconds>(end - Clock::now()).count();
        if (left <= 0)
            return false;
        // Only message-only or fully transparent test windows are used.
        // No IME, input injection or system timer-resolution changes.
        MsgWaitForMultipleObjectsEx(0, nullptr, static_cast<DWORD>(left), QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
}

std::size_t DrainFrames(HWND window)
{
    std::size_t count = 0;
    MSG message{};
    while (PeekMessageW(&message, window, kFrameMessage, kFrameMessage, PM_REMOVE))
        ++count;
    return count;
}

struct Statistics
{
    double mean = 0.0;
    double median = 0.0;
    double p95 = 0.0;
    double maximum = 0.0;
    std::vector<double> samples;

    explicit Statistics(std::vector<double> values) : samples(std::move(values))
    {
        Require(!samples.empty(), "timing samples");
        mean = std::accumulate(samples.begin(), samples.end(), 0.0) / samples.size();
        auto sorted = samples;
        std::sort(sorted.begin(), sorted.end());
        median = sorted[sorted.size() / 2];
        p95 = sorted[static_cast<std::size_t>(sorted.size() * 0.95)];
        maximum = sorted.back();
    }

    void Json(std::ostream &out) const
    {
        out << "{\"count\":" << samples.size() << ",\"meanMs\":" << mean << ",\"p50Ms\":" << median
            << ",\"p95Ms\":" << p95 << ",\"maxMs\":" << maximum << ",\"samplesMs\":[";
        for (std::size_t index = 0; index < samples.size(); ++index)
            out << (index ? "," : "") << samples[index];
        out << "]}";
    }
};

void TestLifecycle(HWND window)
{
    CandidateAnimationScheduler scheduler;
    Require(!scheduler.Schedule(Clock::now()), "unbound scheduler rejects scheduling");
    Require(!scheduler.Bind(nullptr, kFrameMessage), "invalid HWND rejected");
    Require(scheduler.Bind(window, kFrameMessage), "valid message-only HWND bound");

    // Repeated later requests cannot postpone the original 10 ms deadline.
    auto started = Clock::now();
    Require(scheduler.Schedule(started + 10ms), "first deadline accepted");
    for (int request = 0; request < 1000; ++request)
        Require(scheduler.Schedule(started + 300ms + std::chrono::milliseconds(request)), "later request preserved");
    MSG message{};
    Require(TakeMessage(window, kFrameMessage, message, 250ms), "earlier deadline not postponed");
    Require(Milliseconds(Clock::now() - started) < 250.0, "original deadline delivered before later request");
    Require(scheduler.ConsumeMessage(message.wParam, message.lParam), "first wake-up consumed");
    Require(!scheduler.ConsumeMessage(message.wParam, message.lParam), "duplicate wake-up ignored");

    // An earlier replacement must rearm the wait, rather than keep a late one.
    started = Clock::now();
    Require(scheduler.Schedule(started + 800ms), "late deadline accepted");
    Require(scheduler.Schedule(started + 5ms), "earlier replacement accepted");
    Require(TakeMessage(window, kFrameMessage, message, 250ms), "earlier replacement delivered");
    Require(scheduler.ConsumeMessage(message.wParam, message.lParam), "replacement wake-up consumed");

    // One callback posts one message; Schedule while that message is queued
    // leaves it intact and cannot create another wake-up behind it.
    Require(scheduler.Schedule(Clock::now() + 1ms), "queued test scheduled");
    Require(TakeMessage(window, kFrameMessage, message, 250ms, PM_NOREMOVE), "queued wake-up observed");
    for (int request = 0; request < 1000; ++request)
        Require(scheduler.Schedule(Clock::now()), "queued wake-up preserved");
    Require(TakeMessage(window, kFrameMessage, message, 1ms), "queued message remains available");
    Require(scheduler.ConsumeMessage(message.wParam, message.lParam), "queued message consumed once");
    Sleep(30);
    Require(DrainFrames(window) == 0, "no duplicate or stacked wake-ups");

    Require(scheduler.Schedule(Clock::now() + 1ms), "cancel-queued test scheduled");
    Require(TakeMessage(window, kFrameMessage, message, 250ms), "old queued message captured");
    scheduler.Cancel();
    Require(!scheduler.ConsumeMessage(message.wParam, message.lParam), "cancel invalidates old generation");
    const MSG oldMessage = message;
    Require(scheduler.Schedule(Clock::now() + 1ms), "scheduler reusable after cancel");
    Require(TakeMessage(window, kFrameMessage, message, 250ms), "new generation delivered");
    Require(!scheduler.ConsumeMessage(oldMessage.wParam, oldMessage.lParam),
            "old generation cannot consume new wake-up");
    Require(scheduler.ConsumeMessage(message.wParam, message.lParam), "new generation consumed");

    Require(scheduler.Schedule(Clock::now() + 100ms), "cancel-pending test scheduled");
    scheduler.Cancel();
    Require(!TakeMessage(window, kFrameMessage, message, 130ms), "canceled pending wait stays silent");
    Require(scheduler.Schedule(Clock::now() - 10ms), "past deadline accepted");
    Require(TakeMessage(window, kFrameMessage, message, 250ms), "past deadline wakes promptly");
    Require(!scheduler.ConsumeMessage(message.wParam, message.lParam + 1), "incorrect instance cookie rejected");
    Require(scheduler.ConsumeMessage(message.wParam, message.lParam), "past deadline consumed");
    scheduler.Cancel();

    Require(scheduler.Schedule(Clock::now() + 1ms), "unbind-queued test scheduled");
    Require(TakeMessage(window, kFrameMessage, message, 250ms), "unbind old message captured");
    const MSG beforeUnbind = message;
    Require(!scheduler.Bind(nullptr, 0), "explicit unbind clears HWND and message");
    Require(!scheduler.Schedule(Clock::now()), "unbound scheduler cannot rearm");
    Require(!scheduler.ConsumeMessage(beforeUnbind.wParam, beforeUnbind.lParam), "unbind rejects queued generation");
    Require(scheduler.Bind(window, kFrameMessage), "scheduler rebound after unbind");
    Require(scheduler.Schedule(Clock::now() + 1ms), "rebound scheduler armed");
    Require(TakeMessage(window, kFrameMessage, message, 250ms), "rebound message delivered");
    Require(!scheduler.ConsumeMessage(beforeUnbind.wParam, beforeUnbind.lParam),
            "unbound generation cannot consume rebind");
    Require(scheduler.ConsumeMessage(message.wParam, message.lParam), "rebound generation consumed");
    Require(scheduler.Schedule(Clock::now() + 100ms), "unbind-pending test scheduled");
    Require(!scheduler.Bind(nullptr, 0), "pending wait explicitly unbound");
    Require(!TakeMessage(window, kFrameMessage, message, 130ms), "unbound pending wait stays silent");

    // A presenter may be rebuilt at the same address and bound to the same
    // HWND. Instance cookies must reject the previous object's queued message,
    // even when the new object's local generation has the same numeric value.
    alignas(CandidateAnimationScheduler) unsigned char storage[sizeof(CandidateAnimationScheduler)];
    auto *first = new (storage) CandidateAnimationScheduler;
    Require(first->Bind(window, kFrameMessage), "first placement instance bound");
    Require(first->Schedule(Clock::now() + 1ms), "first placement instance armed");
    Require(TakeMessage(window, kFrameMessage, message, 250ms), "first placement message captured");
    const MSG previousInstanceMessage = message;
    first->~CandidateAnimationScheduler();
    auto *second = new (storage) CandidateAnimationScheduler;
    Require(second->Bind(window, kFrameMessage), "second placement instance bound");
    Require(second->Schedule(Clock::now() + 1ms), "second placement instance armed");
    Require(TakeMessage(window, kFrameMessage, message, 250ms), "second placement message captured");
    Require(!second->ConsumeMessage(previousInstanceMessage.wParam, previousInstanceMessage.lParam),
            "previous same-address instance message rejected");
    Require(second->ConsumeMessage(message.wParam, message.lParam), "current same-address instance message consumed");
    second->~CandidateAnimationScheduler();

    // Exercise destructors immediately before, during and after timer signal.
    // Cancel joins callbacks before any state or synchronization object dies.
    for (int iteration = 0; iteration < 300; ++iteration)
    {
        CandidateAnimationScheduler transient;
        Require(transient.Bind(window, kFrameMessage), "stress scheduler bound");
        Require(transient.Schedule(Clock::now() + 1ms), "stress scheduler armed");
        if (iteration % 3 == 0)
            Sleep(1);
    }
    DrainFrames(window);
    Sleep(40);
    Require(DrainFrames(window) == 0, "destructed schedulers cannot post later");
}

void TestDestroyAndPaint()
{
    CandidateAnimationScheduler scheduler;
    PaintWindow window;
    window.scheduler = &scheduler;

    Require(IsWindowVisible(window.handle) != FALSE, "transparent paint fixture has normal Win32 paint eligibility");
    ValidateRect(window.handle, nullptr);
    window.paints = 0;
    Require(InvalidateRect(window.handle, nullptr, FALSE) != FALSE, "initial paint invalidated");
    Require(UpdateWindow(window.handle) != FALSE, "initial synchronous paint requested");
    Require(window.paints == 1, "UpdateWindow paints synchronously once");
    Require(!GetUpdateRect(window.handle, nullptr, FALSE), "BeginPaint consumes initial dirty region");
    UpdateWindow(window.handle);
    Require(window.paints == 1, "clean window is not repainted twice");

    window.invalidateWhilePainting = true;
    InvalidateRect(window.handle, nullptr, FALSE);
    UpdateWindow(window.handle);
    Require(window.paints == 2, "render invalidation test painted once");
    Require(GetUpdateRect(window.handle, nullptr, FALSE) != FALSE, "render-created invalidation remains pending");
    UpdateWindow(window.handle);
    Require(window.paints == 3, "render-created invalidation receives next paint");
    Require(!GetUpdateRect(window.handle, nullptr, FALSE), "follow-up paint consumes render invalidation");

    window.failPaint = true;
    InvalidateRect(window.handle, nullptr, FALSE);
    UpdateWindow(window.handle);
    Require(window.paints == 4, "failed paint attempted once");
    Require(GetUpdateRect(window.handle, nullptr, FALSE) != FALSE, "failed paint retains retry after EndPaint");
    window.failPaint = false;
    UpdateWindow(window.handle);
    Require(window.paints == 5, "failed paint retry executed");
    Require(!GetUpdateRect(window.handle, nullptr, FALSE), "successful retry consumes dirty region");

    Require(scheduler.Bind(window.handle, kFrameMessage), "destroy test scheduler bound");
    Require(scheduler.Schedule(Clock::now() + 100ms), "destroy test pending wait armed");
    const HWND destroyed = window.handle;
    Require(DestroyWindow(destroyed) != FALSE, "test HWND destroyed");
    Require(window.destroys == 1 && window.nonClientDestroys == 1, "both destruction messages reach host");
    Require(window.handle == nullptr && !IsWindow(destroyed), "destroyed HWND detached");
    Require(!scheduler.Schedule(Clock::now()), "destroy handler unbind prevents further scheduling");
    MSG message{};
    Require(!TakeMessage(nullptr, kFrameMessage, message, 130ms), "destroyed window pending callback stays silent");
}

void TestOverdueYield(HWND window)
{
    const auto now = Clock::now();
    Require(CandidateAnimationScheduler::YieldIfOverdue(now - 1ms, now) == now + 2ms, "past frame yields two ms");
    Require(CandidateAnimationScheduler::YieldIfOverdue(now, now) == now + 2ms, "equal frame deadline yields two ms");
    Require(CandidateAnimationScheduler::YieldIfOverdue(now + 1ms, now) == now + 1ms,
            "future frame deadline unchanged");

    CandidateAnimationScheduler scheduler;
    Require(scheduler.Bind(window, kFrameMessage), "overdue scheduler bound");
    constexpr int frames = 24;
    const auto started = Clock::now();
    for (int frame = 0; frame < frames; ++frame)
    {
        const auto sampled = Clock::now();
        Require(scheduler.Schedule(CandidateAnimationScheduler::YieldIfOverdue(sampled - 5ms, sampled)),
                "overdue frame scheduled with yield");
        Require(PostMessageW(window, kOtherWorkMessage, frame, 0) != FALSE, "other UI work queued");
        MSG message{};
        Require(TakeMessage(window, kOtherWorkMessage, message, 250ms), "other UI work processed between frames");
        Require(message.wParam == static_cast<WPARAM>(frame), "other UI work delivered once");
        Require(TakeMessage(window, kFrameMessage, message, 250ms), "yielded frame delivered");
        Require(scheduler.ConsumeMessage(message.wParam, message.lParam), "yielded frame consumed");
    }
    // A deliberately broad lower bound catches an immediate repost loop without
    // making correctness depend on submillisecond timer precision.
    Require(Milliseconds(Clock::now() - started) >= frames, "overdue frames do not form an immediate redraw loop");
    Require(DrainFrames(window) == 0, "overdue frames never stack wake-ups");
}

Statistics Baseline(HWND window)
{
    std::vector<double> samples;
    auto last = Clock::now();
    // Exact original presenter policy: ceil(1000/60) = 17 ms, 1 ms slack,
    // KillTimer and rearm once for each delivered WM_TIMER.
    Require(SetCoalescableTimer(window, kBaselineTimer, 17, nullptr, 1) != 0, "baseline timer armed");
    for (int frame = 0; frame < 80; ++frame)
    {
        MSG message{};
        Require(TakeMessage(window, WM_TIMER, message, 1000ms), "baseline WM_TIMER delivered");
        Require(message.wParam == kBaselineTimer, "baseline timer id");
        KillTimer(window, kBaselineTimer);
        const auto now = Clock::now();
        samples.push_back(Milliseconds(now - last));
        last = now;
        if (frame + 1 < 80)
            Require(SetCoalescableTimer(window, kBaselineTimer, 17, nullptr, 1) != 0, "baseline timer rearmed");
    }
    return Statistics(std::move(samples));
}

Statistics SchedulerTiming(HWND window, bool &highResolution)
{
    CandidateAnimationScheduler scheduler;
    Require(scheduler.Bind(window, kFrameMessage), "timed scheduler bound");
    std::vector<double> samples;
    auto last = Clock::now();
    const auto interval =
        std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double, std::milli>(1000.0 / 60.0));
    for (int frame = 0; frame < 80; ++frame)
    {
        Require(scheduler.Schedule(last + interval), "timed scheduler armed");
        highResolution = scheduler.IsHighResolution();
        MSG message{};
        Require(TakeMessage(window, kFrameMessage, message, 1000ms), "timed scheduler message delivered");
        Require(scheduler.ConsumeMessage(message.wParam, message.lParam), "timed scheduler message consumed");
        const auto now = Clock::now();
        samples.push_back(Milliseconds(now - last));
        last = now;
    }
    scheduler.Cancel();
    return Statistics(std::move(samples));
}
} // namespace

int main(int argc, char **argv)
{
    try
    {
        MessageWindow window;
        TestLifecycle(window.handle);
        TestDestroyAndPaint();
        TestOverdueYield(window.handle);
        const Statistics baseline = Baseline(window.handle);
        bool highResolution = false;
        const Statistics scheduler = SchedulerTiming(window.handle, highResolution);
        std::cout << std::fixed << std::setprecision(3)
                  << "PASS lifecycle: later/earlier deadlines, queued dedup, "
                     "cancel generations, rearm, past deadline, "
                     "explicit unbind/rebind, 300 destructor races\n"
                  << "PASS transparent Win32 window: synchronous paint, render "
                     "invalidation, failed-paint retry, destroy unbind\n"
                  << "PASS overdue policy: past/equal/future deadlines and 24 "
                     "yielded frames with queued UI work\n"
                  << "WM_TIMER baseline: mean=" << baseline.mean << " ms, p95=" << baseline.p95
                  << " ms, max=" << baseline.maximum << " ms\n"
                  << "Scheduler (highResolution=" << highResolution << "): mean=" << scheduler.mean
                  << " ms, p95=" << scheduler.p95 << " ms, max=" << scheduler.maximum << " ms\n";
        if (argc > 1)
        {
            std::ofstream out(argv[1], std::ios::binary);
            Require(out.good(), "timing JSON output opened");
            out << std::fixed << std::setprecision(6)
                << "{\"schema\":\"kasumi.animation-scheduler-smoke/"
                   "v1\",\"passed\":true,\"highResolution\":"
                << (highResolution ? "true" : "false")
                << ",\"testSurface\":\"Win32 message-only and fully transparent paint HWNDs; no "
                   "installed IME or Direct2D fault injection\""
                   ",\"checks\":{\"explicitUnbind\":true,\"destroyUnbind\":true,"
                   "\"synchronousPaint\":true,"
                   "\"renderInvalidation\":true,\"failedPaintRetry\":true,"
                   "\"yieldedFrames\":24},\"originalWmTimer\":";
            baseline.Json(out);
            out << ",\"scheduler\":";
            scheduler.Json(out);
            out << "}\n";
        }
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
