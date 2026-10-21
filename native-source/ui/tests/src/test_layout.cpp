#include "tests/includes/test_framework.h"

#include "msimeui/Controls.h"
#include "msimeui/Layout.h"
#include "msimeui/Window.h"

#include <cmath>
#include <memory>

using namespace msimeui;

namespace
{
// A leaf visual with a fixed desired size. It reports its size verbatim rather
// than clamping to the available space, so the tests can observe the clamping
// that Visual::MeasureInLayout is responsible for.
class FixedVisual : public Visual
{
  public:
    explicit FixedVisual(SizeF size) : size_(size)
    {
    }

    SizeF Measure(const SizeF &) override
    {
        ++measureCount;
        return size_;
    }

    void Arrange(const RectF &finalRect) override
    {
        bounds_ = finalRect;
    }

    void Render(DeviceResources &) override
    {
    }

    int measureCount = 0;

  private:
    SizeF size_;
};

std::shared_ptr<FixedVisual> MakeFixed(float width, float height)
{
    return std::make_shared<FixedVisual>(SizeF{width, height});
}
} // namespace

TEST_CASE(margin_deflates_the_available_size_and_inflates_the_reported_size)
{
    auto visual = MakeFixed(100.0f, 50.0f);
    visual->SetMargin(10.0f);

    const SizeF outer = visual->MeasureInLayout({200.0f, 200.0f});

    REQUIRE_NEAR(outer.width, 120.0f);
    REQUIRE_NEAR(outer.height, 70.0f);
}

TEST_CASE(explicit_size_overrides_the_measured_size)
{
    auto visual = MakeFixed(100.0f, 50.0f);
    visual->SetWidth(80.0f);
    visual->SetHeight(20.0f);

    const SizeF outer = visual->MeasureInLayout({200.0f, 200.0f});

    REQUIRE_NEAR(outer.width, 80.0f);
    REQUIRE_NEAR(outer.height, 20.0f);
}

TEST_CASE(min_and_max_constraints_clamp_the_desired_size)
{
    auto visual = MakeFixed(100.0f, 50.0f);
    visual->SetMinWidth(150.0f);
    visual->SetMaxHeight(30.0f);

    const SizeF outer = visual->MeasureInLayout({400.0f, 400.0f});

    REQUIRE_NEAR(outer.width, 150.0f);
    REQUIRE_NEAR(outer.height, 30.0f);
}

TEST_CASE(the_desired_size_never_exceeds_the_available_space)
{
    auto visual = MakeFixed(500.0f, 500.0f);

    const SizeF outer = visual->MeasureInLayout({200.0f, 100.0f});

    REQUIRE_NEAR(outer.width, 200.0f);
    REQUIRE_NEAR(outer.height, 100.0f);
}

TEST_CASE(measuring_twice_with_the_same_available_size_reuses_the_cache)
{
    auto visual = MakeFixed(100.0f, 50.0f);

    visual->MeasureInLayout({200.0f, 200.0f});
    visual->MeasureInLayout({200.0f, 200.0f});
    REQUIRE(visual->measureCount == 1);

    // A different available size has to invalidate the cached measurement.
    visual->MeasureInLayout({300.0f, 200.0f});
    REQUIRE(visual->measureCount == 2);
}

TEST_CASE(stretch_alignment_fills_the_arranged_slot)
{
    auto visual = MakeFixed(40.0f, 20.0f);
    visual->SetHorizontalAlignment(HorizontalAlignment::Stretch);
    visual->SetVerticalAlignment(VerticalAlignment::Stretch);

    visual->MeasureInLayout({200.0f, 100.0f});
    visual->ArrangeInLayout({0.0f, 0.0f, 200.0f, 100.0f});

    const RectF &bounds = visual->GetBounds();
    REQUIRE_NEAR(bounds.width, 200.0f);
    REQUIRE_NEAR(bounds.height, 100.0f);
}

TEST_CASE(center_alignment_positions_the_visual_inside_the_slot)
{
    auto visual = MakeFixed(40.0f, 20.0f);
    visual->SetHorizontalAlignment(HorizontalAlignment::Center);
    visual->SetVerticalAlignment(VerticalAlignment::Center);

    visual->MeasureInLayout({200.0f, 100.0f});
    visual->ArrangeInLayout({0.0f, 0.0f, 200.0f, 100.0f});

    const RectF &bounds = visual->GetBounds();
    REQUIRE_NEAR(bounds.x, 80.0f);
    REQUIRE_NEAR(bounds.y, 40.0f);
    REQUIRE_NEAR(bounds.width, 40.0f);
    REQUIRE_NEAR(bounds.height, 20.0f);
}

