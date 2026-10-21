#include "statistics/stats_aggregate.h"

#include "statistics/stats_frames.h"

#include <map>
#include <utility>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace MsimeStats
{
namespace
{
bool IsUnknownTime(TimePoint time)
{
    return time == TimePoint{};
}
} // namespace

LocalTimeParts DefaultLocalTimeResolver(TimePoint utc)
{
#ifdef _WIN32
    const uint64_t raw = TimeToFiletime(utc);
    FILETIME filetime{};
    filetime.dwLowDateTime = static_cast<DWORD>(raw & 0xFFFFFFFFULL);
    filetime.dwHighDateTime = static_cast<DWORD>(raw >> 32);

    SYSTEMTIME utc_system{};
    if (!FileTimeToSystemTime(&filetime, &utc_system))
    {
        return UtcPartsOf(utc);
    }
    SYSTEMTIME local_system{};
    const SYSTEMTIME *resolved = &local_system;
    if (!SystemTimeToTzSpecificLocalTime(nullptr, &utc_system, &local_system))
    {
        resolved = &utc_system;
    }
    LocalTimeParts parts;
    parts.year = resolved->wYear;
    parts.month = resolved->wMonth;
    parts.day = resolved->wDay;
    parts.hour = resolved->wHour;
    return parts;
#else
    // Non-Windows builds only exist for the portable unit tests, which always
    // inject their own resolver; UTC is the most honest fallback.
    return UtcPartsOf(utc);
#endif
}

int64_t ActiveDeltaMs(TimePoint prev, TimePoint cur)
{
    if (IsUnknownTime(prev) || IsUnknownTime(cur))
    {
        return 0;
    }
    const auto gap = cur - prev;
    if (gap <= std::chrono::milliseconds::zero() || gap > std::chrono::milliseconds(kActiveGapLimitMs))
    {
        return 0;
    }
    return std::chrono::duration_cast<std::chrono::milliseconds>(gap).count();
}

Accumulator::Accumulator() : local_time_(DefaultLocalTimeResolver)
{
}

Accumulator::Accumulator(LocalTimeResolver resolver) : local_time_(std::move(resolver))
{
}

Batch Accumulator::Add(const std::vector<FanyImeStatsEvent> &events)
{
    std::map<int, DayDelta> days;
    std::map<std::pair<int, int>, HourDelta> hours;

    for (const FanyImeStatsEvent &event : events)
    {
        TimePoint timestamp = FiletimeToTime(event.timestamp_utc_ft);
        if (IsUnknownTime(timestamp))
        {
            // A zero FILETIME cannot come from GetSystemTimeAsFileTime, so it
            // means a broken frame. Keep the characters (attributed to the
            // previous event's day, or to today when there is none) but never
            // treat the event as active time.
            timestamp = has_last_ ? last_ : std::chrono::system_clock::now();
        }

        int64_t active_ms = 0;
        if (has_last_ && timestamp > last_)
        {
            active_ms = ActiveDeltaMs(last_, timestamp);
            last_ = timestamp;
        }
        else if (!has_last_)
        {
            last_ = timestamp;
            has_last_ = true;
        }

        const LocalTimeParts local = local_time_(timestamp);
        const int day_key = DayKeyOf(local);
        DayDelta &day = days[day_key];
        day.day_key = day_key;
        day.cjk += event.cjk;
        day.latin += event.latin;
        day.digit += event.digit;
        day.punct += event.punct;
        day.other += event.other;
        day.active_ms += active_ms;

        const std::pair<int, int> hour_key(day_key, local.hour);
        HourDelta &hour = hours[hour_key];
        hour.day_key = day_key;
        hour.hour = local.hour;
        hour.chars += EventTotalChars(event);
        hour.active_ms += active_ms;
    }

    Batch batch;
    batch.days.reserve(days.size());
    batch.hours.reserve(hours.size());
    for (const auto &entry : days)
    {
        batch.days.push_back(entry.second);
    }
    for (const auto &entry : hours)
    {
        batch.hours.push_back(entry.second);
    }
    return batch;
}

void Accumulator::Reset()
{
    last_ = TimePoint{};
    has_last_ = false;
}

TimePoint Accumulator::LastEventTime() const
{
    return last_;
}

bool Accumulator::HasLastEvent() const
{
    return has_last_;
}
} // namespace MsimeStats
