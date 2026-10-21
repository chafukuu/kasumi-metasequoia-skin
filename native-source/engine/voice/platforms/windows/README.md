# Metasequoia Voice Input(水杉记言)

English | [简体中文](../../README.zh-CN.md)

This standalone Windows host lives in `engine/voice/platforms/windows/` and links the public voice targets. It is off by default and is not what the input method ships: the product's voice host is `server/`.

## Build and run

Run from the **`engine/` directory** with Visual Studio's C++ tools, CMake and vcpkg installed. Set `VCPKG_ROOT` to your vcpkg checkout first:

```powershell
cmake -S voice -B build-voice -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows-static '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>' -DVCPKG_MANIFEST_FEATURES=windows-app -DMSIME_VOICE_WINDOWS_APP=ON
cmake --build build-voice --config Release --parallel
New-Item -ItemType Directory -Force "$env:LOCALAPPDATA/MetasequoiaVoiceInput"
Copy-Item voice/assets/* "$env:LOCALAPPDATA/MetasequoiaVoiceInput" -Recurse -Force
```

Edit the copied `config.toml` with your own SiliconFlow token, then launch `build-voice/platforms/windows/Release/MetasequoiaVoiceInput.exe`. The `assets` directory is at `voice/assets/`, not beside this README.

Nothing in CI builds this host, so a change here is verified only by whoever makes it.

This host uses cloud recognition. Local Whisper was removed from the library entirely, so an old `local_whisper` value in a carried-over configuration does nothing.

## Usage

- **Hotkeys**:
  - RAlt pressed to start recording, release to stop recording and send text to active application
  - RAlt + Space: Lock recording
  - Ctrl + F9: Toggle recording

## Configuration

Edit `$env:LOCALAPPDATA\MetasequoiaVoiceInput\config.toml` (create if not exists) to configure the application.

Below is a template:

```toml
# 自动语音识别（ASR）配置
[asr_api]
# API 基础地址
endpoint = "https://api.siliconflow.cn/v1/audio/transcriptions"
# 服务提供商（如：azure、openai、deepgram 等）
provider = "siliconflow"
# API 访问令牌
token = "<YOUR_OWN_TOKEN>"

# 文本润色配置
[polish_api]
# API 基础地址
endpoint = "https://api.siliconflow.cn/v1/chat/completions"
# 服务提供商（如：azure、openai、deepgram 等）
provider = "siliconflow"
# API 访问令牌
token = "<YOUR_OWN_TOKEN>"

# 基础设置
[settings]
# 偏好语言
language = "zh-cn"
# 触发时是否播放提示音
notification_sound = true
# 上屏前是否要先进行文本润色
polish_text = false
# This standalone host uses cloud recognition.
stt_provider = "cloud_siliconflow"
```

The tray icon's right-click menu has **设置** (opens `config.toml` in the editor associated with `.toml`, falling back to Notepad, and writes the defaults first if the file does not exist), **关于** and **退出**. Double-clicking the tray icon toggles recording. Changes to `config.toml` take effect the next time the host starts.

## License

GPL-3.0.