TEST_CASE(trailing_alignment_pushes_the_visual_to_the_far_edge)
{
    auto visual = MakeFixed(40.0f, 20.0f);
    visual->SetHorizontalAlignment(HorizontalAlignment::Trailing);
    visual->SetVerticalAlignment(VerticalAlignment::Trailing);

    visual->MeasureInLayout({200.0f, 100.0f});
    visual->ArrangeInLayout({0.0f, 0.0f, 200.0f, 100.0f});

    const RectF &bounds = visual->GetBounds();
    REQUIRE_NEAR(bounds.x, 160.0f);
    REQUIRE_NEAR(bounds.y, 80.0f);
}

TEST_CASE(margin_offsets_the_arranged_bounds)
{
    auto visual = MakeFixed(40.0f, 20.0f);
    visual->SetMargin(Thickness{10.0f, 5.0f, 0.0f, 0.0f});
    visual->SetHorizontalAlignment(HorizontalAlignment::Leading);
    visual->SetVerticalAlignment(VerticalAlignment::Leading);

    visual->MeasureInLayout({200.0f, 100.0f});
    visual->ArrangeInLayout({0.0f, 0.0f, 200.0f, 100.0f});

    const RectF &bounds = visual->GetBounds();
    REQUIRE_NEAR(bounds.x, 10.0f);
    REQUIRE_NEAR(bounds.y, 5.0f);
}

TEST_CASE(stack_panel_reports_the_children_height_plus_spacing)
{
    auto panel = std::make_shared<StackPanel>(8.0f);
    panel->AddChild(MakeFixed(60.0f, 20.0f));
    panel->AddChild(MakeFixed(40.0f, 30.0f));

    const SizeF outer = panel->MeasureInLayout({200.0f, 200.0f});

    // Widest child, and both heights with one gap between them.
    REQUIRE_NEAR(outer.width, 60.0f);
    REQUIRE_NEAR(outer.height, 58.0f);
}

TEST_CASE(stack_panel_advances_each_child_by_its_height_and_the_spacing)
{
    auto first = MakeFixed(60.0f, 20.0f);
    auto second = MakeFixed(40.0f, 30.0f);
    auto panel = std::make_shared<StackPanel>(8.0f);
    panel->AddChild(first);
    panel->AddChild(second);

    panel->MeasureInLayout({200.0f, 200.0f});
    panel->ArrangeInLayout({0.0f, 0.0f, 200.0f, 100.0f});

    REQUIRE_NEAR(first->GetBounds().y, 0.0f);
    REQUIRE_NEAR(first->GetBounds().height, 20.0f);
    REQUIRE_NEAR(second->GetBounds().y, 28.0f);
    REQUIRE_NEAR(second->GetBounds().height, 30.0f);
}

TEST_CASE(stack_panel_padding_insets_its_children)
{
    auto child = MakeFixed(40.0f, 20.0f);
    auto panel = std::make_shared<StackPanel>();
    panel->SetPadding(12.0f);
    panel->AddChild(child);

    panel->MeasureInLayout({200.0f, 200.0f});
    panel->ArrangeInLayout({0.0f, 0.0f, 200.0f, 100.0f});

    REQUIRE_NEAR(child->GetBounds().x, 12.0f);
    REQUIRE_NEAR(child->GetBounds().y, 12.0f);
}

TEST_CASE(horizontal_stack_panel_advances_along_x)
{
    auto first = MakeFixed(60.0f, 20.0f);
    auto second = MakeFixed(40.0f, 30.0f);
    auto panel = std::make_shared<HorizontalStackPanel>(6.0f);
    panel->AddChild(first);
    panel->AddChild(second);

    const SizeF outer = panel->MeasureInLayout({300.0f, 200.0f});
    panel->ArrangeInLayout({0.0f, 0.0f, 300.0f, 100.0f});

    REQUIRE_NEAR(outer.width, 106.0f);
    REQUIRE_NEAR(first->GetBounds().x, 0.0f);
    REQUIRE_NEAR(second->GetBounds().x, 66.0f);
}

TEST_CASE(horizontal_candidate_translation_uses_a_second_line)
{
    CandidateList list(28.0f);
    list.SetOrientation(CandidateList::Orientation::Horizontal);
    list.SetItems({{L"1", L"candidate", L"", L""}});
    const SizeF plain = list.MeasureInLayout({1000.0f, 1000.0f});

    list.SetItems({{L"1", L"candidate", L"", L"word"}});
    const SizeF translated = list.MeasureInLayout({1000.0f, 1000.0f});
    REQUIRE_NEAR(translated.width, plain.width);
    REQUIRE_NEAR(translated.height, plain.height + 16.0f * 0.78f * 1.25f);

    list.SetItems({{L"1", L"candidate", L"", L"a translation wider than the candidate"}});
    REQUIRE(list.MeasureInLayout({1000.0f, 1000.0f}).width > translated.width);

    list.SetItems({{L"1", L"candidate", L"", L""}});
    REQUIRE_NEAR(list.MeasureInLayout({1000.0f, 1000.0f}).height, plain.height);
}

