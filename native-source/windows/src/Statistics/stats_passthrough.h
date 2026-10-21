#pragma once

#include <cwchar>

// The three composition commit exits only see text this tip writes into the
// document. A key it does not consume is inserted by the host itself, so
// half-width digits and the symbols outside the punctuation table never reach
// those exits -- the digit class stayed at zero before this hook existed
// (2026-09). This predicate decides whether one such key is counted, and it
// stays pure so the rules can be unit tested without a TSF host.
//
// The count is a key-time prediction, not an edit confirmation: a read-only
// document, a full maxlength or an application-level shortcut leaves the key
// counted anyway. That approximation is a product decision (design 2.4); the
// filters below only drop keys that certainly are not a character the user
// typed (modifiers, functional/dead keys, no focused edit context).

namespace MsimeStats
{
inline bool ShouldCountPassthroughChar(wchar_t wch, bool eaten, bool selfGenerated, bool keyboardDisabled,
                                       bool ctrlDown, bool altDown, bool winDown)
{
    // Shift stays allowed on purpose: it is what makes uppercase letters and
    // the shifted symbol row their own characters. Functional keys and dead
    // keys produce no printable wch, so the range check -- not a name list --
    // keeps Enter/Tab/Backspace and friends out. 0x7F is DEL, the one control
    // character that sits above the printable range.
    return !eaten && !selfGenerated && !keyboardDisabled && !ctrlDown && !altDown && !winDown && wch >= 0x20 &&
           wch != 0x7F;
}
} // namespace MsimeStats
