#pragma once

// Event-stream aggregation for the input statistics feature: local-time
// bucketing and active-time accounting. A direct port of the standalone tool's
// internal/stats/stats.go; the state machine is deliberately tiny so its
// behaviour is pinned by unit tests.

#include "engine/contracts/windows_ipc.h"
#include "statistics/stats_types.h"

#include <functional>
#include <vector>

namespace MsimeStats
{
// LocalTimeResolver maps a UTC instant to local calendar fields. Production
// uses DefaultLocalTimeResolver (Win32, DST-aware); tests inject a fixed offset
// so the bucketing assertions do not depend on the machine's timezone.
using LocalTimeResolver = std::function<LocalTimeParts(TimePoint)>;

// DefaultLocalTimeResolver resolves through SystemTimeToTzSpecificLocalTime,
// which applies the current timezone including DST. When the conversion fails
// the UTC fields are returned instead of dropping the event.
LocalTimeParts DefaultLocalTimeResolver(TimePoint utc);

// ActiveDeltaMs returns how much of the gap between two consecutive commits
// counts as active time: the gap itself when it is positive and at most
// kActiveGapLimitMs, otherwise zero. The unknown-time sentinel, zero gaps,
// out-of-order events and clock rollbacks all yield zero.
int64_t ActiveDeltaMs(TimePoint prev, TimePoint cur);

// Accumulator turns a stream of raw events into per-day/per-hour deltas.
//
// The state is the timestamp of the last accepted event. The stream is global
// (not per source process): typing continuously across two applications counts
// as continuous activity, and events arriving out of order contribute no active
// time but do contribute characters.
class Accumulator
{
  public:
    Accumulator();
    explicit Accumulator(LocalTimeResolver resolver);

    // Add folds one frame's events into per-day and per-hour deltas. The result
    // is sorted by day key and by (day, hour).
    Batch Add(const std::vector<FanyImeStatsEvent> &events);

    // Reset clears the accumulator state (used when the collector restarts).
    void Reset();

    // LastEventTime exposes the accumulator state (tests and diagnostics);
    // TimePoint{} when no event has been accepted yet.
    TimePoint LastEventTime() const;
    bool HasLastEvent() const;

  private:
    LocalTimeResolver local_time_;
    TimePoint last_{};
    bool has_last_ = false;
};
} // namespace MsimeStats
