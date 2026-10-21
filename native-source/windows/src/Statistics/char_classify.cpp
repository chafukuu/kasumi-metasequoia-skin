#include "char_classify.h"
#include <Windows.h>

namespace
{
void SaturatingIncrement(uint16_t &value)
{
    if (value != UINT16_MAX)
    {
        ++value;
    }
}

void SaturatingAdd(uint16_t &value, uint16_t count)
{
    if (static_cast<uint32_t>(value) + count > UINT16_MAX)
    {
        value = UINT16_MAX;
    }
    else
    {
        value = static_cast<uint16_t>(value + count);
    }
}

// CJK ideographs count as Chinese: Unified Ideographs, Extension A, the
// compatibility block and the supplementary-plane ideograph range
// (Extensions B-F plus the compatibility supplement).
bool IsCjk(uint32_t codePoint)
{
    return (codePoint >= 0x4E00 && codePoint <= 0x9FFF) || (codePoint >= 0x3400 && codePoint <= 0x4DBF) ||
           (codePoint >= 0xF900 && codePoint <= 0xFAFF) || (codePoint >= 0x20000 && codePoint <= 0x2FA1F);
}

bool IsAsciiLetter(uint32_t codePoint)
{
    return (codePoint >= L'A' && codePoint <= L'Z') || (codePoint >= L'a' && codePoint <= L'z');
}

bool IsDigit(uint32_t codePoint)
{
    return (codePoint >= L'0' && codePoint <= L'9') || (codePoint >= 0xFF10 && codePoint <= 0xFF19);
}

bool IsHighSurrogate(uint32_t codeUnit)
{
    return codeUnit >= 0xD800 && codeUnit <= 0xDBFF;
}

bool IsLowSurrogate(uint32_t codeUnit)
{
    return codeUnit >= 0xDC00 && codeUnit <= 0xDFFF;
}
} // namespace

namespace MsimeStats
{
uint32_t CharClassCounts::Total() const
{
    return static_cast<uint32_t>(cjk) + static_cast<uint32_t>(latin) + static_cast<uint32_t>(digit) +
           static_cast<uint32_t>(punct) + static_cast<uint32_t>(other);
}

void CharClassCounts::Add(const CharClassCounts &counts)
{
    SaturatingAdd(cjk, counts.cjk);
    SaturatingAdd(latin, counts.latin);
    SaturatingAdd(digit, counts.digit);
    SaturatingAdd(punct, counts.punct);
    SaturatingAdd(other, counts.other);
}

CharClassCounts ClassifyText(const wchar_t *text, size_t length)
{
    CharClassCounts counts;
    if (text == nullptr)
    {
        return counts;
    }

    for (size_t index = 0; index < length; ++index)
    {
        const wchar_t unit = text[index];
        const uint32_t codeUnit = static_cast<uint32_t>(static_cast<uint16_t>(unit));
        uint32_t codePoint = codeUnit;

        if (IsHighSurrogate(codeUnit))
        {
            if (index + 1 >= length)
            {
                SaturatingIncrement(counts.other); // unpaired high surrogate
                continue;
            }
            const uint32_t lowUnit = static_cast<uint32_t>(static_cast<uint16_t>(text[index + 1]));
            if (!IsLowSurrogate(lowUnit))
            {
                SaturatingIncrement(counts.other); // unpaired high surrogate
                continue;
            }
            codePoint = 0x10000 + ((codeUnit - 0xD800) << 10) + (lowUnit - 0xDC00);
            ++index; // the pair is one user-perceived character
        }
        else if (IsLowSurrogate(codeUnit))
        {
            SaturatingIncrement(counts.other); // unpaired low surrogate
            continue;
        }

        if (IsCjk(codePoint))
        {
            SaturatingIncrement(counts.cjk);
            continue;
        }
        if (IsAsciiLetter(codePoint))
        {
            SaturatingIncrement(counts.latin);
            continue;
        }
        if (IsDigit(codePoint))
        {
            SaturatingIncrement(counts.digit);
            continue;
        }
        if (codePoint > 0xFFFF)
        {
            // Supplementary non-CJK (emoji, symbols, historic scripts). The
            // character-type API below is BMP-only, and per the design the
            // supplementary plane does not contribute to punctuation.
            SaturatingIncrement(counts.other);
            continue;
        }

        // Punctuation and spaces come from the Unicode character type instead
        // of a hand-written whitelist: fullwidth marks, ideographic space and
        // the like all have to follow the API (A3 sampling conclusion).
        WORD typeFlags = 0;
        if (GetStringTypeW(CT_CTYPE1, &unit, 1, &typeFlags) && (typeFlags & (C1_PUNCT | C1_SPACE)) != 0)
        {
            SaturatingIncrement(counts.punct);
            continue;
        }
        SaturatingIncrement(counts.other);
    }

    return counts;
}
} // namespace MsimeStats
