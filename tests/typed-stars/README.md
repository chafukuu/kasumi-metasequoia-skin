# v0.5.6 星星回归测试

这些测试直接使用 `native-source/` 中的生产头文件和输入事件挂钩源码。测试不读取用户配置或词库，不安装、启动或重启输入法，也不要求先构建完整 Server 或下载 vendor 依赖。

## 运行

需要 Windows x64、Visual Studio C++ Build Tools、Windows SDK 和 CMake。先打开 x64 Native Tools / Developer PowerShell，在仓库根目录运行：

```powershell
cmake -S tests/typed-stars -B build/typed-stars
cmake --build build/typed-stars --config Release
ctest --test-dir build/typed-stars -C Release --output-on-failure
```

Python 3.9+ 可运行两项输入挂钩源码检查。安装 Pillow 后还可运行蓝色星星逐像素校验；CMake 会明确提示缺少 Python 或 Pillow 时未注册的检查。也可以单独执行：

```powershell
python tests/typed-stars/verify_typing_star_hooks.py
python tests/typed-stars/verify_typing_star_dedup.py
python tests/typed-stars/verify_blue_star.py skin/kasumi-adaptive
```

## 覆盖范围

- `TypingStarEvents`：每两个接受字符触发一次双星事件，编辑打断未完成计数但保留排队事件及视觉代际，真正结束组合时复位，并发消费和旧 Hide 消息竞态。
- `TypedStarsSmoke`：精确的蓝色 : 品红 : 黄色 `2 : 1 : 1` 洗牌袋、旧双色兼容、颜色切换不改变运动、轨迹与透明度、生命周期、边界、容量及非法参数。
- `AnimationSchedulerSmoke`：定时器与消息去重、取消与销毁、过期帧让出执行、透明测试窗口的重绘行为。测试创建自己的 Win32 测试窗口和消息队列；计时结果不等于已测量实际输入法视觉帧率。
- `TypingStarHooks` / `TypingStarDedup`：真实输入挂钩边界、候选帧去重后事件投递、显隐与系统动画开关约束，以及无布局修改的重绘路径。
- `BlueStarPixels`：两图均为 114×136 PNG，蓝色逐像素满足 `(R,G,B,A) → (G,B,R,A)`，alpha 完全一致。

这些是独立组件回归测试。完整 Server 的皮肤解析与跨 DPI 候选布局验证使用单独的产品构建链，不包含在这个简化入口内。
