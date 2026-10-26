#include "window/candidate_item_geometry.h"
#include "window/candidate_horizontal_placement.h"
#include "window/candidate_card_placement.h"
#include "window/candidate_view_model.h"
#include "tests/includes/test_framework.h"

#include <limits>

TEST_CASE(candidate_card_edges_keep_text_in_work_area_at_all_supported_scales)
{
    for (const double scale : {1.0, 1.25, 1.5, 2.0})
    {
        // Negative coordinates, a left dock and a top taskbar are deliberate.
        const FanyImeUi::CandidateWorkAreaPx work{-2500, -1320, -100, 0};
        for (const int caretX : {work.left - 30, work.left, work.left + 1, work.right - 1, work.right + 30})
            for (const int caretY : {work.top - 10, work.top + 1, work.top + 60, work.bottom - 1, work.bottom + 10})
            {
                const auto pos = FanyImeUi::PlaceCandidateCard(caretX, caretY, work, scale, 600.3, 75.7, 75.7);
                REQUIRE(pos.has_value());
                const int pad = static_cast<int>(std::round(2.0 * scale));
                REQUIRE(pos->left >= work.left + pad);
                REQUIRE(pos->left + std::ceil(600.3 * scale) <= work.right - pad);
                REQUIRE(pos->top >= work.top + pad);
                REQUIRE(pos->top + std::ceil(75.7 * scale) <= work.bottom - pad);
                if (caretY == work.bottom - 1)
                {
                    REQUIRE(pos->above);
                    REQUIRE(pos->top + std::ceil(75.7 * scale) <= caretY - std::round(24.0 * scale));
                }
                if (caretY == work.top + 60)
                    REQUIRE(!pos->above);
            }
    }
}

TEST_CASE(candidate_card_tall_history_does_not_force_short_list_over_top_input_line)
{
    const auto pos = FanyImeUi::PlaceCandidateCard(400, 100, {0, 0, 800, 400}, 1.25, 200, 70, 350);
    REQUIRE(pos.has_value());
    REQUIRE(!pos->above);
    REQUIRE_EQ(pos->top, 104);
}

TEST_CASE(candidate_card_fractional_bottom_fit_uses_full_ceil_height_and_dpi_padding)
{
    // At 125% the card needs 95 pixels, not 94. A bottom taskbar starts at 1040.
    const auto fits = FanyImeUi::PlaceCandidateCard(100, 938, {0, 0, 1920, 1040}, 1.25, 400, 75.3, 75.3);
    REQUIRE(fits.has_value());
    REQUIRE(!fits->above);
    REQUIRE_EQ(fits->top, 942);
    const auto flips = FanyImeUi::PlaceCandidateCard(100, 939, {0, 0, 1920, 1040}, 1.25, 400, 75.3, 75.3);
    REQUIRE(flips.has_value());
    REQUIRE(flips->above);
    REQUIRE_EQ(flips->top, 814);
}

TEST_CASE(candidate_card_rejects_invalid_geometry_and_handles_tiny_work_area)
{
    REQUIRE(!FanyImeUi::PlaceCandidateCard(0, 0, {0, 0, 10, 10}, 0.0, 1, 1, 1));
    REQUIRE(!FanyImeUi::PlaceCandidateCard(0, 0, {0, 0, 10, 10}, 1.0, std::numeric_limits<double>::quiet_NaN(), 1, 1));
    REQUIRE(!FanyImeUi::PlaceCandidateCard(0, 0, {0, 0, 0, 10}, 1.0, 1, 1, 1));
    const auto tiny = FanyImeUi::PlaceCandidateCard(500, 500, {-1, -1, 0, 0}, 2.0, 20, 20, 20);
    REQUIRE(tiny.has_value());
    REQUIRE_EQ(tiny->left, -1);
    REQUIRE_EQ(tiny->top, -1);
}

