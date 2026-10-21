#pragma once

#include "defines/base_structures.h"

#include <algorithm>
#include <windows.h>

namespace FanyImeUi
{
// All coordinates are physical pixels; content excludes the host's shadow padding
inline POINT TrayMenuPosition(const RECT &icon, const RECT &content, const MonitorCoordinates &workArea)
{
    const int width = content.right - content.left;
    const int height = content.bottom - content.top;
    int x = icon.left + (icon.right - icon.left) / 2 - width / 2;
    int y = icon.top - height;
    if (y < workArea.top)
        y = icon.bottom;
    x = std::clamp(x, workArea.left, workArea.right - width);
    y = std::clamp(y, workArea.top, workArea.bottom - height);
    return {x - content.left, y - content.top};
}
} // namespace FanyImeUi
