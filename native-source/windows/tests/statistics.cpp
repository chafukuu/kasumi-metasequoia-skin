#include "Statistics/char_classify.h"
#include "Statistics/stats_collector.h"
#include "Statistics/stats_passthrough.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace
{
int gFirstFailure = 0;

void Check(int caseId, bool condition)
{
    // Do not use assert: Release builds must execute these checks too.
    if (!condition && gFirstFailure == 0)
    {
        gFirstFailure = caseId;
    }
}

void ExpectClassification(int caseId, const wchar_t *text, size_t length, uint16_t cjk, uint16_t latin, uint16_t digit,
                          uint16_t punct, uint16_t other)
{
    const MsimeStats::CharClassCounts actual = MsimeStats::ClassifyText(text, length);
    const bool matches = actual.cjk == cjk && actual.latin == latin && actual.digit == digit && actual.punct == punct &&
                         actual.other == other;
    Check(caseId, matches);
}

FanyImeStatsEvent MakeEvent(uint64_t timestamp, uint16_t cjk)
{
    FanyImeStatsEvent event;
    event.timestamp_utc_ft = timestamp;
    event.cjk = cjk;
    return event;
}

void TestClassification()
{
    using MsimeStats::ClassifyText;

    Check(1, ClassifyText(nullptr, 8).Total() == 0);
    Check(2, ClassifyText(L"ignored", 0).Total() == 0);

    ExpectClassification(3, L"\u4F60\u597D", 2, 2, 0, 0, 0, 0);             // ideographs
    ExpectClassification(4, L"abcXYZ", 6, 0, 6, 0, 0, 0);                   // ASCII letters
    ExpectClassification(5, L"0123456789", 10, 0, 0, 10, 0, 0);             // ASCII digits
    ExpectClassification(6, L"\uFF0C\u3002\uFF01\uFF1F", 4, 0, 0, 0, 4, 0); // fullwidth punctuation
    ExpectClassification(7, L" ", 1, 0, 0, 0, 1, 0);                        // ASCII space
    ExpectClassification(8, L"\u3000", 1, 0, 0, 0, 1, 0);                   // ideographic space
    ExpectClassification(9, L"\uFF10\uFF11\uFF12", 3, 0, 0, 3, 0, 0);       // fullwidth digits
    ExpectClassification(10, L"\uFF21", 1, 0, 0, 0, 0, 1);                  // fullwidth Latin letter
    ExpectClassification(11, L"\uF900", 1, 1, 0, 0, 0, 0);                  // compatibility ideograph

    // U+20000 (CJK Extension B) is a surrogate pair and must count once as CJK.
    const wchar_t extensionB[] = {static_cast<wchar_t>(0xD840), static_cast<wchar_t>(0xDC00)};
    ExpectClassification(12, extensionB, 2, 1, 0, 0, 0, 0);

    // Emoji: one user-perceived character, classified as other.
    const wchar_t emoji[] = {static_cast<wchar_t>(0xD83D), static_cast<wchar_t>(0xDE00)};
    ExpectClassification(13, emoji, 2, 0, 0, 0, 0, 1);

    const wchar_t loneHigh[] = {static_cast<wchar_t>(0xD83D)};
    const wchar_t loneLow[] = {static_cast<wchar_t>(0xDE00)};
    ExpectClassification(14, loneHigh, 1, 0, 0, 0, 0, 1);
    ExpectClassification(15, loneLow, 1, 0, 0, 0, 0, 1);

    const wchar_t highThenAscii[] = {static_cast<wchar_t>(0xD83D), L'a'};
    ExpectClassification(16, highThenAscii, 2, 0, 1, 0, 0, 1);

    const wchar_t mixed[] = {L'\u4F60',
                             L'\u597D',
                             L'\uFF0C',
                             L'a',
                             L'b',
                             L'c',
                             L'1',
                             L'2',
                             L'3',
                             static_cast<wchar_t>(0xD83D),
                             static_cast<wchar_t>(0xDE00)};
    const MsimeStats::CharClassCounts mixedCounts = ClassifyText(mixed, 11);
    Check(17, mixedCounts.cjk == 2 && mixedCounts.latin == 3 && mixedCounts.digit == 3 && mixedCounts.punct == 1 &&
                  mixedCounts.other == 1);
    Check(18, mixedCounts.Total() == 10);

    // Counts are 16-bit on the wire: a run longer than 65535 characters must
    // saturate instead of wrapping back to zero.
    const std::wstring longRun(70000, L'\u4E2D');
    const MsimeStats::CharClassCounts saturated = ClassifyText(longRun.data(), longRun.size());
    Check(19, saturated.cjk == 0xFFFF && saturated.Total() == 0xFFFF);

    MsimeStats::CharClassCounts sum;
    sum.Add(mixedCounts);
    sum.Add(mixedCounts);
    Check(20, sum.Total() == 20 && sum.cjk == 4 && sum.other == 2);
}

void TestPassthroughPredicate()
{
    using MsimeStats::ShouldCountPassthroughChar;

    // A plain half-width digit in Chinese mode with no modifiers is the case
    // this predicate exists for: the tip does not eat it, the host inserts it,
    // and no commit exit ever sees it (digit stayed at zero before this hook).
    Check(301, ShouldCountPassthroughChar(L'1', false, false, false, false, false, false));

    // The printable range boundaries. Functional keys arrive with a control
    // character from ToUnicode (Enter 0x0D, Tab 0x09, Backspace 0x08, Esc
    // 0x1B) or with 0, so the range check -- not a key name list -- drops them.
    Check(302, !ShouldCountPassthroughChar(L'\0', false, false, false, false, false, false));
    Check(303, !ShouldCountPassthroughChar(static_cast<wchar_t>(0x1F), false, false, false, false, false, false));
    Check(304, ShouldCountPassthroughChar(static_cast<wchar_t>(0x20), false, false, false, false, false, false));
    Check(305, ShouldCountPassthroughChar(static_cast<wchar_t>(0x7E), false, false, false, false, false, false));
    Check(306, !ShouldCountPassthroughChar(static_cast<wchar_t>(0x7F), false, false, false, false, false, false));
    Check(307, !ShouldCountPassthroughChar(L'\r', false, false, false, false, false, false));
    Check(308, !ShouldCountPassthroughChar(L'\t', false, false, false, false, false, false));
    Check(309, !ShouldCountPassthroughChar(L'\b', false, false, false, false, false, false));
    Check(310, !ShouldCountPassthroughChar(L'\x1B', false, false, false, false, false, false));

    // Every single filter blocks the count on its own.
    Check(311, !ShouldCountPassthroughChar(L'a', true, false, false, false, false, false)); // eaten
    Check(312, !ShouldCountPassthroughChar(L'a', false, true, false, false, false, false)); // self-generated
    Check(313, !ShouldCountPassthroughChar(L'a', false, false, true, false, false, false)); // keyboard disabled
    Check(314, !ShouldCountPassthroughChar(L'a', false, false, false, true, false, false)); // Ctrl
    Check(315, !ShouldCountPassthroughChar(L'a', false, false, false, false, true, false)); // Alt
    Check(316, !ShouldCountPassthroughChar(L'a', false, false, false, false, false, true)); // Win

    // Shift is deliberately not a parameter: uppercase letters and the
    // shifted symbol row still count (the character already carries Shift).
    Check(317, ShouldCountPassthroughChar(L'A', false, false, false, false, false, false));
    Check(318, ShouldCountPassthroughChar(L'!', false, false, false, false, false, false));
    Check(319, ShouldCountPassthroughChar(L' ', false, false, false, false, false, false));
    Check(320, ShouldCountPassthroughChar(L'+', false, false, false, false, false, false));

    // A non-ASCII keyboard layout still produces one countable character.
    Check(321, ShouldCountPassthroughChar(L'\u4F60', false, false, false, false, false, false));

    // The symbols this hook recovers outside the punctuation table classify as
    // punct, which is what the digit fix must show in the aggregate.
    ExpectClassification(322, L"+-=/|", 5, 0, 0, 0, 5, 0);
}

void TestEventQueue()
{
    MsimeStats::StatsEventQueue queue;
    FanyImeStatsEvent popped[MsimeStats::StatsEventQueue::kCapacity] = {};

    Check(101, queue.PopBatch(popped, MsimeStats::StatsEventQueue::kCapacity) == 0);
    Check(102, queue.PopBatch(nullptr, 4) == 0);

    for (uint64_t index = 0; index < MsimeStats::StatsEventQueue::kCapacity; ++index)
    {
        Check(103, queue.Push(MakeEvent(index, 1)));
    }
    Check(104, queue.Size() == MsimeStats::StatsEventQueue::kCapacity);

    // One event more than the capacity: the oldest is evicted, the newest is
    // retained, and the loss is counted exactly once.
    Check(105, !queue.Push(MakeEvent(1000, 5)));
    Check(106, queue.Size() == MsimeStats::StatsEventQueue::kCapacity);
    Check(107, queue.TakeDroppedCount() == 1);
    Check(108, queue.TakeDroppedCount() == 0);

    const size_t taken = queue.PopBatch(popped, MsimeStats::StatsEventQueue::kCapacity);
    Check(109, taken == MsimeStats::StatsEventQueue::kCapacity);
    Check(110, popped[0].timestamp_utc_ft == 1); // event 0 was evicted
    Check(111, popped[taken - 1].timestamp_utc_ft == 1000);
    Check(112, queue.Size() == 0);

    for (uint64_t index = 0; index < 3; ++index)
    {
        queue.Push(MakeEvent(index + 10, 1));
    }
    Check(113, queue.PopBatch(popped, 2) == 2);
    Check(114, popped[0].timestamp_utc_ft == 10 && popped[1].timestamp_utc_ft == 11);
    Check(115, queue.Size() == 1);
    Check(116, queue.PopBatch(popped, MsimeStats::StatsEventQueue::kCapacity) == 1);
    Check(117, popped[0].timestamp_utc_ft == 12);

    // The drop counter saturates instead of wrapping.
    queue.AddDroppedCount(0xFFFFFFFFu);
    queue.AddDroppedCount(7);
    Check(118, queue.TakeDroppedCount() == 0xFFFFFFFFu);
}

void TestWireContract()
{
    Check(201, sizeof(FanyImeStatsBatchHeader) == 28);
    Check(202, sizeof(FanyImeStatsEvent) == 24);
    Check(203, offsetof(FanyImeStatsEvent, timestamp_utc_ft) == 0);
    Check(204, offsetof(FanyImeStatsEvent, cjk) == 8);
    Check(205, FANY_IME_STATS_MAGIC == 0x54415453);
    Check(206, FANY_IME_STATS_VERSION == 1);

    const FanyImeStatsBatchHeader header;
    Check(207, header.magic == FANY_IME_STATS_MAGIC && header.version == FANY_IME_STATS_VERSION);
    Check(208, header.header_size == 28 && header.payload_bytes == 0 && header.event_count == 0 &&
                   header.dropped_count == 0 && header.source_process_id == 0);

    // A completely full queue has to fit into one frame.
    Check(209, sizeof(FanyImeStatsBatchHeader) + MsimeStats::StatsEventQueue::kCapacity * sizeof(FanyImeStatsEvent) <=
                   FANY_IME_STATS_MAX_FRAME_BYTES);
}
} // namespace

int main()
{
    TestWireContract();
    TestClassification();
    TestPassthroughPredicate();
    TestEventQueue();
    return gFirstFailure;
}
