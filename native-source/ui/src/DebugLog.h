#pragma once

#include <sstream>
#include <string>

namespace msimeui
{
// Logging is off unless MSIMEUI_DEBUG_LOG=1 is set: the log is appended by reopening a
// file next to the executable, which is far too slow for the paths that call it (every
// click, and TSF's GetTextExt on every composition update).
bool DebugLogEnabled();
void DebugLog(const std::string &message);
} // namespace msimeui

// Streams `expr` into a message only when logging is on, so the formatting costs nothing
// otherwise: MSIMEUI_DEBUG_LOG("point=" << x << "," << y);
#define MSIMEUI_DEBUG_LOG(expr)                                                                                        \
    do                                                                                                                 \
    {                                                                                                                  \
        if (::msimeui::DebugLogEnabled())                                                                              \
        {                                                                                                              \
            std::ostringstream msimeuiDebugLogStream_;                                                                 \
            msimeuiDebugLogStream_ << expr;                                                                            \
            ::msimeui::DebugLog(msimeuiDebugLogStream_.str());                                                         \
        }                                                                                                              \
    } while (0)
