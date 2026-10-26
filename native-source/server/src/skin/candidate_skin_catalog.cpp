#include "skin/candidate_skin_catalog.h"

#include <toml++/toml.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iterator>
#include <string_view>
#include <system_error>

namespace CandidateSkinCatalog
{
namespace
{
// 共享基底来自 msime 的固定主题（crates/client-core/src/theme.rs，上游调整时需同步）；
// Windows 只复用 Fluent 的结构，配色在加载时补进现有槽位。system 跟随宿主，没有调色板。
struct SharedBase
{
    const char *id;
    const char *theme;
    const char *surface;
    const char *text;
    const char *accent;
    const char *number;
};

const SharedBase *FindSharedBase(const std::string &id)
{
    static constexpr SharedBase bases[] = {
        {"system", nullptr, nullptr, nullptr, nullptr, nullptr},
        {"shuishan", "dark", "#2A2B27", "#FFFFFF", "#7FE08E", "#9FB5A3"},
        {"light", "light", "#FFFFFF", "#1A1A1A", "#005FB8", "#6A6F76"},
        {"paper", "light", "#F7F5F0", "#1A1E1B", "#2C7A4B", "#6E6A5E"},
        {"night", "dark", "#16262F", "#E6F1F4", "#4FD1C5", "#86A6B0"},
        {"ink", "dark", "#1A1A1A", "#9A9A9A", "#FFFFFF", "#9A9A9A"},
    };
    for (const auto &base : bases)
    {
        if (id == base.id)
        {
            return &base;
        }
    }
    return nullptr;
}

void FillColor(std::string &color, const std::string &fallback)
{
    if (color.empty())
    {
        color = fallback;
    }
}

// 给十六进制颜色换上指定透明度；清单里的 rgba()、颜色名等无法直接拼接的写法退回基底原色。
std::string WithAlpha(const std::string &color, const char *fallback, const char *alpha)
{
    const bool hex = color.size() > 1 && color.front() == '#' &&
                     std::all_of(color.begin() + 1, color.end(), [](unsigned char ch) { return std::isxdigit(ch); });
    if (hex && (color.size() == 7 || color.size() == 9))
    {
        return color.substr(0, 7) + alpha;
    }
    if (hex && (color.size() == 4 || color.size() == 5))
    {
        return std::string{'#', color[1], color[1], color[2], color[2], color[3], color[3]} + alpha;
    }
    return std::string(fallback) + alpha;
}

void AdaptSharedBase(Package &package)
{
    const SharedBase *base = FindSharedBase(package.base);
    if (!base)
    {
        return;
    }
    // 所有使用方（含设置预览）仍按 Fluent 的结构处理。
    package.base = "fluent";
    if (!base->theme)
    {
        return;
    }
    package.themes.erase(std::remove_if(package.themes.begin(), package.themes.end(),
                                        [base](const std::string &theme) { return theme != base->theme; }),
                         package.themes.end());
    CandidateColors &colors = std::string_view(base->theme) == "light" ? package.light : package.dark;
    FillColor(colors.surface, base->surface);
    FillColor(colors.border, "#0000001F");
    FillColor(colors.text, base->text);
    FillColor(colors.number, base->number);
    FillColor(colors.accent, base->accent);
    // 与上游一致，派生槽位跟随清单覆盖后的 accent / text，而不是基底原色。
    FillColor(colors.selected, WithAlpha(colors.accent, base->accent, "24"));
    FillColor(colors.hover, WithAlpha(colors.text, base->text, "0F"));
    FillColor(colors.selectedText, colors.accent);
    FillColor(colors.selectedNumber, colors.number);
    FillColor(colors.translation, colors.number);
    // 菜单与工具栏默认跟随最终候选配色，各自显式设置的颜色始终优先。
    FillColor(colors.menuBackground, colors.surface);
    FillColor(colors.menuBorder, colors.border);
    FillColor(colors.menuText, colors.text);
    FillColor(colors.menuHover, colors.hover);
    ToolbarColors &toolbar = std::string_view(base->theme) == "light" ? package.toolbarLight : package.toolbarDark;
    FillColor(toolbar.background, colors.surface);
    FillColor(toolbar.border, colors.border);
    FillColor(toolbar.handle, colors.accent);
    FillColor(toolbar.divider, colors.border);
    FillColor(toolbar.icon, colors.text);
    FillColor(toolbar.hover, colors.hover);
}

void SetError(std::string *error, const std::string &message)
{
    if (error)
    {
        *error = message;
    }
}

bool IsSafeRelativeResource(const std::string &name)
{
    if (name.empty() || name.size() > 256 || name.front() == '/' || name.front() == '\\' ||
        name.find('\\') != std::string::npos)
    {
        return false;
    }
    if (!std::all_of(name.begin(), name.end(), [](unsigned char ch) {
            return std::isalnum(ch) || ch == '/' || ch == '.' || ch == '_' || ch == '-';
        }))
    {
        return false;
    }
    std::filesystem::path path(name);
    if (path.is_absolute())
    {
        return false;
    }
    for (const auto &part : path)
    {
        if (part == ".." || part == "." || part.empty())
        {
            return false;
        }
    }
    return true;
}

bool ReadString(const toml::table &table, const char *key, std::string &out, size_t maximum, bool required)
{
    const toml::node *node = table.get(key);
    if (!node)
    {
        return !required;
    }
    const auto *value = node->as_string();
    if (!value)
    {
        return false;
    }
    out = value->get();
    return (!required || !out.empty()) && out.size() <= maximum;
}

bool ReadEnumArray(const toml::table &table, const char *key, const std::vector<std::string> &allowed,
                   std::vector<std::string> &out)
{
    const toml::node *node = table.get(key);
    if (!node)
    {
        return false;
    }
    const auto *array = node->as_array();
    if (!array || array->empty())
    {
        return false;
    }
    for (const auto &item : *array)
    {
        const auto *value = item.as_string();
        if (!value)
        {
            return false;
        }
        const std::string text = value->get();
        if (std::find(allowed.begin(), allowed.end(), text) == allowed.end() ||
            std::find(out.begin(), out.end(), text) != out.end())
        {
            return false;
        }
        out.push_back(text);
    }
    return true;
}

double BoundedNumber(const toml::table &table, const char *key, double maximum)
{
    const toml::node *node = table.get(key);
    if (!node)
    {
        return 0.0;
    }
    if (const auto *floating = node->as_floating_point())
    {
        const double value = floating->get();
        return std::isfinite(value) && value >= 0.0 && value <= maximum ? value : -1.0;
    }
    if (const auto *integer = node->as_integer())
    {
        const double value = static_cast<double>(integer->get());
        return value >= 0.0 && value <= maximum ? value : -1.0;
    }
    return -1.0;
}

bool ReadEnum(const toml::table &table, const char *key, const std::vector<std::string> &allowed, std::string &out)
{
    if (!table.contains(key))
    {
        return true;
    }
    std::string text;
    if (!ReadString(table, key, text, 32, true) || std::find(allowed.begin(), allowed.end(), text) == allowed.end())
    {
        return false;
    }
    out = text;
    return true;
}

bool ReadResource(const toml::table &table, const char *key, std::string &out)
{
    return !table.contains(key) || (ReadString(table, key, out, 256, true) && IsSafeRelativeResource(out));
}

// 颜色按 CSS 颜色写法解析，只放行颜色值会用到的字符，挡住 `;`、`{}` 之类的写法：同一份 skin.toml
// 在各平台通用，这里不能比别处宽松。
bool ReadCssColor(const toml::table &table, const char *key, std::string &out)
{
    return ReadString(table, key, out, 80, false) && std::all_of(out.begin(), out.end(), [](unsigned char ch) {
               return std::isalnum(ch) || ch == '#' || ch == '(' || ch == ')' || ch == ',' || ch == '.' || ch == '%' ||
                      ch == ' ' || ch == '-' || ch == '/';
           });
}

bool ReadOptionalBounded(const toml::table &table, const char *key, double maximum, std::optional<double> &out);

bool ReadColors(const toml::table *table, CandidateColors &out)
{
    if (!table)
    {
        return true;
    }
    if (!ReadCssColor(*table, "accent", out.accent) || !ReadCssColor(*table, "selected", out.selected) ||
        !ReadCssColor(*table, "hover", out.hover) || !ReadCssColor(*table, "surface", out.surface) ||
        !ReadCssColor(*table, "border", out.border) || !ReadCssColor(*table, "text", out.text) ||
        !ReadCssColor(*table, "number", out.number) || !ReadCssColor(*table, "translation", out.translation) ||
        !ReadCssColor(*table, "translation_pos", out.translationPos) ||
        !ReadCssColor(*table, "translation_separator", out.translationSeparator) ||
        !ReadCssColor(*table, "candidate_text", out.candidateText) ||
        !ReadCssColor(*table, "preedit_text", out.preeditText) ||
        !ReadCssColor(*table, "preedit_caret", out.preeditCaret) ||
        !ReadCssColor(*table, "selected_text", out.selectedText) ||
        !ReadCssColor(*table, "selected_number", out.selectedNumber) ||
        !ReadCssColor(*table, "selected_translation", out.selectedTranslation) ||
        !ReadCssColor(*table, "selected_translation_pos", out.selectedTranslationPos) ||
        !ReadCssColor(*table, "selected_translation_separator", out.selectedTranslationSeparator) ||
        !ReadCssColor(*table, "selected_bar", out.selectedBar) ||
        !ReadCssColor(*table, "preedit_background", out.preeditBackground) ||
        !ReadCssColor(*table, "preedit_divider", out.preeditDivider) ||
        !ReadCssColor(*table, "page_arrow_divider", out.pageArrowDivider))
    {
        return false;
    }
    if (const toml::node *menuNode = table->get("menu"))
    {
        const auto *menu = menuNode->as_table();
        if (!menu || !ReadCssColor(*menu, "background", out.menuBackground) ||
            !ReadCssColor(*menu, "border", out.menuBorder) || !ReadCssColor(*menu, "text", out.menuText) ||
            !ReadCssColor(*menu, "hover", out.menuHover))
        {
            return false;
        }
    }
    if (const toml::node *bar = table->get("show_selected_bar"))
    {
        const auto *flag = bar->as_boolean();
        if (!flag)
        {
            return false;
        }
        out.showSelectedBar = flag->get();
    }
    return ReadOptionalBounded(*table, "backdrop_opacity", 1.0, out.backdropOpacity) &&
           ReadOptionalBounded(*table, "backdrop_luminosity", 1.0, out.backdropLuminosity);
}

// 字体族挡住引号、反斜杠、分号、花括号、尖括号和控制字符，与各平台共用的 skin.toml 规则一致；
// 字体名可以是中文，所以非 ASCII 字节一律放行。
bool ReadFontFamily(const toml::table &table, const char *key, std::string &out)
{
    if (!table.contains(key))
    {
        return true;
    }
    if (!ReadString(table, key, out, 64, true))
    {
        return false;
    }
    return std::all_of(out.begin(), out.end(), [](unsigned char ch) {
        return ch >= 0x80 || (ch >= 0x20 && ch != 0x7f &&
                              std::string_view("\"'\\,;{}<>`").find(static_cast<char>(ch)) == std::string_view::npos);
    });
}

bool ReadOptionalBounded(const toml::table &table, const char *key, double maximum, std::optional<double> &out)
{
    if (!table.contains(key))
    {
        return true;
    }
    const double value = BoundedNumber(table, key, maximum);
    if (value < 0.0)
    {
        return false;
    }
    out = value;
    return true;
}

// v2 的四边数值（切片、边宽）：1、2 或 4 个 0–500 的数，按 CSS 简写展开。键不存在时什么也不做。
bool ReadBoxValues(const toml::table &table, const char *key, std::vector<double> &out)
{
    const toml::node *node = table.get(key);
    if (!node)
    {
        return true;
    }
    const auto *array = node->as_array();
    if (!array || (array->size() != 1 && array->size() != 2 && array->size() != 4))
    {
        return false;
    }
    for (const auto &item : *array)
    {
        const auto number = item.value<double>();
        if (!number || !std::isfinite(*number) || *number < 0.0 || *number > 500.0)
        {
            return false;
        }
        out.push_back(*number);
    }
    return true;
}

// 可正可负的可选数值，缺省保持 out 不变。
bool ReadSignedNumber(const toml::table &table, const char *key, double limit, double &out)
{
    const toml::node *node = table.get(key);
    if (!node)
    {
        return true;
    }
    double value = 0.0;
    if (const auto *floating = node->as_floating_point())
    {
        value = floating->get();
    }
    else if (const auto *integer = node->as_integer())
    {
        value = static_cast<double>(integer->get());
    }
    else
    {
        return false;
    }
    if (!std::isfinite(value) || std::fabs(value) > limit)
    {
        return false;
    }
    out = value;
    return true;
}

bool ReadOptionalBool(const toml::table &table, const char *key, std::optional<bool> &out)
{
    const toml::node *node = table.get(key);
    if (!node)
    {
        return true;
    }
    const auto *flag = node->as_boolean();
    if (!flag)
    {
        return false;
    }
    out = flag->get();
    return true;
}

// Read via the wide path and parse the text. toml::parse_file(manifest.string()) would run the
// path through the ANSI code page: skins live under the user profile, so a non-ASCII (e.g.
// Chinese) user name corrupts it, and on a code page that cannot represent the characters
// path::string() throws right past the callers' toml handlers.
std::optional<toml::table> ParseManifest(const std::filesystem::path &manifest)
{
    std::ifstream input(manifest, std::ios::binary);
    if (!input)
    {
        return std::nullopt;
    }
    const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    return toml::parse(text);
}

bool ReadToolbarColors(const toml::node *node, ToolbarColors &out)
{
    if (!node)
    {
        return true;
    }
    const auto *table = node->as_table();
    return table && ReadCssColor(*table, "background", out.background) && ReadCssColor(*table, "border", out.border) &&
           ReadCssColor(*table, "handle", out.handle) && ReadCssColor(*table, "divider", out.divider) &&
           ReadCssColor(*table, "icon", out.icon) && ReadCssColor(*table, "hover", out.hover);
}

// 长度：0–maximum 的 DIP 数值，或 "<数值>em"（0–maximumEm 倍字号）。键不存在时什么也不做。
bool ReadLength(const toml::table &table, const char *key, double maximum, double maximumEm, std::optional<Length> &out)
{
    const toml::node *node = table.get(key);
    if (!node)
    {
        return true;
    }
    if (const auto *text = node->as_string())
    {
        const std::string &value = text->get();
        if (value.size() < 3 || value.compare(value.size() - 2, 2, "em") != 0)
        {
            return false;
        }
        double number = 0.0;
        const char *first = value.data();
        const char *last = value.data() + value.size() - 2;
        const auto [end, ec] = std::from_chars(first, last, number);
        if (ec != std::errc() || end != last || !std::isfinite(number) || number < 0.0 || number > maximumEm)
        {
            return false;
        }
        out = Length{number, true};
        return true;
    }
    const double number = BoundedNumber(table, key, maximum);
    if (number < 0.0)
    {
        return false;
    }
    out = Length{number, false};
    return true;
}

bool ReadGeometry(const toml::table &table, CandidateGeometry &out)
{
    if (!ReadLength(table, "padding", 64.0, 4.0, out.padding) ||
        !ReadLength(table, "item_gap", 64.0, 4.0, out.itemGap) ||
        !ReadLength(table, "item_extra_height", 64.0, 4.0, out.itemExtraHeight) ||
        !ReadLength(table, "number_inset", 64.0, 4.0, out.numberInset) ||
        !ReadLength(table, "item_padding_right", 64.0, 4.0, out.itemPaddingRight) ||
        !ReadLength(table, "item_min_width", 500.0, 40.0, out.itemMinWidth) ||
        !ReadLength(table, "number_gap", 64.0, 4.0, out.numberGap) ||
        !ReadLength(table, "selected_bar_width", 16.0, 1.0, out.selectedBarWidth) ||
        !ReadOptionalBounded(table, "number_scale", 2.0, out.numberScale) ||
        !ReadOptionalBounded(table, "selected_bar_height_em", 2.0, out.selectedBarHeightEm) ||
        !ReadOptionalBool(table, "selected_bar_inside", out.selectedBarInside) ||
        !ReadOptionalBool(table, "item_corners_follow_card", out.itemCornersFollowCard) ||
        !ReadOptionalBool(table, "justify_rows", out.justifyRows) ||
        !ReadOptionalBool(table, "single_line", out.singleLine) ||
        !ReadOptionalBool(table, "trim_hidden_preedit_gap", out.trimHiddenPreeditGap) ||
        !ReadEnum(table, "page_arrow_style", {"chevron", "triangle"}, out.pageArrowStyle))
    {
        return false;
    }
    std::vector<double> margin;
    if (!ReadBoxValues(table, "preedit_margin_dip", margin))
    {
        return false;
    }
    if (!margin.empty())
    {
        // 按 CSS 简写展开：1 个值四边相同，2 个值是上下、左右，4 个值是上右下左。
        const double top = margin[0];
        const double right = margin.size() > 1 ? margin[1] : top;
        const double bottom = margin.size() > 2 ? margin[2] : top;
        const double left = margin.size() > 3 ? margin[3] : right;
        out.preeditMarginDip = InsetsDip{top, right, bottom, left};
    }
    return true;
}

bool ReadItemSeparators(const toml::node *node, std::optional<ItemSeparators> &out)
{
    if (!node)
        return true;
    const auto *table = node->as_table();
    if (!table)
        return false;
    ItemSeparators separators;
    if (!table->contains("color") || !ReadCssColor(*table, "color", separators.color) || separators.color.empty())
        return false;
    const auto readRequired = [table](const char *key, double &value) {
        if (!table->contains(key))
            return false;
        value = BoundedNumber(*table, key, 4096.0);
        return value >= 0.0;
    };
    if (!readRequired("top_dip", separators.topDip) || !readRequired("bottom_dip", separators.bottomDip) ||
        !readRequired("line_width_dip", separators.lineWidthDip) ||
        !readRequired("line_gap_dip", separators.lineGapDip) || separators.bottomDip <= separators.topDip ||
        separators.lineWidthDip <= 0.0 || separators.lineGapDip < separators.lineWidthDip)
        return false;
    out = std::move(separators);
    return true;
}

bool ReadBandArtwork(const toml::node *node, std::optional<BandArtwork> &out)
{
    if (!node)
        return true;
    const auto *table = node->as_table();
    if (!table)
        return false;
    BandArtwork artwork;
    const auto readPng = [table](const char *key, std::string &path) {
        if (!table->contains(key) || !ReadResource(*table, key, path))
            return false;
        std::string extension = std::filesystem::u8path(path).extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        return extension == ".png";
    };
    if (!readPng("background", artwork.background) || !readPng("foreground", artwork.foreground) ||
        !table->contains("width_dip") || !table->contains("height_dip") || !table->contains("offset_y_dip") ||
        !table->contains("foreground_right_clip_dip"))
        return false;
    artwork.widthDip = BoundedNumber(*table, "width_dip", 4096.0);
    artwork.heightDip = BoundedNumber(*table, "height_dip", 4096.0);
    artwork.foregroundRightClipDip = BoundedNumber(*table, "foreground_right_clip_dip", 4096.0);
    double offsetXDip = 0.0;
    if (artwork.widthDip <= 0.0 || artwork.heightDip <= 0.0 || artwork.foregroundRightClipDip < 0.0 ||
        artwork.foregroundRightClipDip > artwork.widthDip ||
        !ReadSignedNumber(*table, "offset_y_dip", 4096.0, artwork.offsetYDip) ||
        !ReadSignedNumber(*table, "offset_x_dip", 0.0, offsetXDip))
        return false;
    out = std::move(artwork);
    return true;
}

bool ReadLiveSD(const toml::node *node, std::optional<LiveSD> &out)
{
    if (!node)
    {
        out.reset();
        return true;
    }
    const auto *table = node->as_table();
    if (!table)
        return false;
    std::optional<bool> enabled;
    if (!ReadOptionalBool(*table, "enabled", enabled))
        return false;
    if (!enabled.value_or(true))
    {
        out.reset();
        return true;
    }
    LiveSD value;
    const auto readPng = [table](const char *key, std::string &path) {
        if (!table->contains(key) || !ReadResource(*table, key, path))
            return false;
        auto extension = std::filesystem::u8path(path).extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        return extension == ".png";
    };
    const auto readRange = [table](const char *key, double minimum, double maximum, double &number) {
        return ReadSignedNumber(*table, key, maximum, number) && number >= minimum && number <= maximum;
    };
    if (table->contains("click_image") &&
        (!ReadResource(*table, "click_image", value.clickImage) || !IsAnimatableImagePath(value.clickImage)))
        return false;
    if (!readRange("click_duration_ms", 300.0, 10000.0, value.clickDurationMs))
        return false;
    if (!readPng("idle_image", value.idleImage) || !readPng("idle2_image", value.idle2Image) ||
        !readPng("happy_image", value.happyImage) || !readPng("miss_image", value.missImage) ||
        !readRange("stage_width_dip", 200.0, 1000.0, value.stageWidthDip) ||
        !readRange("stage_height_dip", 120.0, 500.0, value.stageHeightDip) ||
        !readRange("width_dip", 40.0, 300.0, value.widthDip) ||
        !readRange("height_dip", 46.0, 345.0, value.heightDip) ||
        !readRange("foot_y_dip", 60.0, value.stageHeightDip - 4.0, value.footYDip) ||
        value.stageWidthDip < value.widthDip + 24.0 || value.footYDip < value.heightDip + 20.0)
        return false;
    out = std::move(value);
    return true;
}

// Resolve links with error-code overloads, so an otherwise safe relative path cannot escape the skin directory.
bool IsContainedRegularResource(const std::filesystem::path &directory, const std::string &resource)
{
    std::error_code ec;
    const auto root = std::filesystem::canonical(directory, ec);
    if (ec)
        return false;
    const auto file = std::filesystem::canonical(directory / std::filesystem::u8path(resource), ec);
    if (ec)
        return false;
    const auto relative = file.lexically_relative(root);
    if (relative.empty() || relative.is_absolute())
        return false;
    for (const auto &part : relative)
        if (part == ".." || part == ".")
            return false;
    return std::filesystem::is_regular_file(file, ec);
}

bool ReadTypedStars(const toml::node *node, std::optional<TypedStars> &out)
{
    if (!node)
        return true;
    const auto *table = node->as_table();
    if (!table)
        return false;
    if (const auto *enabled = table->get("enabled"))
    {
        const auto *flag = enabled->as_boolean();
        if (!flag)
            return false;
        if (!flag->get())
        {
            out.reset();
            return true;
        }
    }
    TypedStars value;
    const auto readPng = [table](const char *key, std::string &path) {
        if (!table->contains(key) || !ReadResource(*table, key, path))
            return false;
        auto extension = std::filesystem::u8path(path).extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        return extension == ".png";
    };
    if (!readPng("yellow_image", value.yellowImage) || !readPng("pink_image", value.pinkImage) ||
        (table->contains("blue_image") && !readPng("blue_image", value.blueImage)) ||
        !ReadSignedNumber(*table, "origin_x_dip", 2000.0, value.originXDip) ||
        !ReadSignedNumber(*table, "origin_y_dip", 2000.0, value.originYDip) ||
        !ReadSignedNumber(*table, "min_x_dip", 2000.0, value.minXDip) ||
        !ReadSignedNumber(*table, "max_x_dip", 2000.0, value.maxXDip) ||
        !ReadSignedNumber(*table, "min_y_dip", 2000.0, value.minYDip) ||
        !ReadSignedNumber(*table, "max_y_dip", 2000.0, value.maxYDip))
        return false;
    const std::pair<const char *, double TypedStars::*> parameters[] = {
        {"width_min_dip", &TypedStars::widthMinDip},
        {"width_max_dip", &TypedStars::widthMaxDip},
        {"rise_min_dip", &TypedStars::riseMinDip},
        {"rise_max_dip", &TypedStars::riseMaxDip},
        {"end_x_min_dip", &TypedStars::endXMinDip},
        {"end_x_max_dip", &TypedStars::endXMaxDip},
        {"end_y_min_dip", &TypedStars::endYMinDip},
        {"end_y_max_dip", &TypedStars::endYMaxDip},
        {"duration_min_ms", &TypedStars::durationMinMs},
        {"duration_max_ms", &TypedStars::durationMaxMs},
        {"fade_in_min_ms", &TypedStars::fadeInMinMs},
        {"fade_in_max_ms", &TypedStars::fadeInMaxMs},
        {"fade_out_min_ms", &TypedStars::fadeOutMinMs},
        {"fade_out_max_ms", &TypedStars::fadeOutMaxMs},
        {"rotation_min_deg", &TypedStars::rotationMinDeg},
        {"rotation_max_deg", &TypedStars::rotationMaxDeg},
        {"spin_min_deg", &TypedStars::spinMinDeg},
        {"spin_max_deg", &TypedStars::spinMaxDeg},
        {"pair_separation_dip", &TypedStars::pairSeparationDip}};
    for (const auto &parameter : parameters)
        if (!ReadSignedNumber(*table, parameter.first, 10000.0, value.*parameter.second))
            return false;
    const auto range = [](double low, double high, double minimum, double maximum) {
        return low >= minimum && high >= low && high <= maximum;
    };
    if (value.maxXDip - value.minXDip < 40.0 || value.maxYDip - value.minYDip < 80.0 ||
        !range(value.widthMinDip, value.widthMaxDip, 1.0, 128.0) ||
        !range(value.riseMinDip, value.riseMaxDip, 1.0, 1000.0) ||
        !range(value.endXMinDip, value.endXMaxDip, value.minXDip, value.maxXDip) ||
        !range(value.endYMinDip, value.endYMaxDip, value.originYDip, value.maxYDip) ||
        !range(value.durationMinMs, value.durationMaxMs, 200.0, 5000.0) ||
        !range(value.fadeInMinMs, value.fadeInMaxMs, 1.0, value.durationMinMs) ||
        !range(value.fadeOutMinMs, value.fadeOutMaxMs, 1.0, value.durationMinMs) ||
        value.fadeInMaxMs + value.fadeOutMaxMs >= value.durationMinMs ||
        !range(value.rotationMinDeg, value.rotationMaxDeg, -3600.0, 3600.0) ||
        !range(value.spinMinDeg, value.spinMaxDeg, -3600.0, 3600.0) || value.pairSeparationDip <= 0.0 ||
        value.pairSeparationDip > value.endXMaxDip - value.endXMinDip || value.originXDip <= value.minXDip ||
        value.originXDip >= value.maxXDip || value.originYDip - value.riseMaxDip <= value.minYDip)
        return false;
    out = std::move(value);
    return true;
}

// 外部皮肤与内置皮肤清单共用的外观键：[candidate_window] 里的圆角、线宽、阴影与版式（含 horizontal /
// vertical 子表），[candidate.*] 配色，[toolbar] 配色与圆角。只覆盖写了的键，所以内置清单可以叠在出厂外观上读。
bool ReadLook(const toml::table &root, Package &package, std::string *error)
{
    if (const auto *window = root["candidate_window"].as_table())
    {
        if (!ReadBandArtwork(window->get("band_artwork"), package.bandArtwork))
        {
            SetError(error, "candidate_window.band_artwork 必须给出相对 PNG 路径、0–4096 的正尺寸、"
                            "有限 offset_y_dip 与不大于 width_dip 的 foreground_right_clip_dip；offset_x_dip 只能为 0");
            return false;
        }
        if (!ReadItemSeparators(window->get("item_separators"), package.itemSeparators))
        {
            SetError(error, "candidate_window.item_separators 必须给出颜色与 0–4096 的有限 DIP 数值；"
                            "bottom_dip 必须大于 top_dip，line_width_dip 必须大于 0，line_gap_dip 不得小于线宽");
            return false;
        }
        if (window->contains("corner_radius_dip"))
        {
            const double radius = BoundedNumber(*window, "corner_radius_dip", 32.0);
            if (radius < 0.0)
            {
                SetError(error, "candidate_window.corner_radius_dip 超出范围");
                return false;
            }
            package.cornerRadiusDip = radius;
        }
        if (!ReadOptionalBounded(*window, "border_width_dip", 4.0, package.borderWidthDip))
        {
            SetError(error, "candidate_window.border_width_dip 超出范围");
            return false;
        }
        if (!ReadOptionalBounded(*window, "item_corner_radius_dip", 16.0, package.itemCornerRadiusDip))
        {
            SetError(error, "candidate_window.item_corner_radius_dip 超出范围");
            return false;
        }
        if (!ReadEnum(*window, "shadow", {"none", "soft", "strong"}, package.shadow))
        {
            SetError(error, "candidate_window.shadow 只能是 none、soft 或 strong");
            return false;
        }
        if (const toml::node *rowsNode = window->get("expand_rows"))
        {
            const auto *rows = rowsNode->as_integer();
            if (!rows || rows->get() < kMinExpandRows || rows->get() > kMaxExpandRows)
            {
                SetError(error, "candidate_window.expand_rows 必须是 2–8 的整数");
                return false;
            }
            package.expandRows = static_cast<int>(rows->get());
        }
        const toml::node *horizontal = window->get("horizontal");
        const toml::node *vertical = window->get("vertical");
        if (!ReadGeometry(*window, package.geometry) || (horizontal && !horizontal->as_table()) ||
            (vertical && !vertical->as_table()) ||
            (horizontal && !ReadGeometry(*horizontal->as_table(), package.horizontalGeometry)) ||
            (vertical && !ReadGeometry(*vertical->as_table(), package.verticalGeometry)))
        {
            SetError(error, "candidate_window 的版式数值无效");
            return false;
        }
    }
    const auto *candidate = root["candidate"].as_table();
    if (candidate && (!ReadColors((*candidate)["dark"].as_table(), package.dark) ||
                      !ReadColors((*candidate)["light"].as_table(), package.light)))
    {
        SetError(error, "candidate 配色无效");
        return false;
    }
    if (const toml::node *toolbarNode = root.get("toolbar"))
    {
        const auto *toolbar = toolbarNode->as_table();
        if (!toolbar || !ReadToolbarColors(toolbar->get("dark"), package.toolbarDark) ||
            !ReadToolbarColors(toolbar->get("light"), package.toolbarLight))
        {
            SetError(error, "toolbar 配色无效");
            return false;
        }
        if (toolbar->contains("corner_radius_dip"))
        {
            const double radius = BoundedNumber(*toolbar, "corner_radius_dip", 32.0);
            if (radius < 0.0)
            {
                SetError(error, "toolbar.corner_radius_dip 超出范围");
                return false;
            }
            package.toolbarCornerRadiusDip = radius;
        }
        if (!ReadOptionalBounded(*toolbar, "icon_corner_radius_dip", 16.0, package.toolbarIconCornerRadiusDip))
        {
            SetError(error, "toolbar.icon_corner_radius_dip 超出范围");
            return false;
        }
    }
    return true;
}

// 内置清单的开关：page_arrows、fit_content、expand_panel。只覆盖写了的键。
bool ReadSwitches(const toml::table &root, DefaultSkin &skin, std::string *error)
{
    const toml::node *windowNode = root.get("candidate_window");
    if (!windowNode)
    {
        return true;
    }
    const auto *window = windowNode->as_table();
    if (!window || !ReadOptionalBool(*window, "page_arrows", skin.pageArrows))
    {
        SetError(error, "candidate_window.page_arrows 必须是 true 或 false");
        return false;
    }
    if (!ReadOptionalBool(*window, "fit_content", skin.fitContent))
    {
        SetError(error, "candidate_window.fit_content 必须是 true 或 false");
        return false;
    }
    if (!ReadOptionalBool(*window, "expand_panel", skin.expandPanel))
    {
        SetError(error, "candidate_window.expand_panel 必须是 true 或 false");
        return false;
    }
    return true;
}

// 出厂清单：构建时从仓库的 skins/default/<id>/skin.toml 生成（见 server/CMakeLists.txt），内容与安装包里的一致。
struct FactoryManifest
{
    const char *id;
    const char *text;
};

constexpr FactoryManifest kFactoryManifests[] = {
#include "builtin_skin_manifests.inc"
};

struct FactoryEntry
{
    Package look;
    DefaultSkin switches;
};

const std::vector<std::pair<std::string, FactoryEntry>> &FactoryEntries()
{
    static const std::vector<std::pair<std::string, FactoryEntry>> entries = [] {
        std::vector<std::pair<std::string, FactoryEntry>> parsed;
        for (const FactoryManifest &manifest : kFactoryManifests)
        {
            FactoryEntry entry;
            entry.switches.id = manifest.id;
            try
            {
                const toml::table root = toml::parse(std::string_view(manifest.text));
                (void)ReadString(root, "name", entry.switches.name, 80, false);
                (void)ReadString(root, "description", entry.switches.description, 500, false);
                // 出厂清单由测试保证有效；万一读不出来，那一项保持渲染器的缺省值，不影响其他皮肤。
                Package look;
                if (ReadLook(root, look, nullptr))
                    entry.look = std::move(look);
                DefaultSkin switches = entry.switches;
                if (ReadSwitches(root, switches, nullptr))
                    entry.switches = std::move(switches);
            }
            catch (const std::exception &)
            {
            }
            entry.look.id = manifest.id;
            entry.look.base = manifest.id;
            parsed.emplace_back(manifest.id, std::move(entry));
        }
        return parsed;
    }();
    return entries;
}

const FactoryEntry *FindFactoryEntry(const std::string &id)
{
    for (const auto &[entryId, entry] : FactoryEntries())
    {
        if (entryId == id)
            return &entry;
    }
    return nullptr;
}

const FactoryEntry &FactoryEntryOrFluent(const std::string &id)
{
    if (const FactoryEntry *entry = FindFactoryEntry(id))
        return *entry;
    if (const FactoryEntry *fluent = FindFactoryEntry("fluent"))
        return *fluent;
    static const FactoryEntry empty;
    return empty;
}
} // namespace

float ResolveLength(const Length &length, float fontSize)
{
    return length.em ? std::round(fontSize * static_cast<float>(length.value)) : static_cast<float>(length.value);
}

bool HasFactoryLook(const std::string &id)
{
    return FindFactoryEntry(id) != nullptr;
}

const Package &FactoryLook(const std::string &id)
{
    return FactoryEntryOrFluent(id).look;
}

Package ResolveBuiltInLook(const std::filesystem::path &skinsRoot, const std::string &id)
{
    const std::string key = HasFactoryLook(id) ? id : std::string("fluent");
    Package look = FactoryLook(key);
    const std::filesystem::path manifest =
        skinsRoot / std::filesystem::u8path(kDefaultSkinsFolder) / std::filesystem::u8path(key) / L"skin.toml";
    try
    {
        const std::optional<toml::table> parsed = ParseManifest(manifest);
        if (!parsed)
        {
            return look;
        }
        // 叠在副本上读，数据目录里的清单哪一项写错了就整份不用，回到出厂外观。
        Package overlaid = look;
        if (ReadLook(*parsed, overlaid, nullptr))
        {
            return overlaid;
        }
    }
    catch (const std::exception &)
    {
    }
    return look;
}

DefaultSkin ResolveBuiltInInfo(const std::filesystem::path &skinsRoot, const std::string &id)
{
    DefaultSkin info = FactoryEntryOrFluent(id).switches;
    if (const std::optional<DefaultSkin> data = LoadDefault(skinsRoot, id))
    {
        if (!data->name.empty())
            info.name = data->name;
        if (!data->description.empty())
            info.description = data->description;
    }
    return info;
}

const std::vector<std::string> &BuiltInIds()
{
    static const std::vector<std::string> ids = {"fluent",       "wechat",           "wechat_scroll", "graphite",
                                                 "willow_green", "autumn_osmanthus", "microsoft"};
    return ids;
}

const std::vector<std::string> &OfficialPackageIds()
{
    static const std::vector<std::string> ids = {"green_leaf"};
    return ids;
}

bool IsOfficialPackage(const std::string &id)
{
    const auto &ids = OfficialPackageIds();
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

const std::string &BorderImageSource(const BorderImage &borderImage, bool light)
{
    return light && !borderImage.lightSource.empty() ? borderImage.lightSource : borderImage.source;
}

bool IsCanvasBase(const std::string &base)
{
    return base == kCanvasBase;
}

bool IsAnimatableImagePath(const std::string &path)
{
    const auto dot = path.find_last_of('.');
    if (dot == std::string::npos || path.find('/', dot) != std::string::npos)
    {
        return false;
    }
    std::string extension = path.substr(dot);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return extension == ".png" || extension == ".apng" || extension == ".gif" || extension == ".webp";
}

bool IsBuiltIn(const std::string &id)
{
    const auto &ids = BuiltInIds();
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

bool IsSafeId(const std::string &id)
{
    if (id.empty() || id.size() > 64 || !std::isalnum(static_cast<unsigned char>(id.front())))
    {
        return false;
    }
    return std::all_of(id.begin(), id.end(), [](unsigned char ch) {
        return std::islower(ch) || std::isdigit(ch) || ch == '.' || ch == '_' || ch == '-';
    });
}

bool IsBackupFolder(const std::string &folder)
{
    constexpr std::string_view kSuffix = ".bak";
    if (folder.size() < kSuffix.size())
    {
        return false;
    }
    return std::equal(kSuffix.begin(), kSuffix.end(), folder.end() - kSuffix.size(),
                      [](char a, char b) { return a == std::tolower(static_cast<unsigned char>(b)); });
}

bool Supports(const Package &package, const std::string &layout, const std::string &theme)
{
    return std::find(package.layouts.begin(), package.layouts.end(), layout) != package.layouts.end() &&
           std::find(package.themes.begin(), package.themes.end(), theme) != package.themes.end();
}

std::optional<Package> LoadSupported(const std::filesystem::path &skinsRoot, const std::string &id,
                                     const std::string &layout, const std::string &theme)
{
    std::optional<Package> package = Load(skinsRoot, id);
    if (package && !Supports(*package, layout, theme))
    {
        return std::nullopt;
    }
    return package;
}

std::optional<Package> Load(const std::filesystem::path &skinsRoot, const std::string &id, std::string *error)
{
    if (!IsSafeId(id) || IsBuiltIn(id) || id == kDefaultSkinsFolder)
    {
        SetError(error, "目录名不是有效的外部皮肤 ID");
        return std::nullopt;
    }
    const std::filesystem::path directory = skinsRoot / std::filesystem::u8path(id);
    const std::filesystem::path manifest = directory / L"skin.toml";
    try
    {
        const std::optional<toml::table> parsed = ParseManifest(manifest);
        if (!parsed)
        {
            SetError(error, "缺少或无法解析 skin.toml");
            return std::nullopt;
        }
        const toml::table &root = *parsed;
        const int schemaVersion = static_cast<int>(root["schema_version"].value_or(0));
        if (schemaVersion != 1 && schemaVersion != 2)
        {
            SetError(error, "仅支持 schema_version 1 或 2");
            return std::nullopt;
        }
        Package package;
        package.schemaVersion = schemaVersion;
        if (!ReadString(root, "id", package.id, 64, true) || package.id != id ||
            !ReadString(root, "name", package.name, 80, true) ||
            !ReadString(root, "version", package.version, 32, true) ||
            !ReadString(root, "author", package.author, 120, false) ||
            !ReadString(root, "description", package.description, 500, false) ||
            !ReadString(root, "base", package.base, 32, true) ||
            (!IsBuiltIn(package.base) && !FindSharedBase(package.base) && !IsCanvasBase(package.base)))
        {
            SetError(error, "manifest 的基本信息无效");
            return std::nullopt;
        }
        // canvas 没有自己的面板，只有 v2 的九宫格与挂件能让它可见；v1 皮肤用它只会得到一张透明卡片。
        if (IsCanvasBase(package.base) && schemaVersion < 2)
        {
            SetError(error, "base = \"canvas\" 只能用于 schema_version = 2 的皮肤");
            return std::nullopt;
        }
        // v2 字段只在 canvas 上生效：其他 base 的卡片几何与九宫格/挂件互相打架。这种皮肤仍可使用，按 v1 处理。
        if (schemaVersion >= 2 && !IsCanvasBase(package.base))
        {
            package.schemaVersion = 1;
            package.notices.push_back("schema_version = 2 需要 base = \"canvas\"，已按 schema_version = 1 "
                                      "加载，九宫格、挂件等 v2 字段被忽略");
        }
        const auto *supports = root["supports"].as_table();
        if (!supports || !ReadEnumArray(*supports, "layouts", {"horizontal", "vertical"}, package.layouts) ||
            !ReadEnumArray(*supports, "themes", {"dark", "light"}, package.themes))
        {
            SetError(error, "supports.layouts 或 supports.themes 无效");
            return std::nullopt;
        }
        const auto *window = root["candidate_window"].as_table();
        if (!window)
        {
            SetError(error, "缺少 candidate_window");
            return std::nullopt;
        }
        package.minWidthDip = BoundedNumber(*window, "min_width_dip", 1000.0);
        if (package.minWidthDip < 0.0)
        {
            SetError(error, "candidate_window.min_width_dip 超出范围");
            return std::nullopt;
        }
        if (!ReadOptionalBounded(*window, "max_width_dip", 4096.0, package.maxWidthDip) ||
            (package.maxWidthDip && *package.maxWidthDip < 1.0))
        {
            SetError(error, "candidate_window.max_width_dip 必须是 1–4096 的有限数值");
            return std::nullopt;
        }
        if (package.maxWidthDip && package.minWidthDip > *package.maxWidthDip)
        {
            SetError(error, "candidate_window.min_width_dip 不得大于 max_width_dip");
            return std::nullopt;
        }
        std::optional<bool> reserveMaxWidth;
        if (!ReadOptionalBool(*window, "reserve_max_width", reserveMaxWidth))
        {
            SetError(error, "candidate_window.reserve_max_width 必须是 true 或 false");
            return std::nullopt;
        }
        package.reserveMaxWidth = reserveMaxWidth.value_or(false);
        if (package.reserveMaxWidth && !package.maxWidthDip)
        {
            SetError(error, "candidate_window.reserve_max_width = true 时必须设置 max_width_dip");
            return std::nullopt;
        }
        // 装饰图是可选的：没有这张表就没有装饰；有这张表时图片和两个尺寸都必须给出。
        if (const toml::node *decorationNode = window->get("decoration"))
        {
            const auto *decoration = decorationNode->as_table();
            if (!decoration || !decoration->contains("image") ||
                !ReadResource(*decoration, "image", package.decorationImage) ||
                !ReadEnum(*decoration, "align", {"left", "center", "right"}, package.decorationAlign))
            {
                SetError(error, "candidate_window.decoration 无效");
                return std::nullopt;
            }
            package.decorationTopDip = BoundedNumber(*decoration, "top_inset_dip", 500.0);
            package.decorationWidthDip = BoundedNumber(*decoration, "width_dip", 1000.0);
            if (package.decorationTopDip <= 0.0 || package.decorationWidthDip <= 0.0)
            {
                SetError(error, "candidate_window.decoration 尺寸无效");
                return std::nullopt;
            }
        }
        if (!ReadEnum(*window, "backdrop", {"none", "acrylic", "mica"}, package.backdrop))
        {
            SetError(error, "candidate_window.backdrop 只能是 none、acrylic 或 mica");
            return std::nullopt;
        }
        if (!ReadFontFamily(*window, "font_family", package.fontFamily))
        {
            SetError(error, "candidate_window.font_family 无效");
            return std::nullopt;
        }
        if (!ReadOptionalBool(*window, "page_arrows", package.pageArrows))
        {
            SetError(error, "candidate_window.page_arrows 必须是 true 或 false");
            return std::nullopt;
        }
        if (!ReadOptionalBool(*window, "fit_content", package.fitContent))
        {
            SetError(error, "candidate_window.fit_content 必须是 true 或 false");
            return std::nullopt;
        }
        if (!ReadOptionalBool(*window, "expand_panel", package.expandPanel))
        {
            SetError(error, "candidate_window.expand_panel 必须是 true 或 false");
            return std::nullopt;
        }
        if (const toml::node *backgroundNode = window->get("background"))
        {
            const auto *background = backgroundNode->as_table();
            if (!background || !background->contains("image") ||
                !ReadResource(*background, "image", package.backgroundImage) ||
                !ReadEnum(*background, "fit", {"cover", "contain", "stretch"}, package.backgroundFit))
            {
                SetError(error, "candidate_window.background 无效");
                return std::nullopt;
            }
            if (background->contains("opacity"))
            {
                package.backgroundOpacity = BoundedNumber(*background, "opacity", 1.0);
                if (package.backgroundOpacity < 0.0)
                {
                    SetError(error, "candidate_window.background.opacity 超出范围");
                    return std::nullopt;
                }
            }
        }
        if (package.schemaVersion >= 2)
        {
            if (!ReadLiveSD(window->get("livesd"), package.liveSD))
            {
                SetError(error, "candidate_window.livesd requires four relative PNG images, finite stage/sprite "
                                "sizes, and sufficient foot-anchor margins for jumping and swaying");
                return std::nullopt;
            }
            if (!ReadTypedStars(window->get("typed_stars"), package.typedStars))
            {
                SetError(error, "candidate_window.typed_stars requires relative PNG images, finite ordered "
                                "size/trajectory/time ranges, and bounds containing the emission origin");
                return std::nullopt;
            }
            if (const toml::node *borderImageNode = window->get("border_image"))
            {
                const auto *borderImageTable = borderImageNode->as_table();
                BorderImage bi;
                if (!borderImageTable || !borderImageTable->contains("source") ||
                    !ReadResource(*borderImageTable, "source", bi.source) || !IsAnimatableImagePath(bi.source))
                {
                    SetError(error, "candidate_window.border_image.source 无效（需为 .png、.apng、.gif 或 .webp）");
                    return std::nullopt;
                }
                if (borderImageTable->contains("light_source") &&
                    (!ReadResource(*borderImageTable, "light_source", bi.lightSource) ||
                     !IsAnimatableImagePath(bi.lightSource)))
                {
                    SetError(error,
                             "candidate_window.border_image.light_source 无效（需为 .png、.apng、.gif 或 .webp）");
                    return std::nullopt;
                }
                if (!borderImageTable->contains("slice_dip") ||
                    !ReadBoxValues(*borderImageTable, "slice_dip", bi.sliceDip))
                {
                    SetError(error, "candidate_window.border_image.slice_dip 必须是 1、2 或 4 个 0–500 的数值");
                    return std::nullopt;
                }
                if (!ReadBoxValues(*borderImageTable, "stretch_dip", bi.stretchDip))
                {
                    SetError(error, "candidate_window.border_image.stretch_dip 必须是 1、2 或 4 个 0–500 的数值");
                    return std::nullopt;
                }
                if (!ReadBoxValues(*borderImageTable, "width_dip", bi.widthDip))
                {
                    SetError(error, "candidate_window.border_image.width_dip 必须是 1、2 或 4 个 0–500 的数值");
                    return std::nullopt;
                }
                std::optional<bool> fill;
                if (!ReadOptionalBool(*borderImageTable, "fill", fill))
                {
                    SetError(error, "candidate_window.border_image.fill 必须是 true 或 false");
                    return std::nullopt;
                }
                bi.fill = fill.value_or(true);
                package.borderImage = bi;
            }

            if (const toml::node *insetsNode = window->get("content_insets_dip"))
            {
                const auto *insetsTable = insetsNode->as_table();
                if (!insetsTable)
                {
                    SetError(error, "candidate_window.content_insets_dip 必须是表");
                    return std::nullopt;
                }
                InsetsDip insets;
                insets.top = BoundedNumber(*insetsTable, "top", 200.0);
                insets.right = BoundedNumber(*insetsTable, "right", 500.0);
                insets.bottom = BoundedNumber(*insetsTable, "bottom", 200.0);
                insets.left = BoundedNumber(*insetsTable, "left", 500.0);
                if (insets.top < 0.0 || insets.right < 0.0 || insets.bottom < 0.0 || insets.left < 0.0)
                {
                    SetError(error, "candidate_window.content_insets_dip 数值超出范围");
                    return std::nullopt;
                }
                package.contentInsetsDip = insets;
            }

            if (!ReadOptionalBounded(*window, "line_gap_dip", 100.0, package.lineGapDip))
            {
                SetError(error, "candidate_window.line_gap_dip 超出范围");
                return std::nullopt;
            }

            if (const toml::node *outerNode = window->get("outer_insets_dip"))
            {
                const auto *outerTable = outerNode->as_table();
                if (!outerTable)
                {
                    SetError(error, "candidate_window.outer_insets_dip 必须是表");
                    return std::nullopt;
                }
                InsetsDip outer;
                outer.top = BoundedNumber(*outerTable, "top", 500.0);
                outer.right = BoundedNumber(*outerTable, "right", 500.0);
                outer.bottom = BoundedNumber(*outerTable, "bottom", 500.0);
                outer.left = BoundedNumber(*outerTable, "left", 500.0);
                if (outer.top < 0.0 || outer.right < 0.0 || outer.bottom < 0.0 || outer.left < 0.0)
                {
                    SetError(error, "candidate_window.outer_insets_dip 数值超出范围");
                    return std::nullopt;
                }
                package.outerInsetsDip = outer;
            }

            if (const toml::node *decorationsNode = window->get("decorations"))
            {
                const auto *decorationsArr = decorationsNode->as_array();
                if (!decorationsArr)
                {
                    SetError(error, "candidate_window.decorations 必须是数组");
                    return std::nullopt;
                }
                if (decorationsArr->size() > kMaxFreeDecorations)
                {
                    SetError(error, "candidate_window.decorations 最多 32 层");
                    return std::nullopt;
                }
                for (const auto &decNode : *decorationsArr)
                {
                    const auto *decTable = decNode.as_table();
                    FreeDecoration dec;
                    if (!decTable || !decTable->contains("image") || !ReadResource(*decTable, "image", dec.image) ||
                        !IsAnimatableImagePath(dec.image))
                    {
                        SetError(error, "candidate_window.decorations.image 无效（需为 .png、.apng、.gif 或 .webp）");
                        return std::nullopt;
                    }
                    if (decTable->contains("anchor") &&
                        !ReadEnum(*decTable, "anchor",
                                  {"top-left", "top-right", "bottom-left", "bottom-right", "left", "right", "top",
                                   "bottom", "center"},
                                  dec.anchor))
                    {
                        SetError(error, "candidate_window.decorations.anchor 无效");
                        return std::nullopt;
                    }
                    // offset_x_dip、offset_y_dip、z_index 可选；写了就必须是范围内的数（z_index 为整数）。
                    if (!ReadSignedNumber(*decTable, "offset_x_dip", kDecorationOffsetLimitDip, dec.offsetXDip) ||
                        !ReadSignedNumber(*decTable, "offset_y_dip", kDecorationOffsetLimitDip, dec.offsetYDip))
                    {
                        SetError(error, "candidate_window.decorations 的 offset_x_dip / offset_y_dip 必须在 "
                                        "-2000–2000 之间");
                        return std::nullopt;
                    }
                    if (const toml::node *zNode = decTable->get("z_index"))
                    {
                        const auto *zValue = zNode->as_integer();
                        if (!zValue || zValue->get() < -kDecorationZIndexLimit ||
                            zValue->get() > kDecorationZIndexLimit)
                        {
                            SetError(error, "candidate_window.decorations.z_index 必须是 -1000–1000 的整数");
                            return std::nullopt;
                        }
                        dec.zIndex = static_cast<int>(zValue->get());
                    }
                    dec.widthDip = BoundedNumber(*decTable, "width_dip", 2000.0);
                    dec.heightDip = BoundedNumber(*decTable, "height_dip", 2000.0);
                    if (dec.widthDip <= 0.0 || dec.heightDip <= 0.0)
                    {
                        SetError(error, "candidate_window.decorations 的 width_dip 与 height_dip 必须大于 0");
                        return std::nullopt;
                    }
                    package.freeDecorations.push_back(dec);
                }
            }
        }

        if (!ReadLook(root, package, error))
        {
            return std::nullopt;
        }
        std::error_code ec;
        if (package.bandArtwork)
        {
            for (const auto &path : {package.bandArtwork->background, package.bandArtwork->foreground})
            {
                if (!std::filesystem::is_regular_file(directory / std::filesystem::u8path(path), ec))
                {
                    SetError(error, "找不到 candidate_window.band_artwork 的 PNG 文件");
                    return std::nullopt;
                }
            }
        }
        if (!package.decorationImage.empty() &&
            !std::filesystem::is_regular_file(directory / std::filesystem::u8path(package.decorationImage), ec))
        {
            SetError(error, "找不到 candidate_window.decoration.image 文件");
            return std::nullopt;
        }
        if (!package.backgroundImage.empty() &&
            !std::filesystem::is_regular_file(directory / std::filesystem::u8path(package.backgroundImage), ec))
        {
            SetError(error, "找不到 candidate_window.background.image 文件");
            return std::nullopt;
        }
        if (package.borderImage && !package.borderImage->source.empty() &&
            !std::filesystem::is_regular_file(directory / std::filesystem::u8path(package.borderImage->source), ec))
        {
            SetError(error, "找不到 candidate_window.border_image.source 文件");
            return std::nullopt;
        }
        if (package.borderImage && !package.borderImage->lightSource.empty() &&
            !std::filesystem::is_regular_file(directory / std::filesystem::u8path(package.borderImage->lightSource),
                                              ec))
        {
            SetError(error, "找不到 candidate_window.border_image.light_source 文件");
            return std::nullopt;
        }
        for (const auto &dec : package.freeDecorations)
        {
            if (!dec.image.empty() &&
                !std::filesystem::is_regular_file(directory / std::filesystem::u8path(dec.image), ec))
            {
                SetError(error, "找不到 candidate_window.decorations.image 文件");
                return std::nullopt;
            }
        }
        if (package.typedStars)
        {
            for (const auto &path :
                 {package.typedStars->yellowImage, package.typedStars->pinkImage, package.typedStars->blueImage})
            {
                if (path.empty())
                    continue;
                if (!std::filesystem::is_regular_file(directory / std::filesystem::u8path(path), ec))
                {
                    SetError(error, "candidate_window.typed_stars image file was not found");
                    return std::nullopt;
                }
            }
        }
        if (package.liveSD)
        {
            for (const auto &path : {package.liveSD->idleImage, package.liveSD->idle2Image, package.liveSD->happyImage,
                                     package.liveSD->missImage, package.liveSD->clickImage})
            {
                if (path.empty())
                    continue;
                if (!IsContainedRegularResource(directory, path))
                {
                    SetError(error, "candidate_window.livesd image must exist inside the skin directory");
                    return std::nullopt;
                }
            }
        }
        AdaptSharedBase(package);
        // 工具栏拖动条没单独写时跟候选的 accent 走，与选中竖条、预编辑光标同色；accent 也没写才沿用 base。
        FillColor(package.toolbarDark.handle, package.dark.accent);
        FillColor(package.toolbarLight.handle, package.light.accent);
        return package;
    }
    catch (const toml::parse_error &)
    {
        SetError(error, "缺少或无法解析 skin.toml");
        return std::nullopt;
    }
    catch (const std::exception &)
    {
        SetError(error, "skin.toml 不是有效的 TOML manifest");
        return std::nullopt;
    }
}

ScanResult Scan(const std::filesystem::path &skinsRoot)
{
    ScanResult result;
    std::error_code ec;
    if (!std::filesystem::exists(skinsRoot, ec))
    {
        return result;
    }
    for (std::filesystem::directory_iterator it(skinsRoot, ec), end; !ec && it != end; it.increment(ec))
    {
        if (!it->is_directory(ec))
        {
            continue;
        }
        const std::string folder = it->path().filename().u8string();
        // <id>.bak 是安装包覆盖随包皮肤前留下的旧版本：manifest 的 id 与目录名对不上，
        // 不跳过就会在设置页被列成一条损坏的皮肤。
        if (folder == kDefaultSkinsFolder || IsBackupFolder(folder))
        {
            continue;
        }
        std::string error;
        auto package = Load(skinsRoot, folder, &error);
        if (package)
        {
            // 能用但有问题的皮肤照样列出来，问题另记一条，免得作者以为 v2 字段生效了。
            for (const auto &notice : package->notices)
            {
                result.issues.push_back({folder, notice});
            }
            result.packages.push_back(std::move(*package));
        }
        else
        {
            result.issues.push_back({folder, error});
        }
    }
    if (ec)
    {
        result.issues.push_back({"skins", "无法完整读取皮肤目录"});
    }
    std::sort(result.packages.begin(), result.packages.end(),
              [](const Package &a, const Package &b) { return a.name < b.name; });
    std::sort(result.issues.begin(), result.issues.end(),
              [](const Issue &a, const Issue &b) { return a.folder < b.folder; });
    return result;
}

std::optional<DefaultSkin> LoadDefault(const std::filesystem::path &skinsRoot, const std::string &id,
                                       std::string *error)
{
    if (!IsBuiltIn(id))
    {
        SetError(error, "不是内置皮肤 ID");
        return std::nullopt;
    }
    const std::filesystem::path manifest =
        skinsRoot / std::filesystem::u8path(kDefaultSkinsFolder) / std::filesystem::u8path(id) / L"skin.toml";
    try
    {
        const std::optional<toml::table> parsed = ParseManifest(manifest);
        if (!parsed)
        {
            SetError(error, "缺少或无法解析 skin.toml");
            return std::nullopt;
        }
        const toml::table &root = *parsed;
        DefaultSkin skin;
        if (root["schema_version"].value_or(0) != 1 || !ReadString(root, "id", skin.id, 64, true) || skin.id != id ||
            !ReadString(root, "name", skin.name, 80, false) ||
            !ReadString(root, "description", skin.description, 500, false))
        {
            SetError(error, "manifest 的基本信息无效");
            return std::nullopt;
        }
        if (!ReadSwitches(root, skin, error))
        {
            return std::nullopt;
        }
        return skin;
    }
    catch (const std::exception &)
    {
        SetError(error, "skin.toml 不是有效的 TOML manifest");
        return std::nullopt;
    }
}

namespace
{
// 开关的取值顺序：数据目录里的内置清单 → 出厂清单 → 程序缺省值。
bool SwitchValue(const std::optional<DefaultSkin> &defaults, std::optional<bool> DefaultSkin::*field,
                 const std::string &base, bool fallback)
{
    if (defaults && (*defaults).*field)
        return *((*defaults).*field);
    const FactoryEntry &factory = FactoryEntryOrFluent(IsBuiltIn(base) ? base : "fluent");
    return (factory.switches.*field).value_or(fallback);
}
} // namespace

bool ResolvePageArrows(const std::filesystem::path &skinsRoot, const std::string &skinId, const Package *package)
{
    if (package && package->pageArrows)
    {
        return *package->pageArrows;
    }
    const std::string base = package ? package->base : skinId;
    // 配置里的皮肤既不是内置、也没能作为外部皮肤加载时，渲染端回退到 fluent，这里跟着回退。
    const auto defaults = LoadDefault(skinsRoot, IsBuiltIn(base) ? base : "fluent");
    return SwitchValue(defaults, &DefaultSkin::pageArrows, base, kDefaultPageArrows);
}

bool ResolveFitContent(const std::filesystem::path &skinsRoot, const std::string &skinId, const Package *package)
{
    if (package && package->fitContent)
    {
        return *package->fitContent;
    }
    const std::string base = package ? package->base : skinId;
    const auto defaults = LoadDefault(skinsRoot, IsBuiltIn(base) ? base : "fluent");
    return SwitchValue(defaults, &DefaultSkin::fitContent, base, kDefaultFitContent);
}

bool ResolveExpandPanel(const std::filesystem::path &skinsRoot, const std::string &skinId, const Package *package)
{
    if (package && package->expandPanel)
    {
        return *package->expandPanel;
    }
    const std::string base = package ? package->base : skinId;
    const auto defaults = LoadDefault(skinsRoot, IsBuiltIn(base) ? base : "fluent");
    return SwitchValue(defaults, &DefaultSkin::expandPanel, base, kDefaultExpandPanel);
}
} // namespace CandidateSkinCatalog
