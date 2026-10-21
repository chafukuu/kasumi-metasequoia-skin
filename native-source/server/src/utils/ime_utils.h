#pragma once
#include <string>

std::wstring GetPreedit();
// 候选窗预编辑，光标处插一个 U+E000。
std::wstring GetPreeditWithCaretMarker();
// 回给 TSF 的行内预编辑：word 是造词前缀，raw 是剩余按键原串。双拼开启「显示全拼」时是转换后的
// 全拼，末尾附光标映射表（engine/contracts/preedit_caret_map.h）。
std::wstring BuildTsfPreedit(const std::wstring &word, const std::string &raw);
std::wstring GetTsfPreedit();