TEST_CASE(horizontal_candidate_translation_keeps_the_row_bottom_padding_below_it)
{
    // 行高里的底部内边距挪到翻译下面，而不是夹在候选与翻译之间；挪动不应让整行变高。
    CandidateList list(38.0f);
    CandidateList::Appearance appearance;
    appearance.itemHeight = 38.0f;
    appearance.contentPadBottom = 5.0f;
    list.SetAppearance(appearance);
    list.SetOrientation(CandidateList::Orientation::Horizontal);
    list.SetItems({{L"1", L"candidate", L"", L"word"}});
    const float translationH = appearance.fontSize * 0.78f * 1.25f;
    REQUIRE_NEAR(list.MeasureInLayout({1000.0f, 1000.0f}).height, 38.0f + translationH);
    list.ArrangeInLayout({0.0f, 0.0f, 1000.0f, 38.0f + translationH});
    const RectF item = list.GetItemBounds(0);
    REQUIRE_NEAR(item.height, 38.0f + translationH);
}

TEST_CASE(justified_horizontal_candidates_spread_spare_width_to_the_right_edge)
{
    CandidateList list(28.0f);
    CandidateList::Appearance appearance;
    appearance.itemHeight = 28.0f;
    appearance.itemGap = 0.0f;
    appearance.justifyHorizontalRows = true;
    list.SetAppearance(appearance);
    list.SetOrientation(CandidateList::Orientation::Horizontal);
    list.SetItems({{L"1", L"候选", L"", L""}, {L"2", L"候选", L"", L""}});
    const SizeF natural = list.MeasureInLayout({1000.0f, 1000.0f});
    const float naturalFirstWidth = list.GetItemBounds(0).width;

    // 卡片保持了更宽的尺寸：多出的 40 均分给两项，末项右边贴着列表右边。
    list.ArrangeInLayout({10.0f, 0.0f, natural.width + 40.0f, natural.height});
    const RectF first = list.GetItemBounds(0);
    const RectF second = list.GetItemBounds(1);
    REQUIRE_NEAR(first.x, 10.0f);
    REQUIRE_NEAR(first.width, naturalFirstWidth + 20.0f);
    REQUIRE_NEAR(second.x, first.x + first.width);
    REQUIRE_NEAR(second.x + second.width, 10.0f + natural.width + 40.0f);
}

TEST_CASE(vertical_candidate_rows_fill_a_wider_arranged_width)
{
    // 竖排候选项的自然宽度较窄，但被分配到更宽的行宽时（如卡片最小宽度撑大），
    // 每一行都应铺满该宽度，使选中高亮与命中区域覆盖整行。
    CandidateList list(28.0f);
    list.SetOrientation(CandidateList::Orientation::Vertical);
    list.SetItems({{L"1", L"里", L"(aA)", L""}, {L"2", L"力", L"(gP)", L""}});

    const SizeF natural = list.MeasureInLayout({1000.0f, 1000.0f});
    REQUIRE(natural.width < 200.0f); // 自然宽度明显窄于目标行宽

    // 被安排到远宽于自然宽度的插槽（模拟卡片最小宽度撑大后的分配）。
    const float arrangedWidth = 200.0f;
    list.ArrangeInLayout({0.0f, 0.0f, arrangedWidth, natural.height});

    // 每一行的最终矩形都应铺满整个行宽，而不是停留在自然宽度。
    REQUIRE_NEAR(list.GetItemBounds(0).width, arrangedWidth);
    REQUIRE_NEAR(list.GetItemBounds(1).width, arrangedWidth);
    // 左右起点一致，配合外层卡片内边距即可得到对称留白。
    REQUIRE_NEAR(list.GetItemBounds(0).x, 0.0f);
}

TEST_CASE(vertical_candidate_rows_fill_min_width_card)
{
    // 复刻候选窗真实层级：Card(最小宽度) -> body(StackPanel) -> CandidateList。
    // 短候选项时，卡片被最小宽度撑大，行高亮仍应铺满卡片内宽。
    auto list = std::make_shared<CandidateList>(28.0f);
    list->SetOrientation(CandidateList::Orientation::Vertical);
    list->SetItems({{L"1", L"我", L"(pD)", L""}, {L"2", L"握", L"(fT)", L""}});

    auto body = std::make_shared<StackPanel>(2.0f);
    body->AddChild(list);

    Brush brush;
    auto card = std::make_shared<Card>(brush, 5.0f);
    constexpr float kMinWidth = 160.0f;
    card->SetMinWidth(kMinWidth);
    card->AddChild(body);

    const SizeF measured = card->MeasureInLayout({480.0f, 640.0f});
    // 短候选项：卡片被最小宽度撑大，测得宽度即最小宽度。
    REQUIRE_NEAR(measured.width, kMinWidth);

    card->ArrangeInLayout({0.0f, 0.0f, measured.width, measured.height});

    // 卡片内宽 = 最小宽度 - 两侧内边距。行矩形应铺满该内宽。
    const float innerWidth = kMinWidth - 5.0f * 2.0f;
    REQUIRE_NEAR(list->GetItemBounds(0).width, innerWidth);
    REQUIRE_NEAR(list->GetItemBounds(1).width, innerWidth);
}

