cmake_minimum_required(VERSION 3.25.0)

project(MetasequoiaImeEngineTest VERSION 1.0 LANGUAGES CXX)

# Specify the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 直接链接 engine/CMakeLists.txt 的正式引擎库，不再手抄一份源文件清单。
# 手抄的清单曾经漏掉新增的 local_modes/local_database.cpp，imetest 因此链接
# 失败；也没有正式构建给 googlepinyinime 加的 /FI pinyin_file_io.h 和 /utf-8，
# 测的不是出货的那份引擎。依赖、编译选项和 NOMINMAX/UNICODE 等公开定义都随
# MetasequoiaIme::Engine 传过来。关掉 BUILD_TESTING，免得把引擎自己的 ctest
# 目标也一并构建进来。
set(BUILD_TESTING OFF)
add_subdirectory(".." "engine")

# imetest 的 main 在 test_pinyin.cpp。
add_executable(imetest "./src/test_pinyin.cpp")
target_compile_options(imetest PRIVATE /Zc:__cplusplus)
target_link_libraries(imetest PRIVATE MetasequoiaIme::Engine)

# 全拼纠错评测独立二进制（任务 .trellis/tasks/09-12-quanpin-autocorrect-patent
# 阶段 0 的基线采集载体）：采样真实 msime.db 注错测召回，只挂自己的 main，
# 与 imetest 的测试装置互不依赖。
add_executable(eval_quanpin_autocorrect "./src/eval_quanpin_autocorrect.cpp")
target_compile_options(eval_quanpin_autocorrect PRIVATE /Zc:__cplusplus)
target_link_libraries(eval_quanpin_autocorrect PRIVATE MetasequoiaIme::Engine)
