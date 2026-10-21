#pragma once

namespace FanyImeUi
{
// A temporary thread-focus suspension (for example Win+.) does not change
// ime_active. Only terminal TIP activation/deactivation changes that bit, so
// the toolbar stays resident across auxiliary Windows input surfaces but is
// hidden after the user switches to another input method. An auto-hidden
// toolbar stays hidden through focus changes until an input state change or a
// settings change clears auto_hidden.
constexpr bool ShouldShowFloatingToolbar(bool configured_enabled, bool fullscreen, bool ime_active, bool auto_hidden)
{
    return configured_enabled && !fullscreen && ime_active && !auto_hidden;
}
} // namespace FanyImeUi
