#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace CandidateSkinCatalog
{
struct CandidateColors
{
    std::string accent;
    std::string selected;
    std::string hover;
    std::string surface;
    std::string border;
    std::string text;
    std::string number;
    // 候选后的翻译文本。留空时沿用候选文字色并降到 62% 不透明度；设置后按原值绘制，选中行也不改。
    std::string translation;
    // 翻译里的词性（n. / adj. …）与义项分隔符（; / ；）。留空时与同一行的翻译同色。
    // 包写了 translation 而没写这两项时，基础皮肤自带的词性/分隔符配色也一并作废，跟随新的翻译色。
    std::string translationPos;
    std::string translationSeparator;
    std::optional<bool> showSelectedBar;
    // 以下各项留空时的回退：candidate_text / preedit_text 用 text；preedit_caret / selected_bar 用 accent；
    // selected_text / selected_number 用基础皮肤的选中行配色；selected_translation 用 translation，
    // 再不行就是选中行文字色 × 0.62；preedit_background 透明；preedit_divider 不画。
    std::string candidateText;
    std::string preeditText;
    std::string preeditCaret;
    std::string selectedText;
    std::string selectedNumber;
    std::string selectedTranslation;
    // 选中行的词性与分隔符。留空时用 translation_pos / translation_separator；包写了 selected_translation
    // 而没写这两项时跟随 selected_translation。
    std::string selectedTranslationPos;
    std::string selectedTranslationSeparator;
    std::string selectedBar;
    std::string preeditBackground;
    std::string preeditDivider;
    // 候选框右键菜单；留空的项沿用基础皮肤。
    std::string menuBackground;
    std::string menuBorder;
    std::string menuText;
    std::string menuHover;
    // 横排三角翻页箭头前面那条分隔线（page_arrow_style = "triangle" 时才画）；留空不画。
    std::string pageArrowDivider;
    // candidate_window.backdrop 不是 none 时的材质参数，0–1。backdrop_opacity 是 surface 色调盖在材质上的不透明度，
    // 不写时取 surface 自带的 alpha；backdrop_luminosity 是亮度层的不透明度，越高越不受后面画面明暗影响，
    // 不写时亚克力取 0.85、云母取 1。
    std::optional<double> backdropOpacity;
    std::optional<double> backdropLuminosity;
};

// 悬浮工具栏的配色覆盖。留空的项沿用基础皮肤。
struct ToolbarColors
{
    std::string background;
    std::string border;
    std::string handle;
    std::string divider;
    std::string icon;
    std::string hover;
};

struct BorderImage
{
    std::string source;
    // 浅色主题换用的面板图（light_source），切片、边宽和 fill 与 source 共用，两张图要按同一套几何画；
    // 为空时深浅主题都用 source。
    std::string lightSource;
    // 切片线位置，以源图像素计（同 CSS border-image-slice 的数值，键名沿用 _dip）：1/2/4 个值，按 CSS
    // 简写展开成上右下左。
    std::vector<double> sliceDip;
    // 可拉伸区（上右下左，同为源图像素，从图片外缘量起）：给出时覆盖 sliceDip。
    // 面板图里画死的装饰必须落在可拉伸区之外，否则会被横向拉长。
    std::vector<double> stretchDip;
    // 四边在屏幕上的绘制宽度（DIP，同 CSS border-image-width）；为空时取切片的数值当 DIP。
    // 给 2 倍图时把它写成切片的一半，面板在 200% 缩放下仍按原图像素清晰显示。
    std::vector<double> widthDip;
    // 是否画九宫格中心块（CSS border-image-slice 的 fill）
    bool fill = true;
};

struct InsetsDip
{
    double top = 0.0;
    double right = 0.0;
    double bottom = 0.0;
    double left = 0.0;
};

// 候选框里的一段长度：清单里写 DIP 数值，或写成 "0.3em" 按候选字号缩放（四舍五入到整 DIP）。
struct Length
{
    double value = 0.0;
    bool em = false;
};
float ResolveLength(const Length &length, float fontSize);

// 候选框的版式。[candidate_window] 里写的对两种排列都生效，[candidate_window.horizontal] /
// [candidate_window.vertical] 里写的只对那一种生效、并且优先。没写的项沿用 base，再没有就用渲染器的缺省值。
struct CandidateGeometry
{
    // 卡片内边距
    std::optional<Length> padding;
    // 候选项之间的空隙
    std::optional<Length> itemGap;
    // 候选项比 1.35 倍字号多出的高度，上下各一半
    std::optional<Length> itemExtraHeight;
    // 高亮左缘到序号左端的距离（不小于 5）
    std::optional<Length> numberInset;
    // 候选文字后面的留白
    std::optional<Length> itemPaddingRight;
    // 横排每项的最小宽度，0 为不限
    std::optional<Length> itemMinWidth;
    // 序号与候选之间的空
    std::optional<Length> numberGap;
    // 序号字号相对候选字号的比例
    std::optional<double> numberScale;
    // 选中竖条的宽度与高度（高度以字号的倍数计），以及是否落在高亮左缘之内
    std::optional<Length> selectedBarWidth;
    std::optional<double> selectedBarHeightEm;
    std::optional<bool> selectedBarInside;
    // 落在卡片四角上的高亮角跟卡片同一圆角
    std::optional<bool> itemCornersFollowCard;
    // 横排时把卡片多出的宽度均分给同一行各项，高亮铺满到卡片右边
    std::optional<bool> justifyRows;
    // 仅横排：候选保持一行；宽度不足时限制每项文字的显示宽度，不改变原始候选或序号。
    std::optional<bool> singleLine;
    // 隐藏预编辑行时抵掉它与候选之间的 2 DIP 行距
    std::optional<bool> trimHiddenPreeditGap;
    // 翻页箭头样式：chevron（折线）/ triangle（实心三角，横排前面隔一条分隔线）；为空沿用 base
    std::string pageArrowStyle;
    // 预编辑行的外边距（上右下左 DIP）
    std::optional<InsetsDip> preeditMarginDip;
};

// 挂件 z_index 低于它的画在面板（卡片）之下，其余画在面板之上。面板自身相当于 z_index = 1，
// 与样例预览页 .container 的 z-index: 1 一致。
inline constexpr int kDecorationFrontZIndex = 1;
inline constexpr int kDecorationZIndexLimit = 1000;
inline constexpr double kDecorationOffsetLimitDip = 2000.0;
// 挂件层数上限，挡住把几百张大图塞进一个皮肤的清单。
inline constexpr size_t kMaxFreeDecorations = 32;

struct FreeDecoration
{
    std::string image;
    // 锚定基准：top-left, top-right, bottom-left, bottom-right, left, right, top, bottom, center
    std::string anchor = "right";
    double offsetXDip = 0.0;
    double offsetYDip = 0.0;
    double widthDip = 0.0;
    double heightDip = 0.0;
    // -1000–1000；低于 kDecorationFrontZIndex 的在面板之下。同层按声明顺序叠放。
    int zIndex = 2;
};

// 横排首行内部词间的双线；坐标相对于卡片，全部使用 DIP。没有此样式时不绘制。
struct ItemSeparators
{
    std::string color;
    double topDip = 0.0;
    double bottomDip = 0.0;
    double lineWidthDip = 0.0;
    // 两条线的中心距，不小于线宽。
    double lineGapDip = 0.0;
};

// 固定比例的横排飘带：背景固定在左侧并裁去右端，前景整图随卡片右端移动。
// 两层都在同一候选窗口内、文字之前绘制，不参与测量或命中测试。
struct BandArtwork
{
    std::string background;
    std::string foreground;
    double widthDip = 0.0;
    double heightDip = 0.0;
    double offsetYDip = 0.0;
    double foregroundRightClipDip = 0.0;
};

// Optional, zero-measure foreground particles. All coordinates are relative to the candidate card in DIPs.
struct TypedStars
{
    std::string yellowImage;
    std::string pinkImage;
    std::string blueImage;
    double originXDip = 66.52;
    double originYDip = 22.57;
    double minXDip = 8.0;
    double maxXDip = 107.0;
    double minYDip = -100.0;
    double maxYDip = 132.0;
    double widthMinDip = 19.0;
    double widthMaxDip = 23.0;
    double riseMinDip = 24.0;
    double riseMaxDip = 38.0;
    double endXMinDip = 18.0;
    double endXMaxDip = 94.0;
    double endYMinDip = 96.0;
    double endYMaxDip = 108.0;
    double durationMinMs = 1200.0;
    double durationMaxMs = 1450.0;
    double fadeInMinMs = 90.0;
    double fadeInMaxMs = 130.0;
    double fadeOutMinMs = 260.0;
    double fadeOutMaxMs = 360.0;
    double rotationMinDeg = -24.0;
    double rotationMaxDeg = 24.0;
    double spinMinDeg = -70.0;
    double spinMaxDeg = 70.0;
    double pairSeparationDip = 22.0;
};

// Optional v2 realtime companion. Stage and sprite dimensions use DIPs; the anchor is the sprite foot center.
struct LiveSD
{
    std::string clickImage;
    double clickDurationMs = 1600.0;
    std::string idleImage;
    std::string idle2Image;
    std::string happyImage;
    std::string missImage;
    double stageWidthDip = 520.0;
    double stageHeightDip = 246.0;
    double widthDip = 161.28;
    double heightDip = 185.472;
    double footYDip = 233.0;
};

struct Package
{
    // 皮肤规范版本：1 为通用现代卡片皮肤，2 为支持九宫格与自由挂件的高级皮肤。v2 必须以 canvas 为 base，
    // 声明 2 而 base 不是 canvas 的按 1 处理（v2 字段全部忽略），并在 notices 里记一条。
    int schemaVersion = 1;
    // 不妨碍加载的问题；Scan 把它们和加载失败一起列进 issues，设置页的诊断信息会显示。
    std::vector<std::string> notices;
    std::string id;
    std::string name;
    std::string version;
    std::string author;
    std::string description;
    // 渲染用的本地基底；共享基底在 Load 中转为 Fluent，并补齐缺省配色与有效 themes。
    std::string base = "fluent";
    std::vector<std::string> layouts;
    std::vector<std::string> themes;
    double minWidthDip = 0.0;
    // 横排卡片的最大宽度（1–4096 DIP），包含内容内边距，不含阴影与 outer_insets_dip；未设置时不增加限制。
    // min_width_dip 不得大于此值。竖排忽略此限制。
    std::optional<double> maxWidthDip;
    // 仅横排：水平定位按不透明卡片的 max_width_dip 预留，不计 frame、阴影与 outer 留白。
    // 预留不改变实际卡片或宿主窗口的宽高，也不改变垂直定位；实际宽度仍随内容变化。
    // 默认关闭；开启时必须设置 max_width_dip。
    bool reserveMaxWidth = false;
    // 覆盖基础皮肤的候选框外框圆角；未设置时沿用基础皮肤。
    std::optional<double> cornerRadiusDip;
    // 候选框外框线宽（0–4）与候选项高亮圆角（0–16）；未设置时沿用基础皮肤。
    std::optional<double> borderWidthDip;
    std::optional<double> itemCornerRadiusDip;
    // 候选框卡片阴影：none / soft / strong；为空表示沿用基础皮肤。不影响右键菜单的阴影。
    std::string shadow;
    // 候选框卡片背后的系统材质：none / acrylic（模糊后面的窗口）/ mica（模糊桌面壁纸，Win11；不支持时按 acrylic）；
    // 为空等同 none。色调取 surface，参数见 CandidateColors::backdropOpacity。系统关闭透明效果时退回不透明底色。
    std::string backdrop;
    // 候选字体族，排在用户配置的字体之前，用户字体作为回退；为空表示不改。
    std::string fontFamily;
    // 候选框卡片内的背景图，绘制在底色之上、候选文字之下，按外框圆角裁剪。
    std::string backgroundImage;
    std::string backgroundFit = "cover"; // cover / contain / stretch
    double backgroundOpacity = 1.0;
    double decorationTopDip = 0.0;
    double decorationWidthDip = 0.0;
    // 卡片上方的装饰图；为空表示皮肤没有装饰。
    std::string decorationImage;
    std::string decorationAlign = "right"; // left / center / right，相对卡片
    // 候选框里的翻页箭头；未设置时沿用 base 默认皮肤的设置（见 ResolvePageArrows）。
    std::optional<bool> pageArrows;
    // 卡片严格按候选文字所需的宽度收紧，不再垫出厂最小宽度、也不在同一输入里只增不减；
    // min_width_dip 与装饰图宽度仍然生效。未设置时沿用 base 默认皮肤的设置（见 ResolveFitContent）。
    std::optional<bool> fitContent;
    // 卷轴展开：横排候选行右端放一个展开按钮，点开后在候选行下面展开后续候选的网格，右侧细滚动条，
    // 滚轮上下滚动而不是翻页。未设置时沿用 base 默认皮肤的设置（见 ResolveExpandPanel）。
    std::optional<bool> expandPanel;
    // 展开后网格一屏显示的行数（2–8）；未设置时为 kDefaultExpandRows。
    std::optional<int> expandRows;
    CandidateColors dark;
    CandidateColors light;
    // 候选框版式：两种排列共用的，以及只对横排 / 竖排生效的。
    CandidateGeometry geometry;
    CandidateGeometry horizontalGeometry;
    CandidateGeometry verticalGeometry;
    std::optional<ItemSeparators> itemSeparators;
    std::optional<BandArtwork> bandArtwork;
    // 覆盖基础皮肤的悬浮工具栏外框圆角与图标悬停底色的圆角；未设置时沿用基础皮肤。
    std::optional<double> toolbarCornerRadiusDip;
    std::optional<double> toolbarIconCornerRadiusDip;
    ToolbarColors toolbarDark;
    ToolbarColors toolbarLight;
    // v2 专有特性：九宫格贴图边框
    std::optional<BorderImage> borderImage;
    // v2 专有特性：面板内容/文字安全内边距，从面板外缘量起；某一边小于九宫格边框宽度时按边框宽度算。
    std::optional<InsetsDip> contentInsetsDip;
    // v2 专有特性：预编辑行与候选行之间的行距（DIP，0–100）；未设置时沿用 base 的行距。
    std::optional<double> lineGapDip;
    // v2 专有特性：面板之外需要额外预留的窗口空间（上右下左 DIP），给挂在卡片外沿的挂件用。
    // 不声明时窗口只按卡片大小开，挂到外面的立绘会被窗口边界裁掉。
    std::optional<InsetsDip> outerInsetsDip;
    // v2 专有特性：多层自由锚定挂件（支持动图）
    std::vector<FreeDecoration> freeDecorations;
    // Absent by default, and ignored by v1 skins. Does not alter candidate geometry.
    std::optional<TypedStars> typedStars;
    // Absent or disabled by default, and ignored by v1 skins. Does not alter candidate geometry.
    std::optional<LiveSD> liveSD;
};

struct Issue
{
    std::string folder;
    std::string reason;
};

struct ScanResult
{
    std::vector<Package> packages;
    std::vector<Issue> issues;
};

// 内置皮肤的清单：<skinsRoot>/default/<id>/skin.toml，同时承载皮肤级的开关与完整外观。出厂清单编进程序，
// 安装时整份覆盖数据目录里的那一份；数据目录里的清单写了的键覆盖出厂值（技术上能改，但不建议，升级会冲掉）。
// 外部皮肤没写的开关与外观都沿用它 base 的这份清单。
struct DefaultSkin
{
    std::string id;
    std::string name;
    // 设置页皮肤卡片上的一句说明。
    std::string description;
    std::optional<bool> pageArrows;
    std::optional<bool> fitContent;
    std::optional<bool> expandPanel;
};

// 默认皮肤清单所在的子目录名，Scan 跳过它，外部皮肤也不能占用这个 ID。
inline constexpr const char *kDefaultSkinsFolder = "default";
// 默认皮肤清单缺失或没写 page_arrows 时的取值，与出厂清单一致：翻页箭头默认关闭。
inline constexpr bool kDefaultPageArrows = false;
// 默认皮肤清单缺失或没写 fit_content 时的取值：卡片保留出厂最小宽度。
inline constexpr bool kDefaultFitContent = false;
// 默认皮肤清单缺失或没写 expand_panel 时的取值：用翻页而不是卷轴展开。
inline constexpr bool kDefaultExpandPanel = false;
// 卷轴展开后一屏的网格行数，及皮肤可写的范围。
inline constexpr int kDefaultExpandRows = 4;
inline constexpr int kMinExpandRows = 2;
inline constexpr int kMaxExpandRows = 8;

// 全贴图基底：透明面板、无描边、无圆角、无阴影，外观完全来自九宫格面板图与挂件，是 v2 与自定义皮肤的地基。
// 它只能作为外部皮肤的 base，不能单独选用（没有贴图时什么也看不见），也不在 BuiltInIds 里。
inline constexpr const char *kCanvasBase = "canvas";
bool IsCanvasBase(const std::string &base);
// v2 图片（border_image.source、decorations.image）允许的扩展名：.png、.apng、.gif、.webp，不分大小写。
bool IsAnimatableImagePath(const std::string &path);

// 指定主题下实际要画的面板图：浅色主题优先 light_source，没写就用 source。
const std::string &BorderImageSource(const BorderImage &borderImage, bool light);

// 内置皮肤的 ID，按设置页的展示顺序。每个都有 skins/default/<id>/skin.toml 出厂清单。
const std::vector<std::string> &BuiltInIds();
bool IsBuiltIn(const std::string &id);
// 官方贴图皮肤：本身是 skins\<id> 下的外部皮肤包，随安装包装到数据目录、照外部皮肤加载和渲染，
// 只是设置页把它们列在内置皮肤之后，而不是「外部皮肤」列表里。按设置页的展示顺序。
const std::vector<std::string> &OfficialPackageIds();
bool IsOfficialPackage(const std::string &id);
bool IsSafeId(const std::string &id);
// 安装包覆盖同名随包皮肤前，把旧目录改名成 <id>.bak（重名时 <id>.2.bak …）；Scan 跳过它们。
bool IsBackupFolder(const std::string &folder);
bool Supports(const Package &package, const std::string &layout, const std::string &theme);
std::optional<Package> Load(const std::filesystem::path &skinsRoot, const std::string &id,
                            std::string *error = nullptr);
// 读取外部皮肤，但只在它声明支持该布局与主题时才返回；不支持或读不出来时返回空，
// 调用方整份不套用、回退到 fluent（皮肤自己声明的 base 也不用）。
// 两个渲染后端与辅助浮层共用这一条判定，避免某个使用方只 Load 不校验。
std::optional<Package> LoadSupported(const std::filesystem::path &skinsRoot, const std::string &id,
                                     const std::string &layout, const std::string &theme);
ScanResult Scan(const std::filesystem::path &skinsRoot);
std::optional<DefaultSkin> LoadDefault(const std::filesystem::path &skinsRoot, const std::string &id,
                                       std::string *error = nullptr);
// skinId 是配置里的候选皮肤：内置皮肤读自己的默认清单；外部皮肤先看自己的 page_arrows，
// 没写再看 base 的默认清单。package 为外部皮肤已加载的清单，内置皮肤传 nullptr。
bool ResolvePageArrows(const std::filesystem::path &skinsRoot, const std::string &skinId, const Package *package);
// fit_content 的解析顺序同 ResolvePageArrows。
bool ResolveFitContent(const std::filesystem::path &skinsRoot, const std::string &skinId, const Package *package);
// expand_panel 的解析顺序同 ResolvePageArrows。
bool ResolveExpandPanel(const std::filesystem::path &skinsRoot, const std::string &skinId, const Package *package);

// 有出厂清单的基底：六个内置皮肤与 canvas。
bool HasFactoryLook(const std::string &id);
// 编进程序的出厂清单解析出的外观（候选配色、版式、工具栏）。没有出厂清单的 id 用 fluent 的。
const Package &FactoryLook(const std::string &id);
// 实际生效的内置外观：出厂外观上叠加数据目录 skins/default/<id>/skin.toml 里写了的键；
// 那份清单缺失或读不出来时就是出厂外观。没有出厂清单的 id 用 fluent 的。
Package ResolveBuiltInLook(const std::filesystem::path &skinsRoot, const std::string &id);
// 内置皮肤的名称与说明（设置页用）：数据目录里的清单写了就用它的，否则用出厂清单的。
DefaultSkin ResolveBuiltInInfo(const std::filesystem::path &skinsRoot, const std::string &id);
} // namespace CandidateSkinCatalog
