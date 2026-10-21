#pragma once

#include "config/ime_config.h"
#include "engine/core/word_item.h"
#include "engine/local_modes/date_time_query.h"
#include "engine/user_dictionary/user_dictionary_journal.h"
#include <string>
#include <vector>

// Shift+T 模式、混输和展开的日期页共用这一份：按唤醒词查整组日期时间候选，套上学到的顺序和固定位置。
// 混输取的就是这里排第一的格式，所以 T 模式里调到第一的格式，混输里也跟着换。来源一律记 DateTime，
// 选词、置顶、固定位置靠它认出这是按格式 ID 记的候选。调频关闭时不用学到的顺序，和英文槽位一样。
inline std::vector<WordItem> OrderedDateTimeCandidates(const std::string &keyword)
{
    auto items = metasequoia::local_modes::query_date_time(keyword);
    for (auto &item : items)
        item.source = CandidateSource::DateTime;
    user_dictionary::apply_date_time_order(user_dictionary::default_user_db_path(),
                                           metasequoia::local_modes::date_time_category(keyword), items,
                                           GetConfiguredFrequencyAdjustment().mode != "disabled");
    return items;
}

// 格式 ID 以分组名开头（"date:ymd_dash"），由它反查出这组的唤醒词，调频时要拿整组的出厂顺序。
inline std::string DateTimeKeywordForFormat(const std::string &format_id)
{
    if (format_id.rfind("date:", 0) == 0)
        return "rq";
    if (format_id.rfind("time:", 0) == 0)
        return "sj";
    if (format_id.rfind("week:", 0) == 0)
        return "xq";
    return {};
}
