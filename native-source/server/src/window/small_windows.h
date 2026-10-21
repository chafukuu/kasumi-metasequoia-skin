#pragma once

// 候选窗、悬浮工具栏、托盘菜单三个小窗口共用的 Server 侧状态：置顶顺序、皮肤重载、
// 工具栏的输入状态缓存。窗口本身由 msimeui 的 presenter 绘制。

#include <windows.h>

#include <cstdint>

// --- 置顶 ---

// 第一次真正显示时请求把三个小窗口提到 HWND_TOPMOST。三个宿主按固定间隔依次进入
// 置顶带（候选窗、工具栏、托盘菜单，菜单最后，打开的菜单因此在最上面）。
// 返回调用结束时置顶是否已经生效。
bool EnsureSmallWindowsTopmost(const wchar_t *reason);
// 三个宿主都已进入置顶带时为 true。分阶段置顶完成前的几秒里保持 false。
bool AreSmallWindowsTopmostApplied();
// 把托盘菜单重新提到置顶带最前（例如工具栏后置顶之后）。
void RaiseTrayMenuAboveSmallWindows(const wchar_t *reason);
// 托盘菜单正打开在用户面前时为 true。
bool IsTrayMenuOpenToUser();

// --- 皮肤与主题 ---

// 皮肤、主题或候选窗布局改了：重新套用工具栏主题。候选窗与托盘菜单在下一次显示时按
// 配置自己重新解析皮肤。
void ApplyConfiguredUiThemes();
// 强制重读同名皮肤包（设置页改了外部皮肤文件）。递增 GetCandidateSkinReloadRevision。
void ForceReloadConfiguredCandidateSkin();
// 每次强制重载加一，presenter 把它算进皮肤指纹，同名皮肤也会重新加载。
uint64_t GetCandidateSkinReloadRevision();

// --- 悬浮工具栏的输入状态 ---

// 这些入口总是先更新工具栏的状态缓存，再同步给工具栏 presenter。
void UpdateFtbCnEnAndPuncState(int cnEnState, int puncState);
void UpdateFtbCnEnAndDoubleSingleAndPuncState(int cnEnState, int doubleSingleByteState, int puncState,
                                              int capsLockState = 0);
void UpdateFtbPuncState(int puncState);
void UpdateFtbDoubleSingleByteState(int doubleSingleByteState);
void UpdateFtbEnglishInputModeState(int enabled);
void UpdateFtbCapsLockState(int enabled);
void UpdateFtbCharacterSetState();
void UpdateFtbInputModeState(int japaneseMode);
// 把配置的可选按钮重新套到工具栏上并调整宿主尺寸。
void ApplyConfiguredFloatingToolbarItems();

// --- 托盘菜单 ---

// 托盘菜单里的悬浮工具栏开关与输入方案勾选以 config.toml 为准。
void SyncMenuFloatingToolbarToggle();
