#include "tests/includes/test_framework.h"

#include "statistics/stats_frames.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace
{
using ::FanyImeStatsBatchHeader;
using ::FanyImeStatsEvent;
using MsimeStats::FrameError;

std::vector<uint8_t> MakeFrame(const FanyImeStatsBatchHeader &header, const std::vector<FanyImeStatsEvent> &events)
{
    std::vector<uint8_t> frame(sizeof(header) + events.size() * sizeof(FanyImeStatsEvent));
    std::memcpy(frame.data(), &header, sizeof(header));
    if (!events.empty())
    {
        std::memcpy(frame.data() + sizeof(header), events.data(), events.size() * sizeof(FanyImeStatsEvent));
    }
    return frame;
}

// MutateField writes a little-endian uint32 at the given header offset in a
// copy of the frame; the caller expects the resulting frame to be rejected.
std::vector<uint8_t> MutateHeaderField(const std::vector<uint8_t> &frame, size_t offset, uint32_t value)
{
    std::vector<uint8_t> out = frame;
    out[offset + 0] = static_cast<uint8_t>(value);
    out[offset + 1] = static_cast<uint8_t>(value >> 8);
    out[offset + 2] = static_cast<uint8_t>(value >> 16);
    out[offset + 3] = static_cast<uint8_t>(value >> 24);
    return out;
}
} // namespace

// The C++ side pins the same numbers with static_assert in
// engine/contracts/windows_ipc.h. If either side is edited without the other,
// these assertions are the only thing that notices.
TEST_CASE(stats_frame_contract_layout_matches_contract)
{
    REQUIRE_EQ(sizeof(FanyImeStatsBatchHeader), 28u);
    REQUIRE_EQ(sizeof(FanyImeStatsEvent), 24u);
    REQUIRE_EQ(offsetof(FanyImeStatsBatchHeader, magic), 0u);
    REQUIRE_EQ(offsetof(FanyImeStatsBatchHeader, version), 4u);
    REQUIRE_EQ(offsetof(FanyImeStatsBatchHeader, header_size), 8u);
    REQUIRE_EQ(offsetof(FanyImeStatsBatchHeader, payload_bytes), 12u);
    REQUIRE_EQ(offsetof(FanyImeStatsBatchHeader, event_count), 16u);
    REQUIRE_EQ(offsetof(FanyImeStatsBatchHeader, dropped_count), 20u);
    REQUIRE_EQ(offsetof(FanyImeStatsBatchHeader, source_process_id), 24u);
    REQUIRE_EQ(offsetof(FanyImeStatsEvent, timestamp_utc_ft), 0u);
    REQUIRE_EQ(offsetof(FanyImeStatsEvent, cjk), 8u);
    REQUIRE_EQ(offsetof(FanyImeStatsEvent, latin), 10u);
    REQUIRE_EQ(offsetof(FanyImeStatsEvent, digit), 12u);
    REQUIRE_EQ(offsetof(FanyImeStatsEvent, punct), 14u);
    REQUIRE_EQ(offsetof(FanyImeStatsEvent, other), 16u);
}

// Golden bytes pin the wire format independently of the encoder: the expected
// slice is spelled out by hand, exactly as the DLL would write it.
TEST_CASE(stats_frame_header_golden_bytes)
{
    const std::vector<uint8_t> raw = {
        0x53, 0x54, 0x41, 0x54, // "STAT"
        0x01, 0x00, 0x00, 0x00, // version 1
        0x1c, 0x00, 0x00, 0x00, // header_size 28
        0x00, 0x00, 0x00, 0x00, // payload_bytes 0
        0x00, 0x00, 0x00, 0x00, // event_count 0
        0x03, 0x00, 0x00, 0x00, // dropped_count 3
        0x04, 0x03, 0x02, 0x01, // source_process_id 0x01020304
    };
    FanyImeStatsBatchHeader header;
    std::vector<FanyImeStatsEvent> events;
    REQUIRE(MsimeStats::DecodeFrame(raw.data(), raw.size(), header, events) == FrameError::None);
    REQUIRE_EQ(header.magic, 0x54415453u);
    REQUIRE_EQ(header.version, 1u);
    REQUIRE_EQ(header.header_size, 28u);
    REQUIRE_EQ(header.payload_bytes, 0u);
    REQUIRE_EQ(header.event_count, 0u);
    REQUIRE_EQ(header.dropped_count, 3u);
    REQUIRE_EQ(header.source_process_id, 0x01020304u);
    REQUIRE(events.empty());
}

TEST_CASE(stats_frame_event_golden_bytes)
{
    const std::vector<uint8_t> event_bytes = {
        0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, // timestamp 0x0102030405060708
        0x0b, 0x0a,                                     // cjk 0x0a0b
        0x0d, 0x0c,                                     // latin 0x0c0d
        0x0f, 0x0e,                                     // digit 0x0e0f
        0x11, 0x10,                                     // punct 0x1011
        0x13, 0x12,                                     // other 0x1213
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,             // tail padding
    };
    FanyImeStatsBatchHeader header;
    header.payload_bytes = static_cast<uint32_t>(event_bytes.size());
    header.event_count = 1;
    std::vector<uint8_t> frame = MakeFrame(header, {});
    frame.insert(frame.end(), event_bytes.begin(), event_bytes.end());

    FanyImeStatsBatchHeader got_header;
    std::vector<FanyImeStatsEvent> events;
    REQUIRE(MsimeStats::DecodeFrame(frame.data(), frame.size(), got_header, events) == FrameError::None);
    REQUIRE_EQ(events.size(), 1u);
    REQUIRE_EQ(events[0].timestamp_utc_ft, 0x0102030405060708ull);
    REQUIRE_EQ(events[0].cjk, 0x0a0bu);
    REQUIRE_EQ(events[0].latin, 0x0c0du);
    REQUIRE_EQ(events[0].digit, 0x0e0fu);
    REQUIRE_EQ(events[0].punct, 0x1011u);
    REQUIRE_EQ(events[0].other, 0x1213u);
}