TEST_CASE(candidate_reserved_left_keeps_observed_growth_on_the_right)
{
    FanyImeUi::CandidateReservedLeft placement;
    const FanyImeUi::CandidateWorkAreaPx work{0, 0, 2880, 1800};
    const auto first = placement.Place(1800, work, 2.0, 1032.0);
    REQUIRE(first.has_value());
    REQUIRE(first->captured);
    REQUIRE_EQ(first->cardLeftPx, 812);
    const int cardLeftOffsetPx = 64; // Transparent 32 DIP shadow: not reserved again.
    const int hostLeft = first->cardLeftPx - cardLeftOffsetPx;
    int previousRight = 0;
    for (const int actualCardWidth : {1387, 1441, 2064, 1387})
    {
        const auto current = placement.Place(1800, work, 2.0, 1032.0);
        REQUIRE(current.has_value());
        REQUIRE(!current->captured);
        REQUIRE_EQ(current->cardLeftPx - cardLeftOffsetPx, hostLeft);
        const int right = current->cardLeftPx + actualCardWidth;
        REQUIRE(right <= work.right - 4);
        if (actualCardWidth == 1441)
            REQUIRE_EQ(right - previousRight, 54);
        previousRight = right;
    }
}

TEST_CASE(candidate_reserved_left_reserves_card_only_at_each_monitor_scale)
{
    struct ScaleCase
    {
        double scale;
        int workRight;
        int caretX;
        int expectedLeft;
    };
    const ScaleCase cases[] = {
        {1.0, 1440, 900, 406}, {1.25, 1800, 1125, 507}, {1.5, 2160, 1350, 609}, {2.0, 2880, 1800, 812}};
    for (const auto &test : cases)
    {
        FanyImeUi::CandidateReservedLeft placement;
        const auto result = placement.Place(test.caretX, {0, 40, test.workRight, 1600}, test.scale, 1032.0);
        REQUIRE(result.has_value());
        REQUIRE_EQ(result->cardLeftPx, test.expectedLeft);
        REQUIRE_EQ(result->cardLeftPx + static_cast<int>(std::ceil(1032.0 * test.scale)),
                   test.workRight - static_cast<int>(std::round(2.0 * test.scale)));
    }
}

TEST_CASE(candidate_reserved_left_follows_composition_lifetime_and_not_horizontal_caret_advances)
{
    FanyImeUi::CandidateReservedLeft placement;
    const FanyImeUi::CandidateWorkAreaPx work{0, 0, 3840, 2160};
    REQUIRE_EQ(placement.Place(200, work, 2.0, 1032.0)->cardLeftPx, 200);
    const auto advanced = placement.Place(500, work, 2.0, 1032.0);
    REQUIRE_EQ(advanced->cardLeftPx, 200);
    REQUIRE(!advanced->captured);
    placement.Reset(); // Presenter Hide/skin change.
    const auto nextComposition = placement.Place(500, work, 2.0, 1032.0);
    REQUIRE_EQ(nextComposition->cardLeftPx, 500);
    REQUIRE(nextComposition->captured);
}

TEST_CASE(candidate_reserved_left_recaptures_for_monitor_dpi_work_area_and_maximum_changes)
{
    FanyImeUi::CandidateReservedLeft placement;
    REQUIRE(placement.Place(100, {0, 0, 2880, 1800}, 2.0, 1032.0)->captured);
    const auto negativeMonitor = placement.Place(-600, {-2560, -1440, 0, 0}, 1.25, 1032.0);
    REQUIRE(negativeMonitor->captured);
    REQUIRE_EQ(negativeMonitor->cardLeftPx, -1293);
    const auto workAreaChanged = placement.Place(-1700, {-2560, -1400, 0, 0}, 1.25, 1032.0);
    REQUIRE(workAreaChanged->captured);
    REQUIRE_EQ(workAreaChanged->cardLeftPx, -1700);
    const auto dpiChanged = placement.Place(-600, {-2560, -1400, 0, 0}, 2.0, 1032.0);
    REQUIRE(dpiChanged->captured);
    REQUIRE_EQ(dpiChanged->cardLeftPx, -2068);
    const auto maximumChanged = placement.Place(-600, {-2560, -1400, 0, 0}, 2.0, 800.0);
    REQUIRE(maximumChanged->captured);
    REQUIRE_EQ(maximumChanged->cardLeftPx, -1604);
}

