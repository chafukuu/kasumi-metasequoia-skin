#pragma once

// Derived metrics for the statistics settings page: cards, streaks, speeds,
// category split and the today-by-hour distribution. A direct port of the
// standalone tool's internal/stats/overview.go; the frontend only formats what
// comes out of here.

#include "statistics/stats_types.h"

#include <vector>

namespace MsimeStats
{
// ComputeOverview derives every metric from the stored rows.
//
// `rows` must be the full stats_daily table; `today_hourly` may be empty and
// backs the today-by-hour chart (buckets of other days are ignored).
// `today_day_key` is the local calendar day of "now"; the caller resolves it
// because only the caller knows the process's timezone.
Overview ComputeOverview(const std::vector<DailyRow> &rows, const std::vector<HourlyRow> &today_hourly,
                         int today_day_key);

// CurrentStreak counts consecutive recorded days ending today, or ending
// yesterday when today has no record yet (the day is still in progress, so it
// must not reset the streak).
int CurrentStreak(const std::vector<DailyRow> &rows, int today_day_key);

// LongestStreak returns the longest run of consecutive recorded days.
int LongestStreak(const std::vector<DailyRow> &rows);
} // namespace MsimeStats
