#include "tests/includes/test_framework.h"
#include "engine/local_modes/date_time_query.h"

#include <algorithm>
#include <array>
#include <string>

namespace
{
using metasequoia::local_modes::LocalDateTime;

// The same instant the SYSTEMTIME version of this fixture described. Every expectation below is
// unchanged, so a divergence between the old module and the engine's port shows up as a failing
// assertion rather than as a silently different format.
LocalDateTime SampleTime()
{
    LocalDateTime value = {};
    value.year = 2026;
    value.month = 8;
    value.day = 9;
    value.weekday = 0;
    value.hour = 14;
    value.minute = 30;
    value.second = 0;
    return value;
}
} // namespace

TEST_CASE(date_time_query_accepts_all_date_wake_words)
{
    const LocalDateTime now = SampleTime();
    for (const char *keyword : std::array<const char *, 3>{"rq", "riqi", "date"})
    {
        const auto results = metasequoia::local_modes::query_date_time(keyword, &now);
        const std::array<const char *, 17> expected = {
            "2026年8月9日",
            "2026-08-09",
            "2026/08/09",
            "2026.08.09",
            "20260809",
            "26年8月9日",
            "8月9日",
            "08-09",
            "0809",
            "2026年8月9日 星期日",
            "8月9日 周日",
            "2026-08-09 Sun",
            "2026-08-09 14:30",
            "8月9日 14:30",
            "二〇二六年八月九日",
            "贰零贰陆年捌月零玖日",
            "丙午年六月二十七日",
        };
        REQUIRE_EQ(results.size(), expected.size());
        for (size_t index = 0; index < expected.size(); ++index)
            REQUIRE_EQ(results[index].word, std::string(expected[index]));
        REQUIRE(results[0].source == CandidateSource::Generated);
    }
}

TEST_CASE(date_time_query_accepts_all_time_wake_words)
{
    const LocalDateTime now = SampleTime();
    for (const char *keyword : std::array<const char *, 3>{"sj", "shijian", "time"})
    {
        const auto results = metasequoia::local_modes::query_date_time(keyword, &now);
        const std::array<const char *, 13> expected = {
            "14:30",
            "14:30:00",
            "1430",
            "143000",
            "下午2:30",
            "下午2点30分",
            "下午两点半",
            "2:30 PM",
            "2:30pm",
            "02:30:00 PM",
            "2026-08-09 14:30:00",
            "2026年8月9日 14:30",
            "8月9日 下午2:30",
        };
        REQUIRE_EQ(results.size(), expected.size());
        for (size_t index = 0; index < expected.size(); ++index)
            REQUIRE_EQ(results[index].word, std::string(expected[index]));
    }
}

TEST_CASE(date_time_query_accepts_all_week_wake_words)
{
    const LocalDateTime sunday = SampleTime();
    for (const char *keyword : std::array<const char *, 3>{"xq", "xingqi", "week"})
    {
        const auto results = metasequoia::local_modes::query_date_time(keyword, &sunday);
        const std::array<const char *, 4> expected = {
            "星期日",
            "星期天",
            "Sunday",
            "Sun",
        };
        REQUIRE_EQ(results.size(), expected.size());
        for (size_t index = 0; index < expected.size(); ++index)
            REQUIRE_EQ(results[index].word, std::string(expected[index]));
        REQUIRE(results[0].source == CandidateSource::Generated);
    }

    LocalDateTime monday = SampleTime();
    monday.weekday = 1;
    const auto results = metasequoia::local_modes::query_date_time("week", &monday);
    const std::array<const char *, 3> expected = {
        "星期一",
        "Monday",
        "Mon",
    };
    REQUIRE_EQ(results.size(), expected.size());
    for (size_t index = 0; index < expected.size(); ++index)
        REQUIRE_EQ(results[index].word, std::string(expected[index]));
}

