#include "settings/native/native_page_skin_common.h"

// partials/handwriting-settings.html + modules/handwriting-settings.ts + handwriting-settings.css.
namespace native_settings
{
VisualPtr BuildHandwritingPage(PageContext &context)
{
    return skinpreview::BuildPanelPage(context, L"手写识别板", L"打开手写识别板",
                                       L"使用鼠标或触控方式手写输入，自动识别候选汉字", "openHandwritingPanel",
                                       "appearance.theme_handwriting", L"handwriting_board.png",
                                       L"handwriting_board_light.png");
}
} // namespace native_settings