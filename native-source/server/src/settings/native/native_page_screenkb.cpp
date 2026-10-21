#include "settings/native/native_page_skin_common.h"

// partials/screenkb-settings.html + modules/screenkb-settings.ts + screenkb-settings.css.
namespace native_settings
{
VisualPtr BuildScreenKeyboardPage(PageContext &context)
{
    return skinpreview::BuildPanelPage(context, L"屏幕键盘", L"打开屏幕键盘", L"使用鼠标或触控方式输入文字与快捷按键",
                                       "openKeyboardPanel", "appearance.theme_screen_keyboard", L"softkbd.png",
                                       L"softkbd_light.png");
}
} // namespace native_settings