TEST_CASE(stats_frame_decodes_metadata_and_events)
{
    FanyImeStatsBatchHeader header;
    header.dropped_count = 7;
    header.source_process_id = 4242;

    std::vector<FanyImeStatsEvent> events(2);
    events[0].timestamp_utc_ft = 100;
    events[0].cjk = 2;
    events[0].punct = 1;
    events[1].timestamp_utc_ft = 200;
    events[1].latin = 4;
    events[1].digit = 1;
    events[1].other = 1;
    header.payload_bytes = static_cast<uint32_t>(events.size() * sizeof(FanyImeStatsEvent));
    header.event_count = static_cast<uint32_t>(events.size());
    const std::vector<uint8_t> frame = MakeFrame(header, events);

    FanyImeStatsBatchHeader got_header;
    std::vector<FanyImeStatsEvent> got_events;
    REQUIRE(MsimeStats::DecodeFrame(frame.data(), frame.size(), got_header, got_events) == FrameError::None);
    REQUIRE_EQ(got_header.payload_bytes, 48u);
    REQUIRE_EQ(got_header.event_count, 2u);
    REQUIRE_EQ(got_header.dropped_count, 7u);
    REQUIRE_EQ(got_header.source_process_id, 4242u);
    REQUIRE_EQ(got_events.size(), 2u);
    REQUIRE_EQ(got_events[0].timestamp_utc_ft, 100ull);
    REQUIRE_EQ(got_events[0].cjk, 2u);
    REQUIRE_EQ(got_events[0].punct, 1u);
    REQUIRE_EQ(got_events[1].timestamp_utc_ft, 200ull);
    REQUIRE_EQ(got_events[1].latin, 4u);
    REQUIRE_EQ(got_events[1].digit, 1u);
    REQUIRE_EQ(got_events[1].other, 1u);
}

// A frame that fails any structural check must be rejected as a whole: the
// reader never tries to resynchronise inside a frame that may have a different
// layout.
TEST_CASE(stats_frame_bad_frames_are_rejected)
{
    FanyImeStatsBatchHeader header;
    header.payload_bytes = static_cast<uint32_t>(sizeof(FanyImeStatsEvent));
    header.event_count = 1;
    const std::vector<uint8_t> good = MakeFrame(header, std::vector<FanyImeStatsEvent>(1));

    struct Case
    {
        const char *name;
        FrameError want;
        std::vector<uint8_t> raw;
    };
    std::vector<uint8_t> trailing = good;
    trailing.push_back(0);
    const uint32_t too_large =
        static_cast<uint32_t>(FANY_IME_STATS_MAX_FRAME_BYTES - sizeof(FanyImeStatsBatchHeader) + 1);

    const std::vector<Case> cases = {
        {"short header", FrameError::ShortHeader,
         std::vector<uint8_t>(good.begin(), good.begin() + sizeof(header) - 1)},
        {"truncated payload", FrameError::TrailingBytes, std::vector<uint8_t>(good.begin(), good.end() - 1)},
        {"bad magic", FrameError::BadMagic, MutateHeaderField(good, 0, 0)},
        {"bad version", FrameError::BadVersion, MutateHeaderField(good, 4, 99)},
        {"bad header size", FrameError::BadHeaderSize, MutateHeaderField(good, 8, 32)},
        {"payload too large", FrameError::PayloadTooLarge, MutateHeaderField(good, 12, too_large)},
        {"payload not event multiple", FrameError::BadPayloadSize, MutateHeaderField(good, 12, 23)},
        {"event count mismatch", FrameError::EventCountMismatch, MutateHeaderField(good, 16, 5)},
        {"trailing bytes", FrameError::TrailingBytes, trailing},
    };

    for (const Case &test_case : cases)
    {
        FanyImeStatsBatchHeader got_header;
        std::vector<FanyImeStatsEvent> events;
        const FrameError error =
            MsimeStats::DecodeFrame(test_case.raw.data(), test_case.raw.size(), got_header, events);
        REQUIRE(error == test_case.want);
        REQUIRE(events.empty());
    }
}

TEST_CASE(stats_frame_source_process_must_match_the_pipe_client)
{
    FanyImeStatsBatchHeader header;
    header.source_process_id = 4242;
    REQUIRE(MsimeStats::ValidateFrameSource(header, 4242) == FrameError::None);
    REQUIRE(MsimeStats::ValidateFrameSource(header, 4243) == FrameError::SourceProcessMismatch);
}

TEST_CASE(stats_frame_filetime_round_trip)
{
    const MsimeStats::TimePoint now = MsimeStats::TimeFromUtcParts(2026, 9, 20, 12, 34, 56);
    const uint64_t filetime = MsimeStats::TimeToFiletime(now);
    REQUIRE(MsimeStats::FiletimeToTime(filetime) == now);
    REQUIRE(MsimeStats::FiletimeToTime(0) == MsimeStats::TimePoint{});
    REQUIRE(MsimeStats::FiletimeToTime(UINT64_MAX) != MsimeStats::TimePoint{});
}

TEST_CASE(stats_frame_event_total)
{
    FanyImeStatsEvent event;
    event.cjk = 1;
    event.latin = 2;
    event.digit = 3;
    event.punct = 4;
    event.other = 5;
    REQUIRE_EQ(MsimeStats::EventTotalChars(event), 15);
}