// 调频、置顶、固定位置都按格式 ID 记，ID 必须稳定、各组内不重复，且跟着候选走。
TEST_CASE(date_time_query_tags_each_candidate_with_a_stable_format_id)
{
    const LocalDateTime now = SampleTime();
    for (const char *keyword : std::array<const char *, 3>{"rq", "sj", "xq"})
    {
        const auto ids = metasequoia::local_modes::date_time_format_ids(keyword);
        const std::string category = metasequoia::local_modes::date_time_category(keyword);
        for (size_t index = 0; index < ids.size(); ++index)
        {
            REQUIRE_EQ(ids[index].rfind(category + ":", 0), static_cast<size_t>(0));
            REQUIRE(std::count(ids.begin(), ids.end(), ids[index]) == 1);
        }
        const auto results = metasequoia::local_modes::query_date_time(keyword, &now);
        for (const auto &result : results)
            REQUIRE(std::find(ids.begin(), ids.end(), result.pinyin) != ids.end());
    }
    REQUIRE_EQ(metasequoia::local_modes::date_time_category("riqi"), std::string("date"));
    REQUIRE_EQ(metasequoia::local_modes::date_time_category("time"), std::string("time"));
    REQUIRE(metasequoia::local_modes::date_time_category("today").empty());

    // 星期天之外没有「星期天」这一条，ID 仍留在格式表里。
    LocalDateTime monday = SampleTime();
    monday.weekday = 1;
    const auto week = metasequoia::local_modes::query_date_time("xq", &monday);
    REQUIRE_EQ(week.size(), static_cast<size_t>(3));
    REQUIRE_EQ(week[1].pinyin, std::string("week:en_full"));
    REQUIRE_EQ(metasequoia::local_modes::date_time_format_ids("xq").size(), static_cast<size_t>(4));
}

// Shift+T 后面写数字：指定的日期时间沿用唤醒词的格式 ID，学到的顺序、置顶、固定位置跟着走。
TEST_CASE(date_time_query_formats_a_specific_date_or_time)
{
    const LocalDateTime now = SampleTime();
    const auto date = metasequoia::local_modes::query_date_time("20241225", &now);
    REQUIRE_EQ(date.size(), static_cast<size_t>(15));
    REQUIRE_EQ(date[0].word, std::string("2024年12月25日"));
    REQUIRE_EQ(date[9].word, std::string("2024年12月25日 星期三"));
    REQUIRE_EQ(date[0].pinyin, std::string("date:ymd_cn"));
    REQUIRE_EQ(metasequoia::local_modes::date_time_category("20241225"), std::string("date"));

    const auto month_day = metasequoia::local_modes::query_date_time("1/5", &now);
    REQUIRE_EQ(month_day[0].word, std::string("2026年1月5日"));

    const auto year_month = metasequoia::local_modes::query_date_time("2024/12", &now);
    REQUIRE_EQ(year_month.size(), static_cast<size_t>(5));
    REQUIRE_EQ(year_month[0].word, std::string("2024年12月"));
    const auto date_ids = metasequoia::local_modes::date_time_format_ids("rq");
    for (const auto &item : year_month)
        REQUIRE(std::find(date_ids.begin(), date_ids.end(), item.pinyin) != date_ids.end());
    // 年月几种在唤醒词下给不出，rq 的候选不变。
    REQUIRE_EQ(metasequoia::local_modes::query_date_time("rq", &now).size(), static_cast<size_t>(17));

    const auto time = metasequoia::local_modes::query_date_time("9:05", &now);
    REQUIRE_EQ(time[0].word, std::string("09:05"));
    REQUIRE_EQ(time[0].pinyin, std::string("time:hm"));
    REQUIRE_EQ(metasequoia::local_modes::date_time_category("9:05"), std::string("time"));

    const auto date_time = metasequoia::local_modes::query_date_time("2024122514:30", &now);
    REQUIRE_EQ(date_time[0].word, std::string("2024-12-25 14:30:00"));
    REQUIRE_EQ(metasequoia::local_modes::date_time_category("2024122514:30"), std::string("time"));

    REQUIRE(metasequoia::local_modes::is_date_time_query("2024/2/29", &now));
    REQUIRE(!metasequoia::local_modes::is_date_time_query("2023/2/29", &now));
    REQUIRE(!metasequoia::local_modes::is_date_time_query("1225", &now));
    REQUIRE(metasequoia::local_modes::query_date_time("25:00", &now).empty());
}

TEST_CASE(date_time_query_rejects_unknown_words_and_honors_limit)
{
    const LocalDateTime now = SampleTime();
    REQUIRE(!metasequoia::local_modes::is_date_time_keyword("today"));
    REQUIRE(metasequoia::local_modes::is_date_time_keyword("week"));
    REQUIRE(metasequoia::local_modes::query_date_time("today", &now).empty());
    REQUIRE(metasequoia::local_modes::query_date_time("rq", &now, 0).empty());
    REQUIRE_EQ(metasequoia::local_modes::query_date_time("rq", &now, 3).size(), static_cast<size_t>(3));
}
