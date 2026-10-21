#pragma once

#include "engine/core/scheme_type.h"
#include "engine/core/word_item.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// 候选混输的后台查询：英文、emoji、颜文字、符号、日期时间共用这一条线程。它们各自都只是一次小查询，
// 合在一起按一次输入查完、一次回调交回任务线程，候选窗每个按键最多因混输刷新一次。
// 专用英文模式（英文状态、Shift+Y）和候选译文仍走 EnglishIme，快捷短语在任务线程上同步混入。
namespace MixedCandidates
{
struct Request
{
    // 带大小写的原始输入，也是回调认领结果用的键。
    std::string input;
    SchemeType scheme = SchemeType::Quanpin;
    bool english = false;
    std::size_t english_min_prefix = 2;
    // 首页英文数。只显示一个时沿用原来的五行查询，多于一个才按不同词取更多补全。
    std::size_t english_first_page_count = 1;
    bool emoji = false;
    bool kaomoji = false;
    // 符号只认整串精确等于编码、且建库时标了可混输的行（alpha、szq、yi → ①），和 emoji 的前缀匹配不同。
    // 按顺序试，取第一个查得到的：双拼先放换算出的全拼（sj → san），再放原始按键；空表示这次不查。
    std::vector<std::string> symbol_codes;
    // 日期时间唤醒词（rq / riqi / sj ...），空串表示这次不查。双拼要先换算成全拼才认得出
    // shijian 这类整拼，换算依赖会话，所以在任务线程上算好再交过来。
    std::string date_time_keyword;
};

struct Result
{
    std::vector<WordItem> english;
    std::vector<WordItem> emoji;
    std::vector<WordItem> kaomoji;
    std::vector<WordItem> symbol;
    std::vector<WordItem> date_time;

    bool empty() const
    {
        return english.empty() && emoji.empty() && kaomoji.empty() && symbol.empty() && date_time.empty();
    }
};

using ApplyCallback = std::function<void(Result result, const std::string &input, uint64_t generation)>;

void Start(const std::string &english_db_path, const std::string &others_db_path, ApplyCallback apply_callback);
void Stop();
void OnInputChanged(Request request);
void Clear();
bool IsCurrent(const std::string &input, uint64_t generation);
} // namespace MixedCandidates
