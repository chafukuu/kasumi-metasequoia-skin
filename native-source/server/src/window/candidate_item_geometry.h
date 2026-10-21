#pragma once

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

// Geometry only: these records must never carry candidate text or labels.
struct CandidateItemRectDip
{
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct CandidateMainRowGeometry
{
    // Internal boundaries only. N items in the first row produce N - 1 edges.
    std::vector<float> edgesDip;
    // The first item's left and the last item's right; empty for no valid row.
    std::vector<float> rangeDip;
};

inline CandidateMainRowGeometry CandidateMainRowGeometryFromBounds(const CandidateItemRectDip &card,
                                                                   const std::vector<CandidateItemRectDip> &items)
{
    if (items.empty() || !std::isfinite(card.x) || !std::isfinite(card.width) || card.width <= 0.0f)
        return {};
    float rowY = items.front().y;
    for (const auto &item : items)
    {
        if (!std::isfinite(item.x) || !std::isfinite(item.y) || !std::isfinite(item.width) ||
            !std::isfinite(item.height) || item.width <= 0.0f || item.height <= 0.0f)
            return {};
        rowY = (std::min)(rowY, item.y);
    }

    CandidateMainRowGeometry result;
    float previousRight = 0.0f;
    for (const auto &item : items)
    {
        if (std::abs(item.y - rowY) > 0.01f)
            continue;
        const float rawLeft = item.x - card.x;
        const float rawRight = rawLeft + item.width;
        if (!std::isfinite(rawLeft) || !std::isfinite(rawRight) || rawLeft < -0.01f || rawRight > card.width + 0.01f)
            return {};
        // Float layout can cross the card edge by a few ulps. Keep every
        // exported endpoint in the card and reject an item wholly outside it.
        const float left = std::clamp(rawLeft, 0.0f, card.width);
        const float right = std::clamp(rawRight, 0.0f, card.width);
        if (right <= left)
            return {};
        if (result.rangeDip.empty())
        {
            result.rangeDip = {left, right};
        }
        else
        {
            if (left < previousRight - 0.01f)
                return {};
            const float edge = (previousRight + left) * 0.5f;
            if (!std::isfinite(edge) || edge <= result.rangeDip.front() || edge >= right ||
                (!result.edgesDip.empty() && edge <= result.edgesDip.back()))
                return {};
            result.edgesDip.push_back(edge);
            result.rangeDip.back() = right;
        }
        previousRight = right;
    }
    return result;
}

inline std::wstring CandidateGeometryDipArray(const std::vector<float> &values)
{
    std::wostringstream output;
    output.imbue(std::locale::classic());
    output << L'[' << std::fixed << std::setprecision(3);
    for (size_t index = 0; index < values.size(); ++index)
    {
        if (index != 0)
            output << L',';
        output << values[index];
    }
    output << L']';
    return output.str();
}
