#include "tests/includes/test_framework.h"
#include "window/tray_menu_placement.h"

TEST_CASE(tray_menu_top_taskbar_opens_below_at_each_scale)
{
    for (const int dpi : {96, 120, 144, 192})
    {
        const auto px = [dpi](int dip) { return MulDiv(dip, dpi, 96); };
        const RECT icon{px(960), 0, px(984), px(40)};
        const MonitorCoordinates work{0, px(40), px(1920), px(1080)};
        for (const bool shadow : {false, true})
        {
            const int left = shadow ? px(16) : 0;
            const int top = shadow ? px(12) : 0;
            const RECT content{left, top, left + px(200), top + px(300)};
            const POINT pos = FanyImeUi::TrayMenuPosition(icon, content, work);
            REQUIRE_EQ(pos.x + content.left, px(872));
            REQUIRE_EQ(pos.y + content.top, icon.bottom);
            REQUIRE(pos.y + content.bottom <= work.bottom);
        }
    }
}

TEST_CASE(tray_menu_bottom_taskbar_keeps_card_above_icon_at_each_scale)
{
    for (const int dpi : {96, 120, 144, 192})
    {
        const auto px = [dpi](int dip) { return MulDiv(dip, dpi, 96); };
        const RECT icon{px(960), px(1040), px(984), px(1080)};
        const MonitorCoordinates work{0, 0, px(1920), px(1040)};
        for (const bool shadow : {false, true})
        {
            const int left = shadow ? px(16) : 0;
            const int top = shadow ? px(12) : 0;
            const RECT content{left, top, left + px(200), top + px(300)};
            const POINT pos = FanyImeUi::TrayMenuPosition(icon, content, work);
            REQUIRE_EQ(pos.x + content.left, px(872));
            REQUIRE_EQ(pos.y + content.bottom, icon.top);
            REQUIRE(pos.y + content.top >= work.top);
        }
    }
}

TEST_CASE(tray_menu_side_taskbars_keep_card_inside_work_area)
{
    const RECT content{16, 12, 216, 312};
    const POINT left = FanyImeUi::TrayMenuPosition({0, 600, 80, 640}, content, {80, 0, 1920, 1080});
    REQUIRE_EQ(left.x + content.left, 80);
    REQUIRE_EQ(left.y + content.bottom, 600);
    const POINT right = FanyImeUi::TrayMenuPosition({1840, 600, 1920, 640}, content, {0, 0, 1840, 1080});
    REQUIRE_EQ(right.x + content.right, 1840);
    REQUIRE_EQ(right.y + content.bottom, 600);
}

TEST_CASE(tray_menu_uses_negative_monitor_coordinates)
{
    const RECT icon{-160, -1080, -120, -1040};
    const RECT content{0, 0, 400, 600};
    const POINT pos = FanyImeUi::TrayMenuPosition(icon, content, {-1920, -1040, 0, 0});
    REQUIRE_EQ(pos.x, -400);
    REQUIRE_EQ(pos.y, -1040);
}

TEST_CASE(tray_menu_remeasured_content_stays_inside_work_area)
{
    const MonitorCoordinates topWork{0, 40, 1920, 1080};
    const RECT topIcon{1880, 0, 1920, 40};
    const MonitorCoordinates bottomWork{0, 0, 1920, 1040};
    const RECT bottomIcon{1880, 1040, 1920, 1080};
    for (const RECT content : {RECT{0, 0, 200, 300}, RECT{0, 0, 600, 500}})
    {
        const POINT top = FanyImeUi::TrayMenuPosition(topIcon, content, topWork);
        REQUIRE_EQ(top.x + content.right, topWork.right);
        REQUIRE_EQ(top.y, topWork.top);
        const POINT bottom = FanyImeUi::TrayMenuPosition(bottomIcon, content, bottomWork);
        REQUIRE_EQ(bottom.x + content.right, bottomWork.right);
        REQUIRE_EQ(bottom.y + content.bottom, bottomWork.bottom);
    }
}