TEST_CASE(vertical_candidate_rows_stay_full_width_after_paint_remeasure)
{
    // 复刻真实渲染时序：ShowInternal 以大可用宽度测量、按卡片宽度排布；随后 Present 阶段
    // 以窗口尺寸重新测量（可用宽度不同，会把每项宽度还原为自然宽度），再以相同矩形排布
    // （可能命中排布缓存而跳过）。行矩形仍须铺满列表宽度。
    CandidateList list(28.0f);
    list.SetOrientation(CandidateList::Orientation::Vertical);
    list.SetItems({{L"1", L"我", L"(pD)", L""}, {L"2", L"握", L"(fT)", L""}});

    const float arrangedWidth = 200.0f;

    // 第一次布局：自然宽度较窄，但被排布到更宽的行宽。
    list.MeasureInLayout({480.0f, 640.0f});
    list.ArrangeInLayout({0.0f, 0.0f, arrangedWidth, 200.0f});
    REQUIRE_NEAR(list.GetItemBounds(0).width, arrangedWidth);

    // Present 阶段以不同的可用宽度重新测量，再以相同矩形排布。
    list.MeasureInLayout({arrangedWidth, 640.0f});
    list.ArrangeInLayout({0.0f, 0.0f, arrangedWidth, 200.0f});

    REQUIRE_NEAR(list.GetItemBounds(0).width, arrangedWidth);
    REQUIRE_NEAR(list.GetItemBounds(1).width, arrangedWidth);
}

TEST_CASE(horizontal_candidate_translation_area_activates_the_candidate)
{
    Window window(L"candidate-test", L"", 1000, 1000);
    CandidateList list(28.0f);
    list.Attach(&window);
    list.SetOrientation(CandidateList::Orientation::Horizontal);
    list.SetItems({{L"1", L"candidate", L"", L"word"}, {L"2", L"candidate", L"", L"word"}});
    const SizeF size = list.MeasureInLayout({1000.0f, 1000.0f});
    list.ArrangeInLayout({0.0f, 0.0f, size.width, size.height});
    size_t activated = 0;
    list.SetOnItemActivated([&](size_t index) { activated = index; });
    const POINT point{static_cast<LONG>(size.width - 2.0f), static_cast<LONG>(size.height - 2.0f)};
    REQUIRE(list.OnMouseDown(point, MK_LBUTTON));
    REQUIRE(list.OnMouseUp(point, 0));
    REQUIRE(activated == 1);
}

TEST_CASE(horizontal_candidates_are_widened_to_the_minimum_item_width)
{
    CandidateList list(28.0f);
    CandidateList::Appearance appearance;
    appearance.itemHeight = 28.0f;
    appearance.itemGap = 4.0f;
    appearance.minItemWidth = 90.0f;
    list.SetAppearance(appearance);
    list.SetOrientation(CandidateList::Orientation::Horizontal);
    list.SetItems({{L"1", L"a", L"", L""}, {L"2", L"b", L"", L""}});
    const SizeF size = list.MeasureInLayout({1000.0f, 1000.0f});
    list.ArrangeInLayout({0.0f, 0.0f, size.width, size.height});
    REQUIRE_NEAR(list.GetItemBounds(0).width, 90.0f);
    REQUIRE_NEAR(list.GetItemBounds(1).x, 94.0f);
    REQUIRE_NEAR(size.width, 184.0f);

    // Vertical rows already span the list, so the minimum does not apply there.
    list.SetOrientation(CandidateList::Orientation::Vertical);
    REQUIRE(list.MeasureInLayout({1000.0f, 1000.0f}).width < 90.0f);
}

