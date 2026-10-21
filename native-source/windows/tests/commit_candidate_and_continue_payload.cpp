#include "IPC/CommitCandidateAndContinuePayload.h"

#include <string>

int main()
{
    // Do not use assert: Release builds must execute these checks too.
    std::size_t consumed = 0;
    std::wstring text;

    if (!ParseCommitCandidateAndContinuePayload(L"4\t数据", consumed, text) || consumed != 4 || text != L"数据")
    {
        return 1;
    }
    // Empty text still consumes: the Server mirrors a cleared composition.
    if (!ParseCommitCandidateAndContinuePayload(L"4\t", consumed, text) || consumed != 4 || !text.empty())
    {
        return 2;
    }
    // A two-digit count is legal as long as it parses.
    if (!ParseCommitCandidateAndContinuePayload(L"12\tab", consumed, text) || consumed != 12 || text != L"ab")
    {
        return 3;
    }

    // Missing tab.
    if (ParseCommitCandidateAndContinuePayload(L"4", consumed, text))
    {
        return 4;
    }
    // Empty count.
    if (ParseCommitCandidateAndContinuePayload(L"\t数据", consumed, text))
    {
        return 5;
    }
    // Non-digit count.
    if (ParseCommitCandidateAndContinuePayload(L"4x\t数据", consumed, text))
    {
        return 6;
    }
    // Negative count.
    if (ParseCommitCandidateAndContinuePayload(L"-4\t数据", consumed, text))
    {
        return 7;
    }
    // Oversized count.
    if (ParseCommitCandidateAndContinuePayload(L"99999999\t数据", consumed, text))
    {
        return 8;
    }
    // Empty payload.
    if (ParseCommitCandidateAndContinuePayload(L"", consumed, text))
    {
        return 9;
    }
    // Tab inside the text is part of the text, not another separator.
    if (!ParseCommitCandidateAndContinuePayload(L"1\ta\tb", consumed, text) || consumed != 1 || text != L"a\tb")
    {
        return 10;
    }
    return 0;
}
