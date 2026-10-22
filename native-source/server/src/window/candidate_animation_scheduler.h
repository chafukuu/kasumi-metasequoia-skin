#pragma once

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>

// A single outstanding animation wake-up, with no drawing or input work on the
// thread-pool thread. All public methods are called by the owning UI thread.
class CandidateAnimationScheduler
{
  public:
    using Clock = std::chrono::steady_clock;

    CandidateAnimationScheduler() = default;
    CandidateAnimationScheduler(const CandidateAnimationScheduler &) = delete;
    CandidateAnimationScheduler &operator=(const CandidateAnimationScheduler &) = delete;

    ~CandidateAnimationScheduler()
    {
        Cancel();
    }

    bool Bind(HWND window, UINT message)
    {
        Cancel();
        std::lock_guard<std::mutex> lock(mutex_);
        window_ = IsWindow(window) ? window : nullptr;
        message_ = message;
        return window_ && message_ != 0;
    }

    // Presentation policy: a missed decorative frame yields briefly instead
    // of immediately redrawing under sustained load. Future deadlines stay exact.
    // Schedule itself still accepts past deadlines for prompt notification.
    static Clock::time_point YieldIfOverdue(Clock::time_point deadline, Clock::time_point now)
    {
        return deadline <= now ? now + std::chrono::milliseconds(2) : deadline;
    }

    bool Schedule(Clock::time_point deadline)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!window_ || !message_)
                return false;
            // Input/paint updates must not postpone a frame which is already
            // due, or stack another message behind an unconsumed wake-up.
            if (queued_ || (armed_ && deadline_ <= deadline))
                return true;
            InvalidateLocked();
        }
        if (!EnsureResources())
            return false;
        QuiesceWait();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            deadline_ = deadline;
            armed_ = true;
        }

        // A negative relative FILETIME avoids wall-clock changes. Round upward
        // to 100 ns, rather than rounding a 60 Hz interval up to 17 ms.
        using TimerTick = std::chrono::duration<long long, std::ratio<1, 10000000>>;
        const auto remaining = deadline - Clock::now();
        auto ticks = std::chrono::ceil<TimerTick>(remaining).count();
        if (ticks < 1)
            ticks = 1;
        LARGE_INTEGER due{};
        due.QuadPart = -ticks;
        if (!SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE))
        {
            std::lock_guard<std::mutex> lock(mutex_);
            InvalidateLocked();
            return false;
        }
        SetThreadpoolWait(wait_, timer_, nullptr);
        return true;
    }

    void Cancel()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            InvalidateLocked();
        }
        // Never hold mutex_ while joining a callback: it takes the same lock.
        QuiesceWait();
        if (wait_)
        {
            CloseThreadpoolWait(wait_);
            wait_ = nullptr;
        }
        if (timer_)
        {
            CloseHandle(timer_);
            timer_ = nullptr;
        }
        highResolution_ = false;
    }

    bool ConsumeMessage(WPARAM generation, LPARAM instance)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (static_cast<UINT_PTR>(instance) != instance_ || !queued_ || generation != generation_)
            return false;
        queued_ = false;
        return true;
    }

    bool IsHighResolution() const
    {
        return highResolution_;
    }

  private:
    static UINT_PTR NextInstance()
    {
        static std::atomic<UINT_PTR> next{1};
        UINT_PTR value = next.fetch_add(1, std::memory_order_relaxed);
        if (value == 0)
            value = next.fetch_add(1, std::memory_order_relaxed);
        return value;
    }

    void InvalidateLocked()
    {
        armed_ = false;
        queued_ = false;
        if (++generation_ == 0)
            ++generation_;
    }

    bool EnsureResources()
    {
        if (timer_ && wait_)
            return true;
        // Older Windows releases reject the high-resolution flag; an ordinary
        // waitable timer remains a supported, correct fallback.
        constexpr DWORD kHighResolution = 0x00000002;
        constexpr DWORD access = TIMER_MODIFY_STATE | SYNCHRONIZE;
        timer_ = CreateWaitableTimerExW(nullptr, nullptr, kHighResolution, access);
        highResolution_ = timer_ != nullptr;
        if (!timer_)
            timer_ = CreateWaitableTimerExW(nullptr, nullptr, 0, access);
        if (!timer_)
            return false;
        wait_ = CreateThreadpoolWait(&TimerReady, this, nullptr);
        if (wait_)
            return true;
        CloseHandle(timer_);
        timer_ = nullptr;
        highResolution_ = false;
        return false;
    }

    void QuiesceWait()
    {
        if (wait_)
            SetThreadpoolWait(wait_, nullptr, nullptr);
        if (timer_)
            CancelWaitableTimer(timer_);
        if (wait_)
            WaitForThreadpoolWaitCallbacks(wait_, TRUE);
    }

    static void CALLBACK TimerReady(PTP_CALLBACK_INSTANCE, void *context, PTP_WAIT, TP_WAIT_RESULT result)
    {
        auto &self = *static_cast<CandidateAnimationScheduler *>(context);
        std::lock_guard<std::mutex> lock(self.mutex_);
        if (result != WAIT_OBJECT_0 || !self.armed_ || self.queued_)
            return;
        self.armed_ = false;
        self.queued_ = true;
        if (!PostMessageW(self.window_, self.message_, self.generation_, static_cast<LPARAM>(self.instance_)))
            self.queued_ = false;
    }

    std::mutex mutex_;
    HWND window_ = nullptr;
    UINT message_ = 0;
    HANDLE timer_ = nullptr;
    PTP_WAIT wait_ = nullptr;
    Clock::time_point deadline_{};
    const UINT_PTR instance_ = NextInstance();
    UINT_PTR generation_ = 1;
    bool armed_ = false;
    bool queued_ = false;
    bool highResolution_ = false;
};
