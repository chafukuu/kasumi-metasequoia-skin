#pragma once

#include <string>

// Assembles what a punctuation keystroke inserts while a create-word composition is alive.
//
// During create words the composition range holds 已选汉字 + 剩余原始输入, but the selected Han
// prefix only exists in GlobalIme::word_for_creating_word: the Server's highlighted-candidate
// reply, the prefetched punctuation and the smart-punctuation fallback all describe the remaining
// raw part alone. Inserting any of them replaces the whole range, so the selection disappears
// with it (issue #747). VK_RETURN already prepends the same prefix in
// CMetasequoiaIME::_HandleCandidateFinalizeForVKReturn.
//
// Only the inserted text gains the prefix. The caller keeps passing the unprefixed string to the
// paired-punctuation step-over, the reversible-space fingerprint and
// _NoteCommittedChinesePunctuation, so the character a punctuation key resolves to stays decided by
// the candidate and the document rather than by the selected prefix.
//
// Kept free of TSF and Windows headers so it can be unit tested on its own
// (windows/tests/punctuation_commit_text.cpp).
inline std::wstring BuildPunctuationCommitText(const std::wstring &creatingWordPrefix,
                                               const std::wstring &punctuationText)
{
    if (creatingWordPrefix.empty())
    {
        return punctuationText;
    }
    return creatingWordPrefix + punctuationText;
}