TEST_CASE(grid_candidates_share_equal_columns_and_wide_items_span_several)
{
    CandidateList list(28.0f);
    CandidateList::Appearance appearance;
    appearance.itemHeight = 28.0f;
    appearance.itemGap = 4.0f;
    appearance.minItemWidth = 60.0f;
    appearance.gridColumns = 4;
    list.SetAppearance(appearance);
    list.SetOrientation(CandidateList::Orientation::Horizontal);
    list.SetItems({{L"", L"a", L"", L""},
                   {L"", L"b", L"", L""},
                   {L"", L"longer word", L"", L""},
                   {L"", L"c", L"", L""},
                   {L"", L"d", L"", L""}});
    // 316 DIP fits four 60 DIP columns; (316 - 3 * 4) / 4 = 76 each.
    const SizeF size = list.MeasureInLayout({316.0f, 1000.0f});
    list.ArrangeInLayout({0.0f, 0.0f, 316.0f, size.height});
    REQUIRE_NEAR(list.GetItemBounds(0).width, 76.0f);
    REQUIRE_NEAR(list.GetItemBounds(1).x, 80.0f);
    // The long item takes whole columns and fits on the first row from the third column on.
    const RectF wide = list.GetItemBounds(2);
    REQUIRE_NEAR(wide.x, 160.0f);
    REQUIRE_NEAR(wide.width, 156.0f);
    REQUIRE_NEAR(list.GetItemBounds(3).x, 0.0f);
    REQUIRE(list.GetItemBounds(3).y > 0.0f);
    REQUIRE_NEAR(list.GetItemBounds(4).x, 80.0f);

    // Narrower than four minimum columns: fewer, still equal, columns.
    list.MeasureInLayout({200.0f, 1000.0f});
    list.ArrangeInLayout({0.0f, 0.0f, 200.0f, 1000.0f});
    REQUIRE_NEAR(list.GetItemBounds(0).width, 64.0f);
    REQUIRE_NEAR(list.GetItemBounds(1).x, 68.0f);
}

TEST_CASE(fit_content_candidates_measure_without_minimum_width_or_slack)
{
    CandidateList list(28.0f);
    list.SetOrientation(CandidateList::Orientation::Vertical);
    list.SetItems({{L"1", L"a", L"", L""}});
    // Without fitContent a short list is floored to 80 DIP.
    REQUIRE_NEAR(list.MeasureInLayout({1000.0f, 1000.0f}).width, 80.0f);

    CandidateList::Appearance appearance;
    appearance.itemHeight = 28.0f;
    appearance.fitContent = true;
    appearance.contentPadLeft = 0.0f;
    appearance.textPadLeft = 5.0f;
    appearance.contentPadRight = 0.0f;
    list.SetAppearance(appearance);
    const float bare = list.MeasureInLayout({1000.0f, 1000.0f}).width;
    REQUIRE(bare < 80.0f);

    // The trailing padding is the only thing after the text, so mirroring the leading one adds exactly that much.
    appearance.contentPadRight = 5.0f;
    list.SetAppearance(appearance);
    REQUIRE_NEAR(list.MeasureInLayout({1000.0f, 1000.0f}).width, bare + 5.0f);
}

TEST_CASE(vertical_candidate_translation_stays_on_the_same_line)
{
    CandidateList list(28.0f);
    list.SetOrientation(CandidateList::Orientation::Horizontal);
    list.SetItems({{L"1", L"candidate", L"", L"word"}});
    const SizeF horizontal = list.MeasureInLayout({1000.0f, 1000.0f});

    list.SetOrientation(CandidateList::Orientation::Vertical);
    const SizeF vertical = list.MeasureInLayout({1000.0f, 1000.0f});
    REQUIRE_NEAR(vertical.height, 28.0f);
    REQUIRE(vertical.width > horizontal.width);
}

TEST_CASE(horizontal_candidates_wrap_and_keep_their_click_indices_at_each_dpi)
{
    Window window(L"candidate-wrap-test", L"", 1000, 1000);
    CandidateList list(28.0f);
    list.Attach(&window);
    list.SetOrientation(CandidateList::Orientation::Horizontal);
    list.SetItems({{L"1", L"候选文字", L"", L""}});
    const float width = list.MeasureInLayout({1000.0f, 1000.0f}).width + 1.0f;
    list.SetItems({{L"1", L"候选文字", L"", L""},
                   {L"2", L"候选文字", L"", L""},
                   {L"3", L"候选文字", L"", L""},
                   {L"4", L"候选文字", L"", L""},
                   {L"5", L"候选文字", L"", L""},
                   {L"6", L"候选文字", L"", L""}});
    const SizeF wrapped = list.MeasureInLayout({width, 1000.0f});
    REQUIRE_NEAR(wrapped.height, 6.0f * 28.0f + 5.0f * 2.0f);
    list.ArrangeInLayout({10.0f, 20.0f, width, wrapped.height});
    size_t activated = 99;
    list.SetOnItemActivated([&](size_t index) { activated = index; });
    for (float dpi : {96.0f, 120.0f, 144.0f, 192.0f})
    {
        window.SetDpiOverride(dpi);
        for (size_t i = 0; i < 6; ++i)
        {
            const POINT point{static_cast<LONG>(DipsToPixels(30.0f, dpi)),
                              static_cast<LONG>(DipsToPixels(34.0f + i * 30.0f, dpi))};
            REQUIRE(list.OnMouseDown(point, MK_LBUTTON));
            REQUIRE(list.OnMouseUp(point, 0));
            REQUIRE(activated == i);
        }
    }
    // 可用宽度恢复后必须重新排成一行，不保留上一轮的换行高度。
    REQUIRE_NEAR(list.MeasureInLayout({2000.0f, 1000.0f}).height, 28.0f);
}

