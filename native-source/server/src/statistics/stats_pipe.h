#pragma once

// Session-scoped named pipe listener for input statistics frames. The wire
// contract lives in engine/contracts/windows_ipc.h; this header deliberately
// stays free of Windows types so the pipe-name helper can be asserted by a
// unit test without pulling in the listener implementation.

#include "engine/contracts/windows_ipc.h"

#include <cstdint>
#include <string>

namespace MsimeStats
{
// BuildStatsPipeName appends the decimal session id to the contract prefix.
// Named pipes live in the machine-global namespace, so concurrent sessions
// (fast user switching, RDP) must not share a listener; the TSF DLL builds the
// identical name from ProcessIdToSessionId(GetCurrentProcessId()).
inline std::wstring BuildStatsPipeName(uint32_t session_id)
{
    return std::wstring(FANY_IME_STATS_PIPE_NAME_PREFIX) + std::to_wstring(session_id);
}

// CurrentStatsSessionId returns the same session id the DLL computes. The zero
// returned when ProcessIdToSessionId fails cannot collide with a real session:
// session 0 is reserved for services.
uint32_t CurrentStatsSessionId();

// StatsPipeEventListenerLoopThread accepts one frame per connection, validates
// it against the contract, aggregates it and persists the result while the
// statistics switch is on. Frames that fail validation, and every frame while
// the switch is off, are dropped without touching the database. Runs until
// pipe_running turns false.
void StatsPipeEventListenerLoopThread();

// ShutdownStatsPipe releases the lazily opened database handle. Call after the
// listener thread has been joined; the store is only touched by that thread
// while it runs.
void ShutdownStatsPipe();
} // namespace MsimeStats
