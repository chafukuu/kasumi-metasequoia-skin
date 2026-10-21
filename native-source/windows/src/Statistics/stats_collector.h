#pragma once

#include "../../../engine/contracts/windows_ipc.h"
#include "char_classify.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace MsimeStats
{
// Fixed-capacity FIFO between the TSF capture path and the asynchronous pipe
// sender. It deliberately has no merge, no bucketing and no timing state: the
// Server owns every aggregate. When the queue is full the oldest event is
// evicted and that loss is reported through the frame's dropped_count. Access
// must be serialized by the caller.
class StatsEventQueue
{
  public:
    static constexpr size_t kCapacity = 256;

    // Returns false when the oldest event had to be evicted to make room.
    bool Push(const FanyImeStatsEvent &event);
    // Moves up to maxCount oldest events into out; returns how many were taken.
    size_t PopBatch(FanyImeStatsEvent *out, size_t maxCount);
    // Events dropped since the previous call (queue overflow, send failures).
    uint32_t TakeDroppedCount();
    void AddDroppedCount(uint32_t count);
    size_t Size() const
    {
        return _count;
    }

  private:
    FanyImeStatsEvent _events[kCapacity] = {};
    size_t _head = 0;
    size_t _count = 0;
    uint32_t _droppedCount = 0;
};

// Called from the TSF capture paths (composition commits and passthrough
// keys). Events with no characters are ignored. The event is appended to the
// in-memory queue and an asynchronous flush is triggered immediately; no local
// time, bucketing or active-time logic runs here, and the host's commit path
// never touches the pipe.
void QueueStatisticsEvent(const CharClassCounts &counts);
} // namespace MsimeStats

inline bool MsimeStats::StatsEventQueue::Push(const FanyImeStatsEvent &event)
{
    if (_count == kCapacity)
    {
        // Overwrite the oldest event and rotate the head: the queue stays full
        // and the newest event is always retained.
        _events[_head] = event;
        _head = (_head + 1) % kCapacity;
        AddDroppedCount(1);
        return false;
    }
    _events[(_head + _count) % kCapacity] = event;
    ++_count;
    return true;
}

inline size_t MsimeStats::StatsEventQueue::PopBatch(FanyImeStatsEvent *out, size_t maxCount)
{
    if (out == nullptr)
    {
        return 0;
    }
    const size_t taken = (std::min)(maxCount, _count);
    for (size_t index = 0; index < taken; ++index)
    {
        out[index] = _events[(_head + index) % kCapacity];
    }
    _head = (_head + taken) % kCapacity;
    _count -= taken;
    return taken;
}

inline uint32_t MsimeStats::StatsEventQueue::TakeDroppedCount()
{
    const uint32_t dropped = _droppedCount;
    _droppedCount = 0;
    return dropped;
}

inline void MsimeStats::StatsEventQueue::AddDroppedCount(uint32_t count)
{
    const uint64_t sum = static_cast<uint64_t>(_droppedCount) + count;
    _droppedCount = sum > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(sum);
}