TEST_CASE(long_candidate_text_wraps_and_the_following_candidate_remains_clickable)
{
    Window window(L"long-candidate-test", L"", 1000, 1000);
    CandidateList list(28.0f);
    list.Attach(&window);
    for (auto orientation : {CandidateList::Orientation::Horizontal, CandidateList::Orientation::Vertical})
    {
        list.SetOrientation(orientation);
        list.SetItems({{L"1", std::wstring(60, L'字'), L"(aux)", L"translation"}});
        const SizeF first = list.MeasureInLayout({140.0f, 2000.0f});
        REQUIRE(first.height > 56.0f);
        REQUIRE(first.width <= 140.0f);
        list.SetItems({{L"1", std::wstring(60, L'字'), L"(aux)", L"translation"}, {L"2", L"下一个", L"", L""}});
        const SizeF all = list.MeasureInLayout({140.0f, 2000.0f});
        REQUIRE(all.height >= first.height + 30.0f);
        list.ArrangeInLayout({0.0f, 0.0f, all.width, all.height});
        size_t activated = 99;
        list.SetOnItemActivated([&](size_t index) { activated = index; });
        const POINT point{20, static_cast<LONG>(all.height - 14.0f)};
        REQUIRE(list.OnMouseDown(point, MK_LBUTTON));
        REQUIRE(list.OnMouseUp(point, 0));
        REQUIRE(activated == 1);
    }
}

TEST_CASE(long_annotation_and_translation_increase_candidate_height)
{
    CandidateList list(28.0f);
    for (auto orientation : {CandidateList::Orientation::Horizontal, CandidateList::Orientation::Vertical})
    {
        list.SetOrientation(orientation);
        list.SetItems({{L"1", L"short", L"", L""}});
        const SizeF plain = list.MeasureInLayout({140.0f, 2000.0f});
        list.SetItems({{L"1", L"short", std::wstring(60, L'a'), std::wstring(100, L'b')}});
        const SizeF annotated = list.MeasureInLayout({140.0f, 2000.0f});
        REQUIRE(annotated.height > plain.height * 3.0f);
        REQUIRE(annotated.width <= 140.0f);
    }
}

TEST_CASE(candidate_arrangement_reflows_when_the_final_slot_is_narrower)
{
    Window window(L"candidate-arrange-test", L"", 1000, 1000);
    CandidateList list(28.0f);
    list.Attach(&window);
    list.SetOrientation(CandidateList::Orientation::Horizontal);
    list.SetItems({{L"1", L"candidate", L"", L""}});
    const float width = list.MeasureInLayout({1000.0f, 1000.0f}).width + 1.0f;
    list.SetItems({{L"1", L"candidate", L"", L""}, {L"2", L"candidate", L"", L""}});
    list.MeasureInLayout({1000.0f, 1000.0f});
    list.Arrange({0.0f, 0.0f, width, 58.0f});
    size_t activated = 99;
    list.SetOnItemActivated([&](size_t index) { activated = index; });
    REQUIRE(list.OnMouseDown({20, 44}, MK_LBUTTON));
    REQUIRE(list.OnMouseUp({20, 44}, 0));
    REQUIRE(activated == 1);
}

namespace
{
CandidateList::Appearance SingleLineCandidateAppearance()
{
    CandidateList::Appearance appearance;
    appearance.horizontalSingleLine = true;
    appearance.fitContent = true;
    appearance.itemHeight = 20.25f;
    appearance.fontSize = 15.0f;
    appearance.labelFontSize = 11.25f;
    appearance.annotationFontSize = 15.0f;
    appearance.minItemWidth = 54.0f;
    appearance.itemGap = 0.0f;
    return appearance;
}

std::vector<CandidateList::Item> LongFirstCandidatePage()
{
    std::vector<CandidateList::Item> items{{L"1", std::wstring(90, L'句'), L"", L""}};
    for (size_t i = 2; i <= 7; ++i)
        items.push_back({std::to_wstring(i), L"候选", L"", L""});
    return items;
}

void RequireSingleCandidateRow(CandidateList &list, size_t count, float width, float height)
{
    float previousRight = 0.0f;
    for (size_t i = 0; i < count; ++i)
    {
        const RectF bounds = list.GetItemBounds(i);
        REQUIRE_NEAR(bounds.y, 0.0f);
        REQUIRE_NEAR(bounds.height, height);
        REQUIRE(bounds.width >= 0.0f);
        REQUIRE(bounds.x + 0.01f >= previousRight);
        REQUIRE(bounds.x + bounds.width <= width + 0.01f);
        previousRight = bounds.x + bounds.width;
    }
}
} // namespace

