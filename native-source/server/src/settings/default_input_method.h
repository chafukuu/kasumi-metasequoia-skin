#pragma once

// 把水杉设为当前用户的默认输入法：在系统设置「语言和区域 → 中文(简体) → 语言选项」里排第一。
//
// 只在设置页用户主动点击时调用，绝不在启动或升级时静默改写。写入的是 HKCU 下的
// 每用户设置，设置进程以当前用户身份运行即可，不需要提权。
//
// 不在键盘列表时先用 input.dll 的 InstallLayoutOrTip 加入（会排在该语言末尾），再用
// SetDefaultLayoutOrTip 挪到第一位；它同时更新 Win + Space 的切换顺序，不改语言列表顺序，
// 因此也不会动界面语言。
//
// 不写「高级键盘设置 → 替代默认输入法」（InputMethodOverride）：它会压过语言列表的顺序，
// 用户以后在系统设置里把别的输入法排到第一也不生效。旧版写过、仍指向水杉的，设为默认时
// 顺手清掉，让默认输入法完全由顺序决定。
//
// 读取不用 input.dll 的 EnumEnabledLayoutOrTip：它在 Windows 10/11 上对 TSF 输入法返回空
// 列表，判断不了排序，因此直接读系统设置页自己维护的 International\User Profile。

#include <Windows.h>

#include <string>

namespace default_input_method
{

struct Status
{
    bool enabled = false;    // 水杉在当前用户的键盘列表里
    bool is_default = false; // 水杉在它所在语言里排第一
};

Status QueryStatus();

// 先确保水杉在键盘列表里，再挪到第一位；完成后按注册表复核。失败时 error 给出人类可读原因。
bool SetAsDefault(std::string &error);

// 打开系统的键盘设置，供 API 失败或用户想手动调整时使用。
void OpenSystemSettings(HWND owner);

} // namespace default_input_method
