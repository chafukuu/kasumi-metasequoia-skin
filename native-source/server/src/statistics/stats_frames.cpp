#include "statistics/stats_frames.h"

#include <chrono>
#include <cstddef>

namespace MsimeStats
{
namespace
{
constexpr size_t kHeaderSize = sizeof(FanyImeStatsBatchHeader);
constexpr size_t kEventSize = sizeof(FanyImeStatsEvent);

// 1601-01-01 to 1970-01-01.
constexpr uint64_t kFiletimeTicksPerSecond = 10'000'000ULL;
constexpr uint64_t kFiletimeUnixEpochOffsetSeconds = 11'644'473'600ULL;
constexpr uint64_t kFiletimeUnixEpochTicks = kFiletimeUnixEpochOffsetSeconds * kFiletimeTicksPerSecond;

// The wire format is little-endian; decode byte by byte instead of reinterpreting
// the contract struct so the byte order never depends on the host.
uint16_t ReadU16(const uint8_t *data)
{
    return static_cast<uint16_t>(static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8));
}

uint32_t ReadU32(const uint8_t *data)
{
    return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

uint64_t ReadU64(const uint8_t *data)
{
    return static_cast<uint64_t>(ReadU32(data)) | (static_cast<uint64_t>(ReadU32(data + 4)) << 32);
}
} // namespace

const char *FrameErrorToString(FrameError error)
{
    switch (error)
    {
    case FrameError::None:
        return "ok";
    case FrameError::ShortHeader:
        return "frame shorter than its header";
    case FrameError::BadMagic:
        return "bad magic";
    case FrameError::BadVersion:
        return "unsupported version";
    case FrameError::BadHeaderSize:
        return "unexpected header size";
    case FrameError::PayloadTooLarge:
        return "payload exceeds max frame size";
    case FrameError::BadPayloadSize:
        return "payload size is not a multiple of the event size";
    case FrameError::EventCountMismatch:
        return "event count does not match payload size";
    case FrameError::TrailingBytes:
        return "frame size does not match payload size";
    case FrameError::SourceProcessMismatch:
        return "source process id does not match the pipe client";
    }
    return "unknown frame error";
}

FrameError ValidateFrameHeader(const FanyImeStatsBatchHeader &header)
{
    if (header.magic != FANY_IME_STATS_MAGIC)
    {
        return FrameError::BadMagic;
    }
    if (header.version != FANY_IME_STATS_VERSION)
    {
        return FrameError::BadVersion;
    }
    if (header.header_size != kHeaderSize)
    {
        return FrameError::BadHeaderSize;
    }
    if (header.payload_bytes > FANY_IME_STATS_MAX_FRAME_BYTES - kHeaderSize)
    {
        return FrameError::PayloadTooLarge;
    }
    if (header.payload_bytes % kEventSize != 0)
    {
        return FrameError::BadPayloadSize;
    }
    if (header.event_count != header.payload_bytes / kEventSize)
    {
        return FrameError::EventCountMismatch;
    }
    return FrameError::None;
}

FrameError ValidateFrameSource(const FanyImeStatsBatchHeader &header, uint32_t client_process_id)
{
    return header.source_process_id == client_process_id ? FrameError::None : FrameError::SourceProcessMismatch;
}

FrameError DecodeFrame(const uint8_t *data, size_t size, FanyImeStatsBatchHeader &header,
                       std::vector<FanyImeStatsEvent> &events)
{
    events.clear();
    if (data == nullptr || size < kHeaderSize)
    {
        return FrameError::ShortHeader;
    }

    header.magic = ReadU32(data + offsetof(FanyImeStatsBatchHeader, magic));
    header.version = ReadU32(data + offsetof(FanyImeStatsBatchHeader, version));
    header.header_size = ReadU32(data + offsetof(FanyImeStatsBatchHeader, header_size));
    header.payload_bytes = ReadU32(data + offsetof(FanyImeStatsBatchHeader, payload_bytes));
    header.event_count = ReadU32(data + offsetof(FanyImeStatsBatchHeader, event_count));
    header.dropped_count = ReadU32(data + offsetof(FanyImeStatsBatchHeader, dropped_count));
    header.source_process_id = ReadU32(data + offsetof(FanyImeStatsBatchHeader, source_process_id));

    const FrameError validation = ValidateFrameHeader(header);
    if (validation != FrameError::None)
    {
        return validation;
    }
    if (size != kHeaderSize + static_cast<size_t>(header.payload_bytes))
    {
        return FrameError::TrailingBytes;
    }

    events.reserve(header.event_count);
    for (uint32_t index = 0; index < header.event_count; ++index)
    {
        const uint8_t *raw = data + kHeaderSize + static_cast<size_t>(index) * kEventSize;
        FanyImeStatsEvent event;
        event.timestamp_utc_ft = ReadU64(raw + offsetof(FanyImeStatsEvent, timestamp_utc_ft));
        event.cjk = ReadU16(raw + offsetof(FanyImeStatsEvent, cjk));
        event.latin = ReadU16(raw + offsetof(FanyImeStatsEvent, latin));
        event.digit = ReadU16(raw + offsetof(FanyImeStatsEvent, digit));
        event.punct = ReadU16(raw + offsetof(FanyImeStatsEvent, punct));
        event.other = ReadU16(raw + offsetof(FanyImeStatsEvent, other));
        events.push_back(event);
    }
    return FrameError::None;
}

TimePoint FiletimeToTime(uint64_t filetime_utc_ft)
{
    if (filetime_utc_ft < kFiletimeUnixEpochTicks)
    {
        return TimePoint{};
    }
    const uint64_t seconds = filetime_utc_ft / kFiletimeTicksPerSecond - kFiletimeUnixEpochOffsetSeconds;
    const uint64_t remainder_ticks = filetime_utc_ft % kFiletimeTicksPerSecond;
    const auto since_epoch = std::chrono::duration_cast<std::chrono::system_clock::duration>(
        std::chrono::seconds(static_cast<int64_t>(seconds)) +
        std::chrono::nanoseconds(static_cast<int64_t>(remainder_ticks) * 100));
    return TimePoint(since_epoch);
}

uint64_t TimeToFiletime(TimePoint time)
{
    using HundredNanoseconds = std::chrono::duration<int64_t, std::ratio<1, 10'000'000>>;
    const int64_t ticks = std::chrono::duration_cast<HundredNanoseconds>(time.time_since_epoch()).count();
    return static_cast<uint64_t>(ticks + static_cast<int64_t>(kFiletimeUnixEpochTicks));
}

int EventTotalChars(const FanyImeStatsEvent &event)
{
    return static_cast<int>(event.cjk) + static_cast<int>(event.latin) + static_cast<int>(event.digit) +
           static_cast<int>(event.punct) + static_cast<int>(event.other);
}
} // namespace MsimeStats
