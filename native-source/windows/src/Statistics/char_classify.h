#pragma once

#include <cstddef>
#include <cstdint>

namespace MsimeStats
{
// User-perceived character counts for one committed-text event. The wire format
// keeps each class in a 16-bit field, so accumulation saturates instead of
// wrapping; the capture path also caps a single commit well below that limit.
struct CharClassCounts
{
    uint16_t cjk = 0;
    uint16_t latin = 0;
    uint16_t digit = 0;
    uint16_t punct = 0;
    uint16_t other = 0;

    uint32_t Total() const;
    // Saturating addition, used when a long commit is read in blocks.
    void Add(const CharClassCounts &counts);
};

// Classifies UTF-16 text into the five statistics classes. A surrogate pair
// counts as one character; an unpaired surrogate counts as `other`. This runs
// inside the TSF process on purpose: sending text over the statistics pipe
// would turn it into a per-keystroke stream and invalidate the privacy
// promise, so only the resulting counts ever leave the process.
CharClassCounts ClassifyText(const wchar_t *text, size_t length);
} // namespace MsimeStats
