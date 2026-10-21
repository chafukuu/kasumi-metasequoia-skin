#include "Key/PunctuationCommitText.h"

#include <string>

int main()
{
    // Do not use assert: Release builds must execute these checks too.

    // #747: selecting 你 out of nihao leaves hao in the composition, and the
    // highlighted candidate only answers for hao. The selected prefix has to
    // ride along with the punctuation or it is dropped with the range.
    if (BuildPunctuationCommitText(L"你", L"好，") != L"你好，")
    {
        return 1;
    }
    // Remaining raw emptied but the created word still alive: pure punctuation.
    if (BuildPunctuationCommitText(L"你", L"，") != L"你，")
    {
        return 2;
    }
    // Numpad '.' keeps its ASCII form but still carries the selection.
    if (BuildPunctuationCommitText(L"你", L".") != L"你.")
    {
        return 3;
    }
    // Nothing was selected: this must return the same bytes it was handed, so
    // every ordinary punctuation commit keeps behaving exactly as before.
    if (BuildPunctuationCommitText(L"", L"。") != L"。")
    {
        return 4;
    }
    if (BuildPunctuationCommitText(L"", L"") != L"")
    {
        return 5;
    }
    // The prefix goes in front and never replaces the punctuation: the last
    // character is still what the punctuation key resolved to, which is what
    // the paired-punctuation and reversible-space decisions read.
    const std::wstring quoted = BuildPunctuationCommitText(L"北京", L"“");
    if (quoted != L"北京“" || quoted.back() != L'“' || quoted.size() != std::wstring(L"北京").size() + 1)
    {
        return 6;
    }
    // A selection with no punctuation text still commits the selection.
    if (BuildPunctuationCommitText(L"你好", L"") != L"你好")
    {
        return 7;
    }

    return 0;
}
