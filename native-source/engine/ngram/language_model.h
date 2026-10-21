#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

// kenlm 三元语言模型的封装。对标 libime 的 libime/core/languagemodel.h，
// 去掉了 fcitx-utils 依赖（FCITX_D 私有指针宏、stringutils、fs），并把
// "按语言在若干目录里找 .lm" 的 LanguageModelResolver 一并去掉——本仓的模型
// 路径由 RuntimePaths 决定，不读 LIBIME_MODEL_DIRS 环境变量。
//
// 这个头文件刻意不 include 任何 kenlm 头：kenlm 仍在用 C++17 移除的
// std::binary_function，需要 _HAS_AUTO_PTR_ETC 才能编过。把 kenlm 关在
// language_model.cpp 里，engine 的其余部分就不必继承那个宏。
namespace ngram
{

using WordIndex = unsigned int;

// 足以容纳 lm::ngram::State（三元模型下是 17 字节，对齐到 4）。
// language_model.cpp 里有 static_assert 校验尺寸和对齐，改 KENLM_MAX_ORDER
// 会在那里编译期报错，而不是在运行时读出错误的分数。
inline constexpr std::size_t kStateSize = 20 + sizeof(void *);

struct State
{
    alignas(8) unsigned char data[kStateSize] = {};
};

class LanguageModel
{
  public:
    // 载入失败（文件不存在、格式不符、KENLM_MAX_ORDER 与模型阶数不一致）不抛
    // 异常，构造出一个 valid() == false 的对象：输入法在模型缺失时必须仍然
    // 可用，退回到调用方的启发式打分。
    explicit LanguageModel(const std::filesystem::path &model_file);
    ~LanguageModel();
    LanguageModel(const LanguageModel &) = delete;
    LanguageModel &operator=(const LanguageModel &) = delete;

    bool valid() const;
    // 载入失败时的原因，valid() 为真时为空。仅用于日志。
    const std::string &error() const;

    WordIndex index(std::string_view word) const;
    WordIndex unknown() const;
    bool is_unknown(WordIndex idx) const;

    // 句首状态。解码整句时从这里起步，这样模型里的 <s> 二元/三元才起作用。
    const State &begin_state() const;
    // 无上下文状态。给孤立词打分时用。
    const State &null_state() const;

    // log10 概率。未登录词额外吃 unknown_penalty()。
    float score(const State &in, WordIndex idx, State &out) const;
    float score(const State &in, std::string_view word, State &out) const;
    float single_word_score(const State &in, std::string_view word) const;

    void set_unknown_penalty(float penalty);
    float unknown_penalty() const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

// 进程内按路径共享的模型。出货的 sc.lm 有 34 MB，全拼词典和双拼词典各载一份
// 会白白多映射一遍；kenlm 的查询全是只读的（Score 是 const，状态在调用方的
// State 里），共享实例可以被多个线程同时使用。
//
// 返回的引用在进程生命周期内始终有效，载入失败时返回的是一个 valid() == false
// 的实例，调用方照常判空即可。
const LanguageModel &shared_language_model(const std::filesystem::path &model_file);

} // namespace ngram
