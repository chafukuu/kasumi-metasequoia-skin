#include "../../engine/contracts/input_state_shortcuts.h"
#include "../../engine/contracts/ipc_negotiation.h"
#include <iostream>
#include <fstream>
#include <sstream>
#define CHECK(expression)                                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expression))                                                                                             \
        {                                                                                                              \
            std::cerr << #expression << '\n';                                                                          \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (false)
std::string Fixture()
{
    using namespace FanyImeInputStateShortcuts;
    std::ostringstream out;
    const auto quote = [&out](std::string_view value) {
        out << '"';
        for (char c : value)
        {
            if (c == '"' || c == '\\')
                out << '\\';
            out << c;
        }
        out << '"';
    };
    out << "{\"actions\":[";
    for (std::size_t i = 0; i < Definitions.size(); ++i)
    {
        const auto &d = Definitions[i];
        if (i)
            out << ',';
        out << "{\"key\":";
        quote(d.key);
        out << ",\"label\":";
        quote(d.label);
        out << ",\"defaultBinding\":";
        quote(d.defaultBinding);
        out << ",\"editor\":";
        quote(d.editor);
        out << ",\"binding\":";
        quote(d.defaultBinding);
        out << ",\"problem\":\"\"}";
    }
    const auto rules = [&](std::string_view name, const auto &items) {
        out << "],";
        quote(name);
        out << ":[";
        bool first = true;
        for (const auto &rule : items)
        {
            if (!first)
                out << ',';
            first = false;
            out << "{\"binding\":";
            quote(rule.binding);
            out << ",\"label\":";
            quote(rule.label);
            out << '}';
        }
    };
    rules("reserved", Reserved);
    rules("warnings", Warnings);
    out << "],\"keys\":[";
    bool first = true;
    for (const auto &key : FanyImeKeyChord::NamedKeys)
    {
        if (!first)
            out << ',';
        first = false;
        out << "{\"code\":";
        quote(key.name);
        out << ",\"vk\":" << key.key << ",\"label\":";
        quote(key.label);
        out << '}';
    }
    out << "]}\n";
    return out.str();
}
int main()
{
    using namespace FanyImeInputStateShortcuts;
    using namespace FanyImeKeyChord;
    std::ifstream fixture(MSIME_SHORTCUT_FIXTURE_PATH);
    CHECK(std::string((std::istreambuf_iterator<char>(fixture)), {}) == Fixture());
    auto values = Defaults();
    auto resolved = Resolve(values);
    CHECK(resolved.Match('E', 3) == Action::EnglishCandidates);
    CHECK(resolved.Match(0x20, 3) == Action::FullWidth);
    CHECK(resolved.Match(0xbe, 2) == Action::Punctuation);
    for (const auto &key : NamedKeys)
        CHECK(KeyCode(key.name) == key.key);
    Binding binding;
    for (const auto value : {"Alt+KeyJ", "Alt+Shift+KeyJ", "Ctrl+Alt+Period", "Win+KeyA", "Shift+KeyA", "Ctrl+",
                             "Ctrl+F13", "Ctrl+Keya", "true"})
        CHECK(!Parse(value, binding) && binding.key == 0);
    for (const auto &rule : Reserved)
        CHECK(!Validate(2, rule.binding, values).empty());
    CHECK(!Validate(2, "Ctrl+Shift+KeyE", values).empty());
    CHECK(!Validate(2, "Ctrl+Shift+Space", values).empty());
    values[0] = "";
    values[1] = "Ctrl+Shift+KeyG";
    CHECK(Validate(2, "Ctrl+Shift+KeyE", values).empty());
    CHECK(Validate(2, "Ctrl+Shift+Space", values).empty());
    values[2] = "Ctrl+Shift+KeyG";
    resolved = Resolve(values);
    CHECK(resolved.bindings[1].key == 0 && resolved.bindings[2].key == 0);
    for (const auto value : {"Alt+KeyJ", "Alt+Shift+KeyJ"})
    {
        CHECK(!Validate(1, value, values).empty());
        values[1] = value;
        CHECK(Resolve(values).bindings[1].key == 0);
        CHECK(Resolve(values).Match('J', Alt) == static_cast<Action>(0));
        CHECK(Resolve(values).Match('J', Alt | Shift) == static_cast<Action>(0));
    }
    values[1] = "Ctrl+Shift+KeyH";
    resolved = Resolve(values);
    for (unsigned modifiers = 0; modifiers < 16; ++modifiers)
        CHECK(resolved.bindings[1].Matches('H', modifiers) == (modifiers == (Control | Shift)));
    values[2] = "invalid";
    CHECK(Resolve(values).bindings[2].key == 0);
    CHECK(Resolve(values).bindings[1].key == 'H');
    CHECK(TomlString(" 'Ctrl+Space' ") == "Ctrl+Space");
    CHECK(TomlString("true") == "!invalid");
    CHECK(Validate(2, "Ctrl+KeyC", values).empty());
    auto hello = FanyImeProtocol::Hello(1, 2, FanyImeProtocol::Capabilities | FanyImeProtocol::InputStateShortcuts);
    CHECK(!(FanyImeProtocol::Negotiate(hello).capabilities & FanyImeProtocol::InputStateShortcuts));
    CHECK(FanyImeProtocol::Negotiate(hello, FanyImeProtocol::Capabilities | FanyImeProtocol::InputStateShortcuts)
              .capabilities &
          FanyImeProtocol::InputStateShortcuts);
    CHECK(FanyImePipeEventType::InputStateAction == 17);
    CHECK(static_cast<unsigned>(Action::EnglishCandidates) == 1);
}