TEST_CASE(single_line_candidate_skin_keeps_a_long_first_sentence_and_all_seven_candidates_on_the_ribbon)
{
    CandidateList list;
    const auto appearance = SingleLineCandidateAppearance();
    list.SetAppearance(appearance);
    list.SetOrientation(CandidateList::Orientation::Horizontal);
    list.SetItems(LongFirstCandidatePage());
    constexpr float contentWidth = 717.0f;
    const SizeF measured = list.MeasureInLayout({contentWidth, 2000.0f});
    REQUIRE(measured.width <= contentWidth + 0.01f);
    REQUIRE_NEAR(measured.height, appearance.itemHeight);
    list.ArrangeInLayout({0.0f, 0.0f, measured.width, measured.height});
    RequireSingleCandidateRow(list, 7, contentWidth, appearance.itemHeight);
    REQUIRE(list.GetItem(6)->label == L"7");
    REQUIRE(list.GetItemBounds(0).width > list.GetItemBounds(1).width);
}

TEST_CASE(single_line_candidate_width_budget_preserves_short_candidates_natural_width)
{
    CandidateList list;
    list.SetAppearance(SingleLineCandidateAppearance());
    list.SetOrientation(CandidateList::Orientation::Horizontal);
    list.SetItems({{L"2", L"候选", L"", L""}});
    const float naturalShort = list.MeasureInLayout({2000.0f, 2000.0f}).width;
    list.SetItems(LongFirstCandidatePage());
    const SizeF measured = list.MeasureInLayout({717.0f, 2000.0f});
    list.ArrangeInLayout({0.0f, 0.0f, measured.width, measured.height});
    for (size_t i = 1; i < 7; ++i)
        REQUIRE_NEAR(list.GetItemBounds(i).width, naturalShort);
}

TEST_CASE(single_line_candidate_long_text_annotations_and_translations_do_not_increase_height)
{
    CandidateList list;
    const auto appearance = SingleLineCandidateAppearance();
    list.SetAppearance(appearance);
    list.SetOrientation(CandidateList::Orientation::Horizontal);
    std::vector<CandidateList::Item> items;
    for (size_t i = 1; i <= 7; ++i)
        items.push_back({std::to_wstring(i), std::wstring(120, L'句') + L"\n第二行", std::wstring(90, L'a'),
                         std::wstring(100, L'b')});
    list.SetItems(items);
    const SizeF measured = list.MeasureInLayout({717.0f, 2000.0f});
    REQUIRE_NEAR(measured.height, appearance.itemHeight);
    list.ArrangeInLayout({0.0f, 0.0f, measured.width, measured.height});
    RequireSingleCandidateRow(list, 7, 717.0f, appearance.itemHeight);
    REQUIRE(list.GetItem(0)->text == items[0].text);
    REQUIRE(list.GetItem(6)->annotation == items[6].annotation);
    REQUIRE(list.GetItem(6)->translation == items[6].translation);
}

TEST_CASE(single_line_candidate_repeated_measure_and_narrower_arrange_keep_stable_geometry)
{
    CandidateList list;
    const auto appearance = SingleLineCandidateAppearance();
    list.SetAppearance(appearance);
    list.SetOrientation(CandidateList::Orientation::Horizontal);
    list.SetItems(LongFirstCandidatePage());
    for (float width : {717.0f, 230.0f, 30.0f, 717.0f, 230.0f})
    {
        list.MeasureInLayout({2000.0f, 2000.0f});
        list.Arrange({0.0f, 0.0f, width, appearance.itemHeight});
        std::vector<RectF> first;
        for (size_t i = 0; i < 7; ++i)
            first.push_back(list.GetItemBounds(i));
        RequireSingleCandidateRow(list, 7, width, appearance.itemHeight);
        list.InvalidateMeasure();
        const SizeF repeated = list.MeasureInLayout({width, 2000.0f});
        list.Arrange({0.0f, 0.0f, width, appearance.itemHeight});
        REQUIRE_NEAR(repeated.height, appearance.itemHeight);
        for (size_t i = 0; i < 7; ++i)
        {
            REQUIRE_NEAR(list.GetItemBounds(i).x, first[i].x);
            REQUIRE_NEAR(list.GetItemBounds(i).width, first[i].width);
        }
    }
}