TEST_CASE(candidate_reserved_left_handles_narrow_and_very_wide_work_areas)
{
    struct WorkCase
    {
        int left;
        int right;
        int caret;
        int expected;
    };
    const WorkCase cases[] = {{0, 800, 700, 4}, {100, 101, 100, 100}, {-4, 0, -1, -3}, {-16000, 16000, 15000, 13932}};
    for (const auto &test : cases)
    {
        FanyImeUi::CandidateReservedLeft placement;
        const auto result = placement.Place(test.caret, {test.left, 0, test.right, 1200}, 2.0, 1032.0);
        REQUIRE(result.has_value());
        REQUIRE_EQ(result->cardLeftPx, test.expected);
        REQUIRE(result->cardLeftPx >= test.left && result->cardLeftPx < test.right);
    }
    FanyImeUi::CandidateReservedLeft extreme;
    const auto result =
        extreme.Place(std::numeric_limits<int>::max(),
                      {std::numeric_limits<int>::min(), 0, std::numeric_limits<int>::max(), 1}, 1.0, 1032.0);
    REQUIRE(result.has_value());
    REQUIRE_EQ(result->cardLeftPx, std::numeric_limits<int>::max() - 1034);
}

TEST_CASE(candidate_reserved_left_rejects_invalid_inputs_without_preserving_a_stale_anchor)
{
    FanyImeUi::CandidateReservedLeft placement;
    const FanyImeUi::CandidateWorkAreaPx work{0, 0, 2880, 1800};
    REQUIRE(placement.Place(100, work, 2.0, 1032.0).has_value());
    for (const double scale :
         {0.0, -1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
        REQUIRE(!placement.Place(100, work, scale, 1032.0).has_value());
    REQUIRE(!placement.Place(100, work, 2.0, 0.0).has_value());
    REQUIRE(!placement.Place(100, work, 2.0, std::numeric_limits<double>::infinity()).has_value());
    REQUIRE(!placement.Place(100, {0, 0, 0, 1800}, 2.0, 1032.0).has_value());
    const auto validAgain = placement.Place(300, work, 2.0, 1032.0);
    REQUIRE(validAgain->captured);
    REQUIRE_EQ(validAgain->cardLeftPx, 300);
}

TEST_CASE(candidate_item_geometry_uses_actual_variable_width_bounds_and_gap_midpoints)
{
    const auto row = CandidateMainRowGeometryFromBounds(
        {20.0f, 30.0f, 500.0f, 50.0f},
        {{35.0f, 40.0f, 45.0f, 30.0f}, {90.0f, 40.0f, 125.0f, 30.0f}, {225.0f, 40.0f, 75.0f, 30.0f}});
    REQUIRE_EQ(row.edgesDip, (std::vector<float>{65.0f, 200.0f}));
    REQUIRE_EQ(row.rangeDip, (std::vector<float>{15.0f, 280.0f}));
    REQUIRE_EQ(CandidateGeometryDipArray(row.edgesDip), std::wstring(L"[65.000,200.000]"));
}

TEST_CASE(candidate_item_geometry_changes_when_total_width_does_not)
{
    const CandidateItemRectDip card{0.0f, 0.0f, 500.0f, 50.0f};
    const auto before =
        CandidateMainRowGeometryFromBounds(card, {{10.0f, 10.0f, 50.0f, 30.0f}, {70.0f, 10.0f, 130.0f, 30.0f}});
    const auto after =
        CandidateMainRowGeometryFromBounds(card, {{10.0f, 10.0f, 100.0f, 30.0f}, {120.0f, 10.0f, 80.0f, 30.0f}});
    REQUIRE_EQ(before.rangeDip, after.rangeDip);
    REQUIRE(before.edgesDip != after.edgesDip);
    REQUIRE_EQ(before.edgesDip, (std::vector<float>{65.0f}));
    REQUIRE_EQ(after.edgesDip, (std::vector<float>{115.0f}));
}

TEST_CASE(candidate_item_geometry_exports_only_first_wrapped_row)
{
    const auto row = CandidateMainRowGeometryFromBounds(
        {20.0f, 30.0f, 300.0f, 100.0f},
        {{35.0f, 40.0f, 80.0f, 30.0f}, {125.0f, 40.0f, 90.0f, 30.0f}, {35.0f, 75.0f, 200.0f, 30.0f}});
    REQUIRE_EQ(row.edgesDip, (std::vector<float>{100.0f}));
    REQUIRE_EQ(row.rangeDip, (std::vector<float>{15.0f, 195.0f}));
}

TEST_CASE(candidate_item_geometry_clears_for_zero_items_and_keeps_single_item_range)
{
    const CandidateItemRectDip card{20.0f, 30.0f, 300.0f, 100.0f};
    const auto empty = CandidateMainRowGeometryFromBounds(card, {});
    REQUIRE(empty.edgesDip.empty());
    REQUIRE(empty.rangeDip.empty());
    REQUIRE_EQ(CandidateGeometryDipArray(empty.edgesDip), std::wstring(L"[]"));
    REQUIRE_EQ(CandidateGeometryDipArray(empty.rangeDip), std::wstring(L"[]"));
    const auto single = CandidateMainRowGeometryFromBounds(card, {{35.0f, 40.0f, 80.0f, 30.0f}});
    REQUIRE(single.edgesDip.empty());
    REQUIRE_EQ(single.rangeDip, (std::vector<float>{15.0f, 95.0f}));
}

TEST_CASE(candidate_item_geometry_rejects_stale_outside_overlapping_and_nonfinite_bounds)
{
    const CandidateItemRectDip card{20.0f, 30.0f, 300.0f, 100.0f};
    const std::vector<std::vector<CandidateItemRectDip>> invalid{
        {{15.0f, 40.0f, 80.0f, 30.0f}},
        {{35.0f, 40.0f, 300.0f, 30.0f}},
        {{35.0f, 40.0f, 80.0f, 30.0f}, {100.0f, 40.0f, 80.0f, 30.0f}},
        {{35.0f, 40.0f, std::numeric_limits<float>::infinity(), 30.0f}},
        {{35.0f, std::numeric_limits<float>::quiet_NaN(), 80.0f, 30.0f}},
        {{35.0f, 40.0f, 80.0f, 0.0f}}};
    for (const auto &items : invalid)
    {
        const auto row = CandidateMainRowGeometryFromBounds(card, items);
        REQUIRE(row.edgesDip.empty());
        REQUIRE(row.rangeDip.empty());
    }
}

TEST_CASE(candidate_item_geometry_remains_card_local_across_moves_and_dpi)
{
    // The layout gives DIPs. Only the consumer multiplies these by its monitor
    // scale; desktop position and 100/125/150/200% must not alter the arrays.
    for (const float scale : {1.0f, 1.25f, 1.5f, 2.0f})
    {
        const auto row = CandidateMainRowGeometryFromBounds(
            {152.0f, 200.0f, 400.0f, 60.0f}, {{164.0f, 210.0f, 80.0f, 30.0f}, {252.0f, 210.0f, 120.0f, 30.0f}});
        REQUIRE_EQ(row.edgesDip, (std::vector<float>{96.0f}));
        REQUIRE_EQ(row.rangeDip, (std::vector<float>{12.0f, 220.0f}));
        const float cardScreenX = 600.0f;
        const float edgeScreenX = cardScreenX + row.edgesDip.front() * scale;
        REQUIRE_EQ((edgeScreenX - cardScreenX) / scale, 96.0f);
    }
}

TEST_CASE(candidate_item_geometry_keeps_tolerated_roundoff_inside_card)
{
    const CandidateItemRectDip card{0.0f, 0.0f, 300.0f, 100.0f};
    const auto row =
        CandidateMainRowGeometryFromBounds(card, {{-0.005f, 10.0f, 80.0f, 30.0f}, {90.0f, 10.0f, 210.005f, 30.0f}});
    REQUIRE_EQ(row.rangeDip, (std::vector<float>{0.0f, 300.0f}));
    REQUIRE_EQ(row.edgesDip.size(), size_t{1});
    REQUIRE(row.edgesDip.front() > 0.0f && row.edgesDip.front() < 300.0f);
    for (const float x : {-0.005f, 300.001f})
    {
        const auto outside = CandidateMainRowGeometryFromBounds(card, {{x, 10.0f, 0.001f, 30.0f}});
        REQUIRE(outside.edgesDip.empty());
        REQUIRE(outside.rangeDip.empty());
    }
}

TEST_CASE(candidate_view_escapes_text_and_translation_without_turning_them_into_markup)
{
    CandidateViewItem item;
    item.text = "<&,\"";
    item.annotation = " AB";
    item.badge = " cloud";
    item.translation = "<gloss>,";
    item.fixed_position = true;
    REQUIRE_EQ(CandidateViewHtml(item), std::string("<span style=\"color:#379AD3\">&lt;&amp;,&quot; AB cloud</span>"
                                                    "<span class=\"cand-translation\">&lt;gloss&gt;,</span>"));
    REQUIRE_EQ(item.text, std::string("<&,\""));
}

TEST_CASE(candidate_view_keeps_commas_and_private_use_characters)
{
    CandidateViewItem item;
    item.text = "a,\xEF\x80\x80";
    item.translation = "b,\xEF\x80\x80";
    REQUIRE_EQ(CandidateViewHtml(item),
               std::string("a,\xEF\x80\x80<span class=\"cand-translation\">b,\xEF\x80\x80</span>"));
}

// 词性与分隔符各包一层 span，皮肤靠 .cand-pos / .cand-sep 给它们单独配色；分段里的文本同样要转义。
TEST_CASE(candidate_view_wraps_part_of_speech_and_separators_in_translation)
{
    CandidateViewItem item;
    item.text = "笑";
    item.translation = "adj. <spreading>; n. laugh";
    REQUIRE_EQ(CandidateViewHtml(item), std::string("笑<span class=\"cand-translation\">"
                                                    "<span class=\"cand-pos\">adj.</span> &lt;spreading&gt;"
                                                    "<span class=\"cand-sep\">;</span> "
                                                    "<span class=\"cand-pos\">n.</span> laugh</span>"));
}

TEST_CASE(candidate_view_pos_toggle_updates_display_and_refresh_signature)
{
    const std::string raw = "adj. <spreading>; n. laugh";
    CandidateViewItem item;
    item.text = "笑";
    item.translation = FanyImeIpc::FormatTranslationGlossForDisplay(raw, true);
    const auto shown_html = CandidateViewHtml(item);
    const auto shown_signature = CandidatePageSignature({item});
    const CandidateFramePageState page{2, 1, true, true};
    const auto shown_frame = CandidateFrameSignature(L"xiao", {item}, 10, 20, page);

    item.translation = FanyImeIpc::FormatTranslationGlossForDisplay(raw, false);
    REQUIRE_EQ(item.translation, std::string("<spreading>; laugh"));
    REQUIRE_EQ(CandidateViewHtml(item), std::string("笑<span class=\"cand-translation\">&lt;spreading&gt;"
                                                    "<span class=\"cand-sep\">;</span> laugh</span>"));
    REQUIRE(shown_signature != CandidatePageSignature({item}));
    REQUIRE(shown_frame != CandidateFrameSignature(L"xiao", {item}, 10, 20, page));

    item.translation = FanyImeIpc::FormatTranslationGlossForDisplay(raw, true);
    REQUIRE_EQ(CandidateViewHtml(item), shown_html);
    REQUIRE_EQ(CandidatePageSignature({item}), shown_signature);
    REQUIRE_EQ(CandidateFrameSignature(L"xiao", {item}, 10, 20, page), shown_frame);
}

TEST_CASE(candidate_page_signature_distinguishes_comma_split_between_items)
{
    CandidateViewItem ab, c, a, bc;
    ab.text = "a,b";
    c.text = "c";
    a.text = "a";
    bc.text = "b,c";
    REQUIRE(CandidatePageSignature({ab, c}) != CandidatePageSignature({a, bc}));
    REQUIRE_EQ(CandidatePageSignature({ab, c}), CandidatePageSignature({ab, c}));
}

TEST_CASE(candidate_frame_signature_includes_page_state)
{
    CandidateViewItem item;
    item.text = "candidate";
    const std::vector<CandidateViewItem> views{item};
    const CandidateFramePageState page{0, 1, false, true};
    const std::wstring original = CandidateFrameSignature(L"preedit", views, 10, 20, page);

    REQUIRE_EQ(original, CandidateFrameSignature(L"preedit", views, 10, 20, page));
    CandidateFramePageState changed = page;
    changed.page_index = 2;
    REQUIRE(original != CandidateFrameSignature(L"preedit", views, 10, 20, changed));
    changed = page;
    changed.has_previous_page = true;
    REQUIRE(original != CandidateFrameSignature(L"preedit", views, 10, 20, changed));
    changed = page;
    changed.has_next_page = false;
    REQUIRE(original != CandidateFrameSignature(L"preedit", views, 10, 20, changed));
}

TEST_CASE(candidate_frame_signature_keeps_preedit_selection_and_caret)
{
    CandidateViewItem first, second;
    first.text = "first";
    second.text = "second";
    const std::vector<CandidateViewItem> views{first, second};
    const CandidateFramePageState page{0, 1, false, true};
    const std::wstring original = CandidateFrameSignature(L"preedit", views, 10, 20, page);

    CandidateFramePageState changed = page;
    changed.selected_index = 1;
    REQUIRE(original != CandidateFrameSignature(L"preedit", views, 10, 20, changed));
    REQUIRE(original != CandidateFrameSignature(L"edited", views, 10, 20, page));
    REQUIRE(original != CandidateFrameSignature(L"preedit", views, 11, 20, page));
    REQUIRE(original != CandidateFrameSignature(L"preedit", views, 10, 21, page));
}

TEST_CASE(candidate_frame_signature_keeps_length_prefixed_candidate_identity)
{
    CandidateViewItem ab, c, a, bc;
    ab.text = "a,b";
    c.text = "c";
    a.text = "a";
    bc.text = "b,c";

    const CandidateFramePageState page{0, 0, false, true};
    REQUIRE(CandidateFrameSignature(L"", {ab, c}, 10, 20, page) != CandidateFrameSignature(L"", {a, bc}, 10, 20, page));
}

TEST_CASE(fixed_position_candidates_get_pin_badge_visible_in_both_backends)
{
    CandidateViewItem item;
    item.text = "你好";
    item.badge = " cloud";
    item.fixed_position = true;
    ApplyFixedPositionBadge(item, true, "paperclip");
    // 追加而非覆盖：徽标并存时顺序稳定；候选串里固定词条整体落在蓝色 span 内。
    REQUIRE_EQ(item.badge, std::string(" cloud \xF0\x9F\x93\x8E"));
    REQUIRE_EQ(CandidateViewHtml(item),
               std::string("<span style=\"color:#379AD3\">你好 cloud \xF0\x9F\x93\x8E</span>"));

    // 样式枚举逐个映射，存储的是名字而不是字面 emoji
    CandidateViewItem pinned;
    pinned.text = "钉";
    pinned.fixed_position = true;
    ApplyFixedPositionBadge(pinned, true, "pushpin");
    REQUIRE_EQ(pinned.badge, std::string(" \xF0\x9F\x93\x8C"));
    CandidateViewItem dotted;
    dotted.text = "点";
    dotted.fixed_position = true;
    ApplyFixedPositionBadge(dotted, true, "dot");
    REQUIRE_EQ(dotted.badge, std::string(" \xC2\xB7"));

    // 开关关闭：不加徽标，但整条仍走蓝色 span（固定状态本身还有颜色提示）
    CandidateViewItem hidden;
    hidden.text = "你好";
    hidden.fixed_position = true;
    ApplyFixedPositionBadge(hidden, false, "paperclip");
    REQUIRE(hidden.badge.empty());
    REQUIRE_EQ(CandidateViewHtml(hidden), std::string("<span style=\"color:#379AD3\">你好</span>"));

    // 未列入白名单的样式不加徽标，手改 TOML 写错也不会渲染出畸形标记
    CandidateViewItem bogus;
    bogus.text = "你好";
    bogus.fixed_position = true;
    ApplyFixedPositionBadge(bogus, true, "smiley");
    REQUIRE(bogus.badge.empty());

    // 未固定的词条不加徽标。
    CandidateViewItem plain;
    plain.text = "世界";
    ApplyFixedPositionBadge(plain, true, "paperclip");
    REQUIRE(plain.badge.empty());
    REQUIRE_EQ(CandidateViewHtml(plain), std::string("世界"));
}

TEST_CASE(ai_suggestion_badge_switches_between_robot_and_whale)
{
    // 出厂默认是 🤖：与改动前的硬编码值逐字节一致，升级不改变默认观感。
    REQUIRE_EQ(AiSuggestionBadge("robot"), std::string(" \xF0\x9F\xA4\x96"));
    REQUIRE_EQ(AiSuggestionBadge("whale"), std::string(" \xF0\x9F\x90\xB3"));

    // 映射表同时是配置层白名单：已知样式有各自的字形，未知样式没有。
    REQUIRE(AiSuggestionBadgeGlyph("robot") != nullptr);
    REQUIRE(AiSuggestionBadgeGlyph("whale") != nullptr);
    REQUIRE(AiSuggestionBadgeGlyph("") == nullptr);
    REQUIRE(AiSuggestionBadgeGlyph("smiley") == nullptr);

    // 未列入白名单的样式按出厂默认处理，手改 TOML 写错也不会让 AI 候选失去来源标识。
    REQUIRE_EQ(AiSuggestionBadge(""), AiSuggestionBadge("robot"));
    REQUIRE_EQ(AiSuggestionBadge("smiley"), AiSuggestionBadge("robot"));

    // 徽标经既有 badge 通道渲染进词条文本，两种样式在两个后端都可见。
    CandidateViewItem whale;
    whale.text = "联想";
    whale.badge = AiSuggestionBadge("whale");
    REQUIRE_EQ(whale.badge, std::string(" \xF0\x9F\x90\xB3"));
    REQUIRE_EQ(CandidateViewHtml(whale), std::string("联想 \xF0\x9F\x90\xB3"));

    CandidateViewItem robot;
    robot.text = "联想";
    robot.badge = AiSuggestionBadge("robot");
    REQUIRE_EQ(robot.badge, std::string(" \xF0\x9F\xA4\x96"));
    REQUIRE_EQ(CandidateViewHtml(robot), std::string("联想 \xF0\x9F\xA4\x96"));

    // AI 徽标是覆盖而非追加：同一词条既固定又是 AI 候选时，两个徽标并存且各占一段。
    CandidateViewItem both;
    both.text = "联想";
    both.fixed_position = true;
    both.badge = AiSuggestionBadge("whale");
    ApplyFixedPositionBadge(both, true, "paperclip");
    REQUIRE_EQ(both.badge, std::string(" \xF0\x9F\x90\xB3 \xF0\x9F\x93\x8E"));
}
