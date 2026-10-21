#pragma once

#include <cstddef>
#include <string>

// Payload of the worker-pipe frame
// SendToTsfWorkerThreadClientViaNamedpipe(..., CommitCandidateAndContinue, ...).
//
// The Server sends "<consumed>\t<text>": `consumed` is the decimal number of leading characters
// this delivery replaces in the TSF keystroke buffer, `text` is the committed text (empty means
// "only consume"). The TSF side trims its own buffer with that count rather than applying a
// remainder the Server computed from a possibly stale view, so letters the user has already typed
// past the committed code survive as the next composition.
//
// The parser rejects anything malformed (no tab, empty/non-digit/oversized count) so a corrupt or
// future frame can never trim an arbitrary prefix. Kept free of TSF and Windows headers so it can
// be unit tested on its own (windows/tests/commit_candidate_and_continue_payload.cpp).
inline bool ParseCommitCandidateAndContinuePayload(const std::wstring &payload, std::size_t &out_consumed,
                                                   std::wstring &out_text)
{
    const std::size_t tab = payload.find(L'\t');
    if (tab == std::wstring::npos || tab == 0)
    {
        return false;
    }

    // The largest real buffer is a short raw spelling, so a count that cannot overflow and is far
    // beyond any real keystroke buffer is a corrupt frame, not a request to clamp.
    constexpr std::size_t kMaxConsumed = 1u << 20;
    std::size_t consumed = 0;
    for (std::size_t i = 0; i < tab; ++i)
    {
        const wchar_t ch = payload[i];
        if (ch < L'0' || ch > L'9')
        {
            return false;
        }
        consumed = consumed * 10 + static_cast<std::size_t>(ch - L'0');
        if (consumed > kMaxConsumed)
        {
            return false;
        }
    }

    out_consumed = consumed;
    out_text = payload.substr(tab + 1);
    return true;
}
