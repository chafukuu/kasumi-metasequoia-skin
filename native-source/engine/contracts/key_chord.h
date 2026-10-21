#pragma once
#include <string_view>

namespace FanyImeKeyChord
{
constexpr unsigned Shift = 1, Control = 2, Alt = 4, Win = 8;
struct Binding
{
    unsigned key = 0, modifiers = 0;
    constexpr bool Matches(unsigned code, unsigned pressed) const
    {
        return key != 0 && key == code && modifiers == pressed;
    }
};
struct KeyName
{
    std::string_view name;
    unsigned key;
    std::string_view label;
};
constexpr KeyName NamedKeys[] = {{"Space", 0x20, "Space"},  {"Semicolon", 0xba, ";"},    {"Equal", 0xbb, "="},
                                 {"Comma", 0xbc, ","},      {"Minus", 0xbd, "-"},        {"Period", 0xbe, "."},
                                 {"Slash", 0xbf, "/"},      {"Backquote", 0xc0, "`"},    {"BracketLeft", 0xdb, "["},
                                 {"Backslash", 0xdc, "\\"}, {"BracketRight", 0xdd, "]"}, {"Quote", 0xde, "'"}};
inline constexpr unsigned KeyCode(std::string_view code)
{
    if (code.size() == 4 && code.substr(0, 3) == "Key" && code[3] >= 'A' && code[3] <= 'Z')
        return code[3];
    if (code.size() == 6 && code.substr(0, 5) == "Digit" && code[5] >= '0' && code[5] <= '9')
        return code[5];
    if (code.size() == 2 && code[0] == 'F' && code[1] >= '1' && code[1] <= '9')
        return 0x70 + code[1] - '1';
    if (code == "F10" || code == "F11" || code == "F12")
        return 0x79 + code[2] - '0';
    for (const auto &entry : NamedKeys)
        if (entry.name == code)
            return entry.key;
    return 0;
}
// Canonical Ctrl [+Shift] + one Windows virtual key. Empty disables.
inline constexpr bool Parse(std::string_view value, Binding &result)
{
    result = {};
    if (value.empty())
        return true;
    if (value.substr(0, 5) != "Ctrl+")
        return false;
    unsigned modifiers = Control;
    value.remove_prefix(5);
    if (value.substr(0, 6) == "Shift+")
    {
        modifiers |= Shift;
        value.remove_prefix(6);
    }
    const unsigned key = KeyCode(value);
    if (!key)
        return false;
    result = {key, modifiers};
    return true;
}
// Stands in for a configured value that is not a string; Parse rejects it.
constexpr std::string_view InvalidValue = "!invalid";
// Used by the DLL's existing small TOML reader; invalid types stay invalid.
inline std::string_view TomlString(std::string_view value)
{
    const auto begin = value.find_first_not_of(" \t\r");
    if (begin == std::string_view::npos)
        return InvalidValue;
    value = value.substr(begin, value.find_last_not_of(" \t\r") - begin + 1);
    if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') && value.back() == value.front())
        return value.substr(1, value.size() - 2);
    return InvalidValue;
}
} // namespace FanyImeKeyChord
