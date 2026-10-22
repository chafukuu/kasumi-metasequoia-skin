#pragma once

#include <cstdint>
#include <mutex>

namespace CandidateTypingStars
{
struct Snapshot
{
    std::uint64_t bursts = 0;
    std::uint64_t epoch = 0;
};

// The input worker publishes accepted insertions, and the UI consumes pairs.
// Only numeric counts cross this boundary; no input text or key is retained.
class EventBridge
{
  public:
    void NotifyInserted()
    {
        const std::lock_guard lock(mutex_);
        if (++pending_ == 2)
        {
            pending_ = 0;
            ++bursts_;
        }
    }

    Snapshot ConsumeSnapshot()
    {
        const std::lock_guard lock(mutex_);
        const Snapshot result{bursts_, epoch_};
        bursts_ = 0;
        return result;
    }

    std::uint64_t ConsumeBursts()
    {
        return ConsumeSnapshot().bursts;
    }

    // Editing breaks the insertion pair, but completed bursts and live UI
    // particles still belong to the current composition.
    void BreakSequence()
    {
        const std::lock_guard lock(mutex_);
        pending_ = 0;
    }

    void Reset()
    {
        const std::lock_guard lock(mutex_);
        ResetUnlocked();
    }

    // UI hide messages can lag behind the worker starting a new composition.
    // An observed old generation must never discard that composition's input.
    bool ResetIfEpoch(std::uint64_t observed)
    {
        const std::lock_guard lock(mutex_);
        if (observed != epoch_)
            return false;
        ResetUnlocked();
        return true;
    }

  private:
    void ResetUnlocked()
    {
        pending_ = 0;
        bursts_ = 0;
        ++epoch_;
    }

    std::mutex mutex_;
    unsigned pending_ = 0;
    std::uint64_t bursts_ = 0;
    std::uint64_t epoch_ = 1;
};

inline EventBridge &SharedEvents()
{
    static EventBridge events;
    return events;
}

inline void NotifyInserted()
{
    SharedEvents().NotifyInserted();
}

inline std::uint64_t ConsumeBursts()
{
    return SharedEvents().ConsumeBursts();
}

inline Snapshot ConsumeSnapshot()
{
    return SharedEvents().ConsumeSnapshot();
}

inline void BreakSequence()
{
    SharedEvents().BreakSequence();
}

inline void Reset()
{
    SharedEvents().Reset();
}

inline bool ResetIfEpoch(std::uint64_t observed)
{
    return SharedEvents().ResetIfEpoch(observed);
}
} // namespace CandidateTypingStars
