#include "shuangpin_preedit_display.h"

#include <algorithm>

namespace
{
struct Segment
{
    std::size_t start = 0;
    std::size_t length = 0;
};

std::vector<Segment> SplitSegments(const std::string &text)
{
    std::vector<Segment> segments;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= text.size(); ++i)
    {
        if (i == text.size() || text[i] == '\'')
        {
            segments.push_back({start, i - start});
            start = i + 1;
        }
    }
    return segments;
}

// 和 GetPreeditWithCaretMarker 同一套字母映射：原串与原串切分只差分词符号，按光标前的字母数对齐。
std::size_t MapRawCaretByLetters(const std::string &raw, std::size_t caret, const std::string &segmentation)
{
    caret = (std::min)(caret, raw.size());
    std::size_t letters_before_caret = 0;
    for (std::size_t i = 0; i < caret; ++i)
    {
        if (raw[i] != '\'')
        {
            ++letters_before_caret;
        }
    }
    std::size_t position = 0;
    std::size_t seen_letters = 0;
    while (position < segmentation.size() && seen_letters < letters_before_caret)
    {
        if (segmentation[position] != '\'')
        {
            ++seen_letters;
        }
        ++position;
    }
    if (caret > 0 && raw[caret - 1] == '\'')
    {
        while (position < segmentation.size() && segmentation[position] == '\'')
        {
            ++position;
        }
    }
    return position;
}

std::size_t MapSegmentationCaret(std::size_t position, const std::vector<Segment> &raw_segments, std::size_t raw_size,
                                 const std::vector<Segment> &quanpin_segments, std::size_t quanpin_size)
{
    if (raw_segments.size() != quanpin_segments.size())
    {
        return position >= raw_size ? quanpin_size : (std::min)(position, quanpin_size);
    }
    for (std::size_t i = 0; i < raw_segments.size(); ++i)
    {
        const Segment &raw = raw_segments[i];
        if (position > raw.start + raw.length)
        {
            continue;
        }
        const Segment &quanpin = quanpin_segments[i];
        const std::size_t offset = position > raw.start ? position - raw.start : 0;
        return quanpin.start + (offset >= raw.length ? quanpin.length : (std::min)(offset, quanpin.length));
    }
    return quanpin_size;
}
} // namespace

ShuangpinQuanpinPreedit BuildShuangpinQuanpinPreedit(const std::string &raw, const std::string &raw_segmentation,
                                                     const std::string &quanpin_segmentation, bool keep_separators)
{
    const std::vector<Segment> raw_segments = SplitSegments(raw_segmentation);
    const std::vector<Segment> quanpin_segments = SplitSegments(quanpin_segmentation);

    ShuangpinQuanpinPreedit preedit;
    preedit.caret_map.reserve(raw.size() + 1);
    for (std::size_t caret = 0; caret <= raw.size(); ++caret)
    {
        const std::size_t position = MapRawCaretByLetters(raw, caret, raw_segmentation);
        preedit.caret_map.push_back(MapSegmentationCaret(position, raw_segments, raw_segmentation.size(),
                                                         quanpin_segments, quanpin_segmentation.size()));
    }

    if (keep_separators)
    {
        preedit.text = quanpin_segmentation;
        return preedit;
    }
    // 去掉分词符号后，每个位置前移它前面的分词符号个数。
    std::vector<std::size_t> separators_before(quanpin_segmentation.size() + 1, 0);
    for (std::size_t i = 0; i < quanpin_segmentation.size(); ++i)
    {
        const bool separator = quanpin_segmentation[i] == '\'';
        separators_before[i + 1] = separators_before[i] + (separator ? 1 : 0);
        if (!separator)
        {
            preedit.text.push_back(quanpin_segmentation[i]);
        }
    }
    for (std::size_t &position : preedit.caret_map)
    {
        position -= separators_before[position];
    }
    return preedit;
}
