#pragma once

#include <string>
#include <string_view>

namespace FanyImeSwitchLanguageHotkeys
{
struct LegacyBindings
{
    bool shift = false;
    bool ctrl = false;
    bool ctrlAltSpace = false;
};

inline bool HasLegacyArrayItem(std::string_view raw, std::string_view item)
{
    const std::string doubleQuoted = "\"" + std::string(item) + "\"";
    const std::string singleQuoted = "'" + std::string(item) + "'";
    return raw.find(doubleQuoted) != std::string_view::npos || raw.find(singleQuoted) != std::string_view::npos;
}

inline LegacyBindings ParseLegacyArray(std::string_view raw)
{
    return {HasLegacyArrayItem(raw, "Shift"), HasLegacyArrayItem(raw, "Ctrl"),
            HasLegacyArrayItem(raw, "Ctrl+Alt+Space") || HasLegacyArrayItem(raw, "Ctrl+Space")};
}
} // namespace FanyImeSwitchLanguageHotkeys
