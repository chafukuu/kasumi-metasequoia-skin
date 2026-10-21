#pragma once

// Frame decoding and validation for the input statistics pipe.
//
// The wire contract lives in engine/contracts/windows_ipc.h and the contract
// structs are used directly (no mirrored copy). A frame that does not match the
// contract exactly is rejected as a whole: the reader never tries to interpret
// an unexpected layout, which is what keeps a version skew from producing
// garbage counts. The reason codes mirror the standalone tool's sentinel errors
// (internal/frames/frames.go) so the branch lists stay comparable.

#include "engine/contracts/windows_ipc.h"
#include "statistics/stats_types.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace MsimeStats
{
enum class FrameError
{
    None = 0,
    ShortHeader,
    BadMagic,
    BadVersion,
    BadHeaderSize,
    PayloadTooLarge,
    BadPayloadSize,
    EventCountMismatch,
    TrailingBytes,
    SourceProcessMismatch,
};

const char *FrameErrorToString(FrameError error);

// ValidateFrameHeader checks every structural invariant of the contract
// header. The payload-size checks also bound what DecodeFrame will accept.
FrameError ValidateFrameHeader(const FanyImeStatsBatchHeader &header);

// ValidateFrameSource compares the header's claimed source process with the pid
// the pipe reports for its client. The decoder cannot know the pid, so the
// listener performs this check separately before accepting a decoded frame.
FrameError ValidateFrameSource(const FanyImeStatsBatchHeader &header, uint32_t client_process_id);

// DecodeFrame validates and decodes one complete in-memory frame. `size` must
// be exactly sizeof(header) + header.payload_bytes: the one-shot pipe read
// guarantees the whole frame was written before decoding starts, so any other
// size means the frame is malformed. On failure the `header` out-parameter
// keeps whatever was decoded and `events` is left empty.
FrameError DecodeFrame(const uint8_t *data, size_t size, FanyImeStatsBatchHeader &header,
                       std::vector<FanyImeStatsEvent> &events);

// FiletimeToTime converts a raw FILETIME value (100ns ticks since 1601-01-01
// UTC) to a UTC time point. Zero and any value before the Unix epoch yield the
// TimePoint{} sentinel, which downstream code treats as "unknown" and never
// counts as active time.
TimePoint FiletimeToTime(uint64_t filetime_utc_ft);

// TimeToFiletime is the inverse of FiletimeToTime. Sub-100ns precision is
// truncated, which is exact for the system clock the DLL samples.
uint64_t TimeToFiletime(TimePoint time);

// EventTotalChars returns the number of user-perceived characters in an event.
int EventTotalChars(const FanyImeStatsEvent &event);
} // namespace MsimeStats
