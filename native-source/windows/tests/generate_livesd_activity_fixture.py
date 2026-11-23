"""Compile production LiveSD scheduling against a deterministic TSF/Win32 fixture."""

from pathlib import Path
import sys

from generate_reconnect_fixture import extract_body


def generate(source_path, output_path):
    source = Path(source_path).read_text(encoding="utf-8")
    markers = (
        "bool IsTypingKey(UINT key)",
        "void CMetasequoiaIME::_ObserveLiveSDTestResult(",
        "void CMetasequoiaIME::_ObserveLiveSDKey(",
        "void CMetasequoiaIME::_QueueLiveSDActivity(",
        "void CMetasequoiaIME::_RunLiveSDActivity()",
        "void CMetasequoiaIME::_CompleteLiveSDActivity(",
        "void CMetasequoiaIME::_CancelLiveSDActivity(bool sendBlur, bool keepPulses)",
        "void CMetasequoiaIME::_OnLiveSDThreadFocusLost()",
    )
    generated = "// Generated from production LiveSD handlers; do not edit.\n"
    for marker in markers:
        start = source.index(marker)
        body = extract_body(source, marker)
        opening = source.index("{", start)
        generated += source[start:opening] + body + "\n"
    Path(output_path).write_text(generated, encoding="utf-8")


if __name__ == "__main__":
    generate(*sys.argv[1:])
