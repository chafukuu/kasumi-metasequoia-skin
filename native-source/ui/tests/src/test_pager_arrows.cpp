#include "tests/includes/test_framework.h"

#include "msimeui/Controls.h"
#include "msimeui/Layout.h"

#include <cmath>
#include <memory>

using namespace msimeui;

namespace
{
std::shared_ptr<PagerArrows> MakePager()
{
    auto pager = std::make_shared<PagerArrows>();
    PagerArrows::Appearance appearance;
    appearance.buttonWidth = 10.0f;
    appearance.buttonHeight = 12.0f;
    appearance.gap = 2.0f;
    pager->SetAppearance(appearance);
    return pager;
}
} // namespace

TEST_CASE(PagerArrowsMeasuresTwoButtonsAndGap)
{
    auto pager = MakePager();
    const SizeF measured = pager->MeasureInLayout({500.0f, 500.0f});
    REQUIRE_NEAR(measured.width, 22.0f);
    REQUIRE_NEAR(measured.height, 12.0f);
}

TEST_CASE(PagerArrowsKeepButtonsInTrailingBottomCorner)
{
    auto pager = MakePager();
    pager->MeasureInLayout({500.0f, 500.0f});
    // A slot larger than the buttons, as a stretched list row would hand it.
    pager->Arrange({100.0f, 50.0f, 40.0f, 30.0f});

    const RectF next = pager->GetPartBounds(PagerArrows::Part::Next);
    const RectF previous = pager->GetPartBounds(PagerArrows::Part::Previous);
    REQUIRE_NEAR(next.x, 130.0f);
    REQUIRE_NEAR(next.y, 68.0f);
    REQUIRE_NEAR(previous.x, 118.0f);
    REQUIRE_NEAR(previous.y, 68.0f);

    REQUIRE(pager->HitTestPart({135.0f, 74.0f}) == PagerArrows::Part::Next);
    REQUIRE(pager->HitTestPart({121.0f, 74.0f}) == PagerArrows::Part::Previous);
    // The gap between the buttons and the empty rest of the slot are not buttons.
    REQUIRE(pager->HitTestPart({129.0f, 74.0f}) == PagerArrows::Part::None);
    REQUIRE(pager->HitTestPart({105.0f, 55.0f}) == PagerArrows::Part::None);
    REQUIRE(!pager->HitTest({105.0f, 55.0f}));
}

TEST_CASE(PagerArrowsDividerTakesRoomBeforeButtons)
{
    auto pager = std::make_shared<PagerArrows>();
    PagerArrows::Appearance appearance;
    appearance.buttonWidth = 10.0f;
    appearance.buttonHeight = 12.0f;
    appearance.glyph = PagerArrows::Glyph::Triangle;
    appearance.glyphSize = 10.0f;
    appearance.dividerWidth = 1.0f;
    appearance.dividerGap = 3.0f;
    pager->SetAppearance(appearance);
    const SizeF measured = pager->MeasureInLayout({500.0f, 500.0f});
    REQUIRE_NEAR(measured.width, 24.0f);
    REQUIRE_NEAR(measured.height, 12.0f);
    REQUIRE_NEAR(appearance.GlyphWidth(), 8.4f);

    // The divider is drawn left of the buttons, so the buttons still sit in the trailing corner.
    pager->Arrange({0.0f, 0.0f, 24.0f, 12.0f});
    REQUIRE_NEAR(pager->GetPartBounds(PagerArrows::Part::Previous).x, 4.0f);
    REQUIRE(pager->HitTestPart({2.0f, 6.0f}) == PagerArrows::Part::None);
}

TEST_CASE(PagerArrowsTrackEnabledParts)
{
    auto pager = MakePager();
    REQUIRE(pager->IsPartEnabled(PagerArrows::Part::Previous));
    REQUIRE(pager->IsPartEnabled(PagerArrows::Part::Next));
    pager->SetEnabled(false, true);
    REQUIRE(!pager->IsPartEnabled(PagerArrows::Part::Previous));
    REQUIRE(pager->IsPartEnabled(PagerArrows::Part::Next));
    REQUIRE(!pager->IsPartEnabled(PagerArrows::Part::None));
}
