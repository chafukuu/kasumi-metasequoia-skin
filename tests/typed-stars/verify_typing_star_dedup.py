"""Verify duplicate candidate frames still deliver decorative input events."""

from pathlib import Path

root = Path(__file__).resolve().parents[2]
window = root / "native-source/server/src/window"
presenter = (window / "candidate_presenter.cpp").read_text(encoding="utf-8")
host = (window / "ime_windows_candidate.cpp").read_text(encoding="utf-8")


def function(source: str, signature: str) -> str:
    start = source.index(signature)
    return source[start:source.index("\n}\n", start) + 2]


dedup_start = host.index("frameSignature == g_last_rendered_candidate_signature")
dedup = host[dedup_start:host.index("return 0;", dedup_start)]
assert "PresentTypedStarsFromInput();" in dedup
assert "IsCandidateHostPaintedVisible(hwnd)" in dedup
assert "ShowFromGlobalState" not in dedup

delivery = function(presenter, "void CandidatePresenter::PresentTypedStarsFromInput()")
assert "!::is_global_wnd_cand_shown" in delivery
assert "!IsWindowVisible(hwnd_)" in delivery
assert "skinAnimates_ && SystemAnimationsEnabled()" in delivery
assert "ConsumeTypedStarInput();" in delivery
assert "if (decorationChanged || (previouslyAnimated && !impl_->resources.AreAnimationsEnabled()))" in delivery
assert "InvalidateRect(hwnd_, nullptr, FALSE);" in delivery
assert "UpdateWindow(hwnd_);" in delivery
# Synchronous painting must consume the newly invalidated decoration region.
assert delivery.index("ConsumeTypedStarInput();") < delivery.index("InvalidateRect(hwnd_, nullptr, FALSE);")
assert delivery.index("InvalidateRect(hwnd_, nullptr, FALSE);") < delivery.index("UpdateWindow(hwnd_);")
for mutation in ("Measure", "Arrange", "PlaceAndShow", "FillItemsFromUi", "ShowFromGlobalState"):
    assert mutation not in delivery

consumer = function(presenter, "bool CandidatePresenter::ConsumeTypedStarInput()")
assert "ConsumeSnapshot();" in consumer
assert "typingStarEpoch = typingEvents.epoch;" in consumer
assert "AreAnimationsEnabled()" in consumer
assert "EmitTypedStarPairs(typingEvents.bursts," in consumer
assert "*impl_->typingStarEpoch != typingEvents.epoch" in consumer
assert consumer.index("if (epochChanged)") < consumer.index("scene.ResetTypedStars();")
assert consumer.index("scene.ResetTypedStars();") < consumer.index("EmitTypedStarPairs(")
assert "return typingEvents.bursts != 0 || epochChanged;" in consumer
# UI callbacks can be stale, so input reset authority stays on the worker.
assert "CandidateTypingStars::Reset" not in presenter
show = function(presenter, "void CandidatePresenter::ShowFromGlobalState(POINT caret)")
assert "ConsumeTypedStarInput();" in show
assert presenter.count("CandidateTypingStars::ConsumeSnapshot();") == 1

print("PASS: candidate dedup delivers events, shares consumption, obeys visibility/motion gates, avoids layout mutation")
