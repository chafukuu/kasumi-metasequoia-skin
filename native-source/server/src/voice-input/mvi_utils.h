#pragma once

#include <windows.h>

namespace mvi_utils
{
// 语音条锚定所用的显示器：前台窗口所在显示器。GetForegroundMonitorMetrics 与
// GetForegroundMonitorScale 都以它为准，保证定位矩形与缩放同源。
HMONITOR GetForegroundMonitor();
// 前台窗口所在显示器的整屏矩形与工作区。一次解析同一显示器，供定位时
// “水平按整屏居中 + 垂直贴工作区底部”共用，避免两次解析落到不同显示器。
struct MonitorMetrics
{
    RECT full{}; // rcMonitor：整屏，用于水平居中（不受左/右任务栏影响）
    RECT work{}; // rcWork：工作区，已扣除任意边缘的任务栏/应用栏，用于贴底
};
MonitorMetrics GetForegroundMonitorMetrics();
// 目标显示器的有效缩放（DPI/96）。与候选框 ScaleFromMonitor 一致地使用
// GetDpiForMonitor(MDT_EFFECTIVE_DPI)，而非 GetDpiForWindow，避免分辨率切换时
// 取到陈旧或错误显示器的 DPI 导致尺寸/缩放不对。
float GetForegroundMonitorScale();
} // namespace mvi_utils
