#include "window/candidate_skin_layers.h"

#include <algorithm>

namespace CandidateSkinLayers
{
Edges ExpandBox(const std::vector<double> &values)
{
    Edges edges;
    if (values.size() == 1)
    {
        edges = {values[0], values[0], values[0], values[0]};
    }
    else if (values.size() == 2)
    {
        edges = {values[0], values[1], values[0], values[1]};
    }
    else if (values.size() == 4)
    {
        edges = {values[0], values[1], values[2], values[3]};
    }
    return edges;
}

NineSlice ResolveNineSlice(const CandidateSkinCatalog::BorderImage &borderImage)
{
    NineSlice result;
    const std::vector<double> &slice = borderImage.stretchDip.empty() ? borderImage.sliceDip : borderImage.stretchDip;
    result.slice = ExpandBox(slice);
    result.width = borderImage.widthDip.empty() ? result.slice : ExpandBox(borderImage.widthDip);
    result.fill = borderImage.fill;
    return result;
}

Edges ResolveContentPadding(const CandidateSkinCatalog::Package &package, double basePadding)
{
    Edges border;
    if (package.borderImage && !package.borderImage->source.empty())
    {
        border = ResolveNineSlice(*package.borderImage).width;
    }
    if (package.contentInsetsDip)
    {
        const CandidateSkinCatalog::InsetsDip &insets = *package.contentInsetsDip;
        return {(std::max)(insets.top, border.top), (std::max)(insets.right, border.right),
                (std::max)(insets.bottom, border.bottom), (std::max)(insets.left, border.left)};
    }
    return {border.top + basePadding, border.right + basePadding, border.bottom + basePadding,
            border.left + basePadding};
}

Layers SplitDecorations(const std::vector<CandidateSkinCatalog::FreeDecoration> &decorations)
{
    std::vector<CandidateSkinCatalog::FreeDecoration> sorted = decorations;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const CandidateSkinCatalog::FreeDecoration &a, const CandidateSkinCatalog::FreeDecoration &b) {
                         return a.zIndex < b.zIndex;
                     });
    Layers layers;
    for (auto &decoration : sorted)
    {
        (decoration.zIndex < CandidateSkinCatalog::kDecorationFrontZIndex ? layers.below : layers.above)
            .push_back(std::move(decoration));
    }
    return layers;
}
} // namespace CandidateSkinLayers
