"""Guard the accepted-insertion hook boundaries without running the IME."""

from pathlib import Path
import re

root = Path(__file__).resolve().parents[2]
ipc = root / "native-source/server/src/ipc"
keys = (ipc / "event_listener_keys.cpp").read_text(encoding="utf-8")
listener = (ipc / "event_listener.cpp").read_text(encoding="utf-8")

start = keys.index("bool ApplyCompositionEditKey(")
end = keys.index("\n}\n", start) + 2
edit = keys[start:end]
insert = edit.index("        raw.insert(")
notify = edit.index("        CandidateTypingStars::NotifyInserted();")
assert edit.count("CandidateTypingStars::NotifyInserted();") == 1
assert insert < notify < edit.index("composition.note_raw_inserted();")
assert re.search(
    r"raw\.insert\([^\n]+\);\s*\+\+composition\.caret_position;\s*"
    r"CandidateTypingStars::NotifyInserted\(\);",
    edit,
)
# No deletion, caret move or restore path may publish a typed-character event.
assert "CandidateTypingStars::NotifyInserted" not in edit[:insert]
assert "if (keycode == VK_BACK)" in edit[:insert]
assert "if (keycode == VK_LEFT || keycode == VK_RIGHT)" in edit[:insert]
assert "return true;" in edit[edit.rindex("if (input ==", 0, insert):insert]

# The only other accepted insertion is the display-only temporary R prefix.
assert keys.count("CandidateTypingStars::NotifyInserted();") == 2
r_prefix = keys[keys.index("    if (r_mode_trigger_key)\n"):]
r_prefix = r_prefix[:r_prefix.index("\n    }")]
assert r_prefix.index("g_r_mode_triggered = true;") < r_prefix.index(
    "CandidateTypingStars::NotifyInserted();"
)
assert re.search(
    r"if \(!is_composition_edit_key \|\| Global::Keycode == VK_BACK \|\| "
    r"Global::Keycode == VK_DELETE \|\|\s*Global::Keycode == VK_LEFT \|\| "
    r"Global::Keycode == VK_RIGHT\)\s*\{\s*CandidateTypingStars::BreakSequence\(\);",
    keys,
)
# Editing and navigation do not advance the visual generation or clear bursts.
assert "CandidateTypingStars::Reset();" not in keys
assert re.search(r"void ClearState\(\)\s*\{\s*CandidateTypingStars::Reset\(\);", listener)

# UI messages may lag behind a new composition; only the worker resets counts.
presenter = (ipc.parent / "window/candidate_presenter.cpp").read_text(encoding="utf-8")
assert "CandidateTypingStars::Reset" not in presenter
assert "*impl_->typingStarEpoch != typingEvents.epoch" in presenter

# No asynchronous candidate refresh or rendering path creates typing events.
for candidate_file in ipc.glob("event_listener*candidates.cpp"):
    assert "CandidateTypingStars::NotifyInserted" not in candidate_file.read_text(encoding="utf-8")

print("PASS: ignored separators, deletion/restore/caret/navigation break only pairing, R-prefix, ClearState resets generation, async boundaries")
