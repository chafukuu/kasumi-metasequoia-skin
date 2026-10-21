# 外部皮肤 manifest（TOML）

每个皮肤目录需要 `skin.toml`，`id` 必须与文件夹名一致。`schema_version` 支持 `1` 与 `2`：v2 是面向全贴图与复杂立绘皮肤的规范，必须以 [`canvas`](#canvas-基底v2) 为 `base`，由 Direct2D 候选窗完整绘制（九宫格面板、上下两层挂件、动图）。

候选框和悬浮工具栏都由 `skin.toml` 声明，不要再提供 `cand.css` 或工具栏 CSS。所有图片路径都相对于皮肤目录，且不能指向目录之外。

## 顶层字段

- 必填：`schema_version`（`1` 或 `2`）、`id`、`name`、`version`、`base`（继承的内置皮肤：`fluent`、`wechat`、`graphite`、`willow_green`、`autumn_osmanthus`、`microsoft`；或与 msime 共享的基底：`system`、`shuishan`、`light`、`paper`、`night`、`ink`，按 `fluent` 的结构绘制并补上基底配色，固定明暗的基底只在对应的 `themes` 下生效；或全贴图基底 `canvas`，只用于 v2）
- `schema_version` 与 `base` 的搭配：
  - `schema_version = 2` 必须写 `base = "canvas"`。写了 2 而 `base` 是别的基底时，皮肤仍可使用，但按 `schema_version = 1` 加载，下面标「v2」的表和键全部忽略（连同其中的错误），设置页的诊断信息里会多一条说明。
  - `schema_version = 1` 不能用 `canvas`：没有面板贴图它只是一张透明卡片，这种皮肤不会出现在可选列表里。
- 可选：`author`、`description`
- `[supports]`：`layouts`（`horizontal` / `vertical`）、`themes`（`dark` / `light`）
- `[license]`：`code`、`assets`

## `[candidate_window]`

- `min_width_dip`：卡片最小宽度；实际最小宽度还会被装饰图的 `width_dip` 撑大
- `max_width_dip`：可选，仅限制横排卡片的最大宽度，`1`–`4096` 的有限数值（DIP），例如 `1032.0`；包含内容内边距，不包含阴影和 `outer_insets_dip`。不写时保持原有布局，竖排忽略。`min_width_dip` 大于此值时清单加载失败；可用屏幕空间更小时以屏幕为限。达到上限后默认按原生布局换行；设定下述 `single_line = true` 可以让候选保持一行
- `reserve_max_width`：可选布尔值，默认 `false`；设为 `true` 时必须同时设置 `max_width_dip`，否则清单加载失败。仅横排生效：水平定位只按不透明卡片的 `max_width_dip` 预留宽度，不加 frame、阴影或 `outer_insets_dip`，透明外缘允许超出屏幕。此虚拟宽度只参与水平定位，实际卡片和宿主窗口的宽高、垂直位置仍由原生布局决定，卡片宽度继续随内容变化。竖排忽略；旧皮肤不写时保持原有定位
- `corner_radius_dip`：卡片圆角，`0`–`32`；不写则沿用 `base`
- `border_width_dip`：卡片外框线宽，`0`–`4`；颜色取 `border`，不写 `border` 时取 `base` 的边框色（杨柳青、秋桂没有外框，此时仍不可见）
- `item_corner_radius_dip`：选中 / 悬停高亮的圆角，`0`–`16`；贴着卡片四角的那几个角仍跟 `corner_radius_dip`
- `shadow`：卡片阴影，`none` | `soft` | `strong`；不写则沿用 `base`。只影响候选卡片，不影响右键菜单
- `backdrop`：卡片背后的系统材质，`none` | `acrylic` | `mica`；不写等于 `none`。`acrylic` 模糊卡片后面的窗口；`mica` 模糊桌面壁纸，只透出壁纸色调（需要 Windows 11，更早的系统按 `acrylic` 画）。材质按 Fluent 配方叠「模糊背景 → 亮度层 → 色调」，色调颜色取 `surface`，两个不透明度见下面配色表里的 `backdrop_opacity` / `backdrop_luminosity`。系统关闭「透明效果」或不支持（低于 Windows 10 1903）时，`surface` 按不透明画。写了 `border_image` 时不生效
- `font_family`：候选字体名，UTF-8 编码后最长 64 字节（约 21 个汉字），不能含引号、反斜杠、逗号、分号、尖括号、花括号和反引号。排在用户设置的字体前面，缺字时仍回落到用户字体；字号不由皮肤决定
- `page_arrows`：候选框里的翻页箭头，`true` | `false`。横排时是候选右侧贴着最后一行的一小列，竖排时是最后一行下方的一矮行、“‹”与序号左对齐；颜色取 `number`，悬停底色取 `hover`。不写则沿用 `base` 的默认皮肤设置（数据目录 `skins\default\<base>\skin.toml`，出厂为 `false`）
- `fit_content`：卡片是否严格按候选文字所需的宽度收紧，`true` | `false`。打开后不再垫出厂的最小宽度（160 DIP），同一次输入里后到的翻译等内容消失时宽度也立刻缩回；`min_width_dip` 和装饰图 `width_dip` 仍然生效。不写则沿用 `base` 的默认皮肤设置（出厂为 `false`）

### `[candidate_window.horizontal]` 的 `single_line`（可选）

布尔值，默认 `false`。设为 `true` 时横排候选保持单行；可用宽度不足时按每项的真实文字宽度分配空间，过长文字以省略号显示，保留原始候选内容、序号与数字选词操作。竖排忽略。适合只能容纳一行的固定贴图候选条。

### `[candidate_window.item_separators]`（可选）

按横排首行的实际候选布局绘制内部词间双细线；只有一个词时不画，也不在首尾画。与文字使用同一次原生绘制，不依赖诊断日志或外部浮层。没有此表时不画；竖排忽略。表中五个字段都必须给出：

- `color`：使用下文的颜色格式，对深浅主题均生效
- `top_dip` / `bottom_dip`：相对卡片顶边的线段起止位置，`0`–`4096` 的有限 DIP 数值，`bottom_dip` 必须大于 `top_dip`；线段被限制在卡片高度之内
- `line_width_dip`：每条线的宽度，`0`–`4096` 的有限 DIP 数值，不含 `0`
- `line_gap_dip`：双线的中心距，`0`–`4096` 的有限 DIP 数值，不得小于线宽

```toml
[candidate_window.item_separators]
color = "#ffad00"
top_dip = 6.75
bottom_dip = 38.25
line_width_dip = 0.4875
line_gap_dip = 1.05
```

### `[candidate_window.band_artwork]`（可选）

固定尺寸的横排双层飘带。背景、前景、候选文字及词间分隔线都在同一个候选窗口内绘制、提交和显示隐藏，避免外部装饰窗口与文字的出现时机不同。没有此表时保持原行为；竖排忽略。

- `background` / `foreground`：必填，皮肤内已存在的相对 `.png` 路径。两图使用完整源图，分别映射到下面的固定绘制尺寸，不随候选条伸缩而横向拉伸
- `width_dip` / `height_dip`：必填，完整图层绘制宽高，`0`–`4096` 的有限 DIP 数值，不含 `0`。应按源图比例声明
- `offset_y_dip`：必填，相对卡片顶边的纵向偏移，`-4096`–`4096` 的有限 DIP 数值
- `foreground_right_clip_dip`：必填，背景右端与卡片右边之间的裁去宽度，`0`–`width_dip` 的有限 DIP 数值；背景保持左侧位置，裁切边随卡片右边变化
- `offset_x_dip`：可省略，给出时只能是数值 `0`。背景左侧与卡片左侧对齐；前景右侧与卡片右侧对齐，整图平移

两层都位于卡片正文与文字之下；此表不改变测量、卡片内边距、命中测试或外部留白。图片需要伸出卡片时仍应通过 `outer_insets_dip` 为实际非透明区域预留窗口空间。

```toml
[candidate_window.band_artwork]
background = "assets/band-background.png"
foreground = "assets/band-foreground.png"
width_dip = 1032.0
height_dip = 344.0
offset_y_dip = -219.75
foreground_right_clip_dip = 137.695027624309
```

### `[candidate_window.decoration]`（可选）

卡片上方的装饰图。写了这张表就必须同时给出 `image`、`top_inset_dip`、`width_dip`。

- `image`：图片路径
- `top_inset_dip` / `width_dip`：装饰框的高和宽，框贴在卡片上方，不与卡片重叠；图片在框内等比缩放
- `align`：`left` | `center` | `right`，相对卡片

### `[candidate_window.background]`（可选）

卡片背景图，画在底色之上、文字之下，按圆角裁剪，对深浅两套主题都生效。

- `image`：图片路径
- `fit`：`cover`（铺满裁切）| `contain`（完整显示）| `stretch`（拉伸）
- `opacity`：`0`–`1`

### `canvas` 基底（v2）

全贴图皮肤的地基：面板透明、没有描边、直角、默认没有阴影，候选框长什么样完全由下面的九宫格面板图和挂件决定。它只能作为 `base`，不能在设置页单独选用。

- 文字与高亮用不依赖底色的中性色：深色主题近白文字（`#f2f2f2`）、白色半透明的选中 / 悬停底，浅色主题近黑文字（`#1a1a1a`）、黑色半透明的选中 / 悬停底；高亮圆角 4，可用 `item_corner_radius_dip` 改。贴图偏深或偏浅时用 `[candidate.dark]` / `[candidate.light]` 改配色。
- v1 的键照样可用：配色、`font_family`、`page_arrows`、`fit_content`、`min_width_dip`、`max_width_dip`、`reserve_max_width`、`corner_radius_dip`、`border_width_dip`、`shadow`（写 `soft` / `strong` 才有阴影）、`[candidate_window.decoration]`、`[candidate_window.background]`、`[toolbar]`。写了 `border_image` 时，卡片的底色、描边、圆角、阴影和 `background` 背景图都让给面板图。
- 右键菜单与悬浮工具栏没有贴图，沿用 `fluent` 的样式，可用 `[candidate.*.menu]` 与 `[toolbar]` 改。翻页箭头、`fit_content` 的出厂值也取 `fluent` 的默认皮肤设置。

### `[candidate_window.border_image]`（v2 可选）

九宫格面板图，语义同 CSS `border-image`（`border-image-repeat: stretch`）：四角按边宽缩放，四边沿长度方向拉伸，中心双向拉伸。

- `source`：图片路径，`.png`、`.apng`、`.gif` 或 `.webp`
- `light_source`：可选，浅色主题换用的面板图，格式要求同 `source`。切片、边宽和 `fill` 与 `source` 共用，所以两张图要按同一套几何画；不写时深浅主题都用 `source`
- `slice_dip`：切片线位置，**按源图像素计**（同 CSS `border-image-slice` 的数值，键名里的 `_dip` 是历史写法），1、2 或 4 个 `0`–`500` 的数，按 CSS 简写展开成上右下左（1 个值四边相同，2 个值是「上下 左右」）
- `stretch_dip`：可选，同为源图像素，给出时代替 `slice_dip`。**面板图里画死的装饰必须落在切片之内（可拉伸区之外）**，否则会被拉长；例如帆船画在右侧，就把右侧切片放到船身左侧
- `width_dip`：可选，四边在屏幕上画多宽，**按 DIP 计**（同 CSS `border-image-width`），写法同上。不写时取切片的数值当 DIP。给 2 倍图时写成切片的一半，200% 缩放下逐像素清晰、100% 下是缩小一半的同一张图。两条对边加起来比面板还宽时四边按同一比例缩小
- `fill`：可选，布尔值，是否画中心块，默认 `true`；`false` 时中心留空、透出桌面

面板图画在卡片的底色之上、文字之下，不按圆角裁剪（同 CSS）。动图面板也会播放，但面板一般建议用静态图，把动画留给挂件。

### `[candidate_window.content_insets_dip]`（v2 可选）

候选框内容与文字的安全内边距，**从面板外缘量起**，用来避开贴图四周自带的花纹或立绘。某一边小于九宫格该边的边宽（`width_dip`）时按边宽算，文字不会压在边框上（同 CSS 的 `border + padding`）。不写时是边宽再加 5 DIP。

- `top` / `right` / `bottom` / `left`：四边内边距，`top` / `bottom` `0`–`200`，`left` / `right` `0`–`500`

### `[candidate_window]` 的 `line_gap_dip`（v2 可选）

预编辑行与候选之间的距离，`0`–`100`。只影响这一处，竖排时候选与翻页箭头之间的间距不变。不写则沿用 `base` 的行距。

### `[candidate_window.outer_insets_dip]`（v2 可选）

挂件挂在卡片外沿时，窗口需要额外预留的透明空白，从卡片四边往外量。

- `top` / `right` / `bottom` / `left`：四边预留量浮点数

**不声明时窗口只按卡片大小开，挂到卡片外面的立绘会被窗口边界裁掉。**

### `[[candidate_window.decorations]]`（v2 可选，数组表）

自由锚定的立绘挂件，可以叠加角色立绘与动图，最多 32 层。

- `image`：图片路径，`.png`、`.apng`、`.gif` 或 `.webp`
- `anchor`：挂件框贴着面板的哪个位置：`top-left` | `top-right` | `bottom-left` | `bottom-right` | `left` | `right` | `top` | `bottom` | `center`，默认 `right`
- `width_dip` / `height_dip`：挂件框的尺寸，`0`–`2000`（不含 0）；图片在框里等比缩放、居中（同 CSS `background-size: contain`）
- `offset_x_dip` / `offset_y_dip`：相对锚点的平移量，`-2000`–`2000`，正数向右、向下
- `z_index`：层级，`-1000`–`1000` 的整数，默认 `2`。面板自身相当于 `1`：**`z_index < 1` 的挂件画在面板之下**（只有伸出面板的部分看得见），其余画在面板与文字之上。同层按 `z_index` 从小到大、相同时按书写顺序叠放

挂件不参与布局，也不接收点击；伸到面板外面时要用 `outer_insets_dip` 给窗口留出空白。

### 动图（v2）

v2 皮肤里的图片可以是动图：APNG（按 [APNG 规范](https://wiki.mozilla.org/APNG_Specification) 解码）、GIF，以及系统 WebP 解码器能逐帧读出的动画 WebP（每帧 100 ms）。

- 帧时长与浏览器一致：`delay_den` 为 0 按 100 算，10 ms 及以下按 100 ms；APNG 的 `num_plays`、GIF 的 NETSCAPE 循环次数生效，播完停在最后一帧。
- 动画从候选窗出现时开始，同一次显示里打字不会让它重头开始；候选窗隐藏或换皮肤后，下一次显示从第一帧开始。
- 系统关闭了动画效果（设置 → 辅助功能 → 视觉效果 → 动画效果）时显示静态图：APNG 显示它的默认图（通常就是第一帧），GIF 显示第一帧。
- 一张动图超过 1024 帧或解码后超过 64 MB、文件损坏或不合规范时只显示第一帧（静态图）。所有动图的帧合计受 64 MB 显存预算约束。
- v1 皮肤里的图片始终是静态首帧，与以往一致。

## `[candidate.dark]` / `[candidate.light]`

`accent`、`selected`、`hover`、`surface`、`border`、`text`、`number`、`translation`、`show_selected_bar`

`candidate_window.backdrop` 不是 `none` 时还可以写两个 `0`–`1` 的数，深浅主题各自设置：

- `backdrop_opacity`：`surface` 色调盖在材质上的不透明度，越高越接近纯色。不写时取 `surface` 自带的 alpha
- `backdrop_luminosity`：亮度层的不透明度。越高，卡片明暗越不受后面画面影响，亮背景上的浅色字也能保住对比度；调低更通透，对比度跟着后面走。不写时 `acrylic` 取 `0.85`、`mica` 取 `1`

Windows 11 的默认值可以作参考：亚克力深色 `#2c2c2c` / `0.15` / `0.96`、浅色 `#fcfcfc` / `0` / `0.85`；云母深色 `#202020` / `0.8` / `1`、浅色 `#f3f3f3` / `0.5` / `1`（依次是 `surface`、`backdrop_opacity`、`backdrop_luminosity`）。

细分配色，都可省略，不写时按右列回落：

| 键 | 作用 | 不写时 |
|---|---|---|
| `candidate_text` | 普通候选的文字 | `text` |
| `preedit_text` | 预编辑（拼音）文字 | `text` |
| `preedit_caret` | 预编辑光标 | `accent` |
| `selected_text` | 选中候选的文字 | `base` 的选中行文字色 |
| `selected_number` | 选中候选的序号 | `base` 的选中行序号色 |
| `translation_pos` | 翻译里的词性（`n.`、`adj.`、`proper n.` 等） | 与翻译同色 |
| `translation_separator` | 翻译里义项之间的分隔符（`;`、`；`） | 与翻译同色 |
| `selected_translation` | 选中候选的翻译 | `base` 的选中行翻译色（微信、杨柳青、秋桂铺实色高亮，各有一组）；`base` 没有时用 `translation`，再不写则取选中文字色的 62% 不透明度 |
| `selected_translation_pos` | 选中候选翻译里的词性 | 写了 `selected_translation` 时与它同色；否则 `base` 的选中行词性色，再回落到 `translation_pos` |
| `selected_translation_separator` | 选中候选翻译里的分隔符 | 写了 `selected_translation` 时与它同色；否则 `base` 的选中行分隔符色，再回落到 `translation_separator` |
| `selected_bar` | 选中项左侧竖条 | `accent` |
| `preedit_background` | 预编辑行底色，圆角同高亮圆角 | 透明 |
| `preedit_divider` | 预编辑行下方 1px 分隔线 | 无 |

设置页里的「候选文字颜色」优先于 `text`、`candidate_text` 和 `preedit_text`。

内置皮肤的翻译用 [Notion 调色板](https://docs.super.so/notion-colors)：单词分别是 fluent 紫、微信蓝、杨柳青绿、石墨棕、微软粉，秋桂浅色橙、深色黄；词性换成相邻的另一个 Notion 色（紫配粉、蓝配绿、绿配蓝、棕配橙、橙配红、黄配橙、粉配紫）再加 `80` 透明度调淡，与单词区分开；分隔符是单词色加 `73` 透明度。包写了 `translation` 而没写 `translation_pos` / `translation_separator` 时，`base` 的词性和分隔符配色一并作废，跟随新的翻译色。想要同样的效果，词性取另一个色相、在色值后补两位十六进制透明度即可：

```toml
translation = "#529cca"
translation_pos = "#4dab9a80"
translation_separator = "#529cca73"
```

### `[candidate.dark.menu]` / `[candidate.light.menu]`（可选）

候选窗右键菜单：`background`、`border`、`text`、`hover`。

## `[toolbar]`

悬浮工具栏样式。所有键都可省略，不写的沿用 `base`。

- `corner_radius_dip`：`0`–`32`
- `[toolbar.dark]` / `[toolbar.light]`：`background`、`border`、`handle`（左侧拖动条）、`divider`、`icon`、`hover`。`handle` 不写时取同一主题的 `[candidate.*].accent`，与选中竖条、预编辑光标同色；`accent` 也没写才沿用 `base`。写的话也请与 `accent` 一致

## 颜色格式

所有颜色键——`[candidate.dark]` / `[candidate.light]` 里除 `show_selected_bar` 外的键、`[candidate.*.menu]` 和 `[toolbar.dark]` / `[toolbar.light]` 的全部键——都接受以下写法。数字、布尔、枚举和路径类的键不是颜色，不能写成颜色值。

| 写法 | 示例 | 说明 |
|---|---|---|
| `#rgb` / `#rrggbb` | `#e8a`、`#e08aa8` | 不透明 |
| `#rgba` / `#rrggbbaa` | `#e8a4`、`#e08aa847` | 末尾一位或两位是不透明度 |
| `rgb(r, g, b)` | `rgb(224, 138, 168)` | `r`、`g`、`b` 为 0–255 |
| `rgba(r, g, b, a)` | `rgba(224, 138, 168, 0.28)` | `a` 为 0–1 |
| `transparent` | `transparent` | 全透明，等同 `#0000` |

- 十六进制大小写均可，但**必须带 `#`**：这是清单格式的约定，与 CSS 写法一致；渲染器对不带 `#` 的写法有容错，但不要依赖它。
- 不透明度在**末尾**（`#RRGGBBAA`），不是 Windows 常见的 `#AARRGGBB`。例如 `#e9e8e89d` 约为 62% 不透明，`rgba(224, 138, 168, 0.28)` 与 `#e08aa847` 等价。