TEST_CASE(single_line_candidate_clicks_keep_all_indices_and_complete_text_at_each_dpi)
{
    Window window(L"candidate-single-line-hit-test", L"", 1600, 1000);
    CandidateList list;
    list.Attach(&window);
    const auto appearance = SingleLineCandidateAppearance();
    list.SetAppearance(appearance);
    list.SetOrientation(CandidateList::Orientation::Horizontal);
    const auto items = LongFirstCandidatePage();
    list.SetItems(items);
    const SizeF measured = list.MeasureInLayout({717.0f, 2000.0f});
    list.ArrangeInLayout({10.0f, 20.0f, measured.width, measured.height});
    size_t activated = 99;
    list.SetOnItemActivated([&](size_t index) { activated = index; });
    for (float dpi : {96.0f, 120.0f, 144.0f, 192.0f})
    {
        window.SetDpiOverride(dpi);
        for (size_t i = 0; i < items.size(); ++i)
        {
            const RectF bounds = list.GetItemBounds(i);
            const POINT point{static_cast<LONG>(DipsToPixels(bounds.x + bounds.width * 0.5f, dpi)),
                              static_cast<LONG>(DipsToPixels(bounds.y + bounds.height * 0.5f, dpi))};
            REQUIRE(list.OnMouseDown(point, MK_LBUTTON));
            REQUIRE(list.OnMouseUp(point, 0));
            REQUIRE(activated == i);
            REQUIRE(list.GetItem(activated)->text == items[i].text);
        }
    }
}

TEST_CASE(single_line_candidates_are_opt_in_and_do_not_change_vertical_wrapping)
{
    CandidateList::Appearance appearance;
    REQUIRE(!appearance.horizontalSingleLine);
    CandidateList list;
    list.SetAppearance(appearance);
    list.SetItems({{L"1", std::wstring(80, L'句'), L"", L""}, {L"2", L"候选", L"", L""}});
    list.SetOrientation(CandidateList::Orientation::Horizontal);
    REQUIRE(list.MeasureInLayout({140.0f, 2000.0f}).height > appearance.itemHeight * 2.0f);
    appearance.horizontalSingleLine = true;
    list.SetAppearance(appearance);
    list.SetOrientation(CandidateList::Orientation::Vertical);
    REQUIRE(list.MeasureInLayout({140.0f, 2000.0f}).height > appearance.itemHeight * 2.0f);
}

TEST_CASE(single_line_candidate_natural_width_includes_both_text_and_translation)
{
    CandidateList list;
    auto appearance = SingleLineCandidateAppearance();
    appearance.minItemWidth = 0.0f;
    list.SetAppearance(appearance);
    list.SetOrientation(CandidateList::Orientation::Horizontal);
    const auto measure = [&](const std::wstring &text, const std::wstring &translation) {
        list.SetItems({{L"1", text, L"", translation}});
        return list.MeasureInLayout({2000.0f, 2000.0f}).width;
    };
    const float overhead = measure(L"", L"");
    const float text = measure(L"word", L"");
    const float translation = measure(L"", L"translation");
    const float combined = measure(L"word", L"translation");
    REQUIRE_NEAR(combined, text + translation - overhead);
    REQUIRE_NEAR(list.MeasureInLayout({2000.0f, 2000.0f}).height, appearance.itemHeight);
}

TEST_CASE(single_line_candidate_display_normalizes_line_separators_without_changing_committed_text)
{
    CandidateList list;
    auto appearance = SingleLineCandidateAppearance();
    appearance.minItemWidth = 0.0f;
    list.SetAppearance(appearance);
    list.SetOrientation(CandidateList::Orientation::Horizontal);
    const std::wstring original = L"a\rb\nc\td\ve\ff\x0085g\x2028h\x2029i";
    list.SetItems({{L"1", L"a b c d e f g h i", L"", L""}});
    const SizeF normalized = list.MeasureInLayout({2000.0f, 2000.0f});
    list.SetItems({{L"1", original, L"", L""}});
    const SizeF separated = list.MeasureInLayout({2000.0f, 2000.0f});
    REQUIRE_NEAR(separated.width, normalized.width);
    REQUIRE_NEAR(separated.height, normalized.height);
    REQUIRE(list.GetItem(0)->text == original);
    appearance.horizontalSingleLine = false;
    list.SetAppearance(appearance);
    REQUIRE(list.MeasureInLayout({2000.0f, 2000.0f}).height > appearance.itemHeight);
    appearance.horizontalSingleLine = true;
    list.SetAppearance(appearance);
    REQUIRE_NEAR(list.MeasureInLayout({2000.0f, 2000.0f}).width, normalized.width);
}
