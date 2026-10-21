# 输入引擎（engine/）

[English README](README.en.md)

水杉输入法 Windows 版的输入引擎：输入会话、候选查询、辅助码、共享契约和语音模块。

这个目录曾经是 `metasequoiaime/MSIME-Engine` 的子模块，现在是本仓库的一个组件，和 `server/`、`ui/`、`windows/`、`installer/` 平级。来源提交、以及专门化时裁掉了什么，见 [UPSTREAM.md](UPSTREAM.md)。**请把它当本仓库的一等代码来改**：和别处一样评审、格式化、测试，不再从上游合并。

| 目录 | 职责 |
|---|---|
| `core/`、`quanpin/`、`shuangpin/`、`english/`、`japanese/` | 输入会话和候选查询 |
| `contracts/` | 与 server、前端页面共享的协议、词库格式和标点策略 |
| `helpcode/` | 辅助码数据和生成脚本 |
| `local_modes/`、`providers/`、`user_dictionary/` | 本地模式、在线候选来源、用户词库 |
| `voice/` | 录音、识别、文本处理；可选构建 |
| `googlepinyinime-rev/`、`ngram/kenlm/`、`utfcpp/`、`voice/third_party/` | 第三方内嵌代码，保留上游格式与许可 |

接入方式是 `<metasequoia/session.h>`；会话隔离、资源目录和兼容接口见 [运行时架构](docs/runtime-architecture.md)。资源清单与校验的权威是 [assets 契约](contracts/assets/README.md)。

## 构建

产品构建不单独构建这个目录。`server/CMakeLists.txt` 用 `add_subdirectory` 把它接进来，所以 CI 是通过 server 和 TSF 前端来编译和验证引擎的——改了这里，跑 server 的构建与测试即可。

单独构建仍然可用，改引擎时比整个 server 快得多。依赖：CMake 3.25+、支持 C++17 的编译器、Boost、fmt、spdlog、SQLite3。

```powershell
vcpkg install --triplet x64-windows-static
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows-static
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure --timeout 20
```

`ctest` 会跑根 `CMakeLists.txt` 登记的全部引擎测试目标（分词与 IPC 契约、标点、平台 API、数据路径、运行时隔离、词库状态、各输入模式的会话测试等）。

引擎源码由仓库根的 `scripts/format.sh` 统一格式化，配置是本目录的 `.clang-format`（`voice/` 另有一份放宽列宽的）。内嵌的第三方目录和生成的头文件不在格式化范围内。

## tests/ 下那套独立工程

`tests/CMakeLists.txt` 是一个**独立的 Windows-only 工程**，产出 `imetest`，源文件是 `tests/src/test_pinyin.cpp`。它**不被根 `CMakeLists.txt` 引入**，所以 CI 不构建也不运行它——往 `test_pinyin.cpp` 加测试不会被自动跑到。

它要求本机装有 Boost，并使用 MSVC 专有编译选项。Boost 的位置按 `-DBoost_ROOT=...` → 环境变量 `Boost_ROOT` / `BOOST_ROOT` → scoop 默认布局（`%USERPROFILE%\scoop\apps\boost\current`）的顺序解析，vcpkg 走 `VCPKG_ROOT`，都不需要改文件。

```powershell
python .\tests\scripts\prepare_env.py  # 只生成 .clangd，供编辑器补全用；构建本身不需要它
cd .\tests\
.\scripts\llaunch.ps1
```

新增测试请优先加到根 `CMakeLists.txt` 已经登记的那些目标里。

## 数据

引擎读取的词库**不在本目录内**，也不由本仓库构建：`msime.db`（全拼、五笔、快捷短语、日语词表）、`english.db`（英文候选与中英释义）、`others.db`（emoji、颜文字、符号）、`dict_japanese.dat`（日语整句模型）都来自 `product-lock.json` 钉住的词库 Release，构建时下载。建库流水线随专门化一并移除，见 [UPSTREAM.md](UPSTREAM.md)。

候选窗的中英翻译除了 `english.db` 的大表之外，还有一层人工覆盖：`english.db` 同目录下的 `custom_translations.txt`，格式为 `源<Tab>释义`，优先级高于大表（见 `english/english_dictionary.cpp`）。要纠正某条释义时改这里，不要动 ECDICT 生成的大表。

词库命名的权威定义是 `contracts/dictionary/format.json`，C++ 查询和仓库根的 `scripts/dictionary_product.py` 共用同一份，禁止在消费端复制规则。

## 语音

[VoiceInput](voice/README.md) 提供 `MetasequoiaIme::Voice` 和 `VoiceCapture`。根构建通过 `METASEQUOIA_IME_BUILD_VOICE=ON` 开启，也可单独构建 `voice/`。平台权限、快捷键、界面和上屏由宿主负责。

识别走云端服务商，本地 Whisper 推理已随专门化移除。唯一内嵌的模型是 `voice/assets/models/silero_vad.onnx`，只有默认关闭的独立 Windows 宿主会加载它；库里受支持的 VAD 是不需要模型的 RMS 实现。

许可与来源逐项见 [NOTICE.md](NOTICE.md)。
