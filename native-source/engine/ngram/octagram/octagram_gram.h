#pragma once

// RIME octagram（八股文）.gram 语法模型的读取端（mmap 版）。
//
// 格式与查询语义按 lotem/librime-octagram（BSD-3-Clause）移植：文件是
// 「Metadata + Darts 双数组 Trie 镜像」，键为「上下文尾部字符 + 词首字符」经
// encode() 压缩的字节串，值为 int(ln(共现频次) * 10000)。它不是概率语言模型，
// query() 给出的是搭配强度信号：max(值/10000 + 搭配惩罚)，全部未命中返回
// non_collocation_penalty。词表零耦合——我们词库里没有的词只是拿不到加成，
// 不会报错。双数组实现在同目录 darts.h（rime/librime 同款，BSD）。
//
// 装载方式用 Win32 内存映射而非整文件 ReadFile，RSS 只随命中的页增长。

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "darts.h"

namespace gram
{

// 打分参数。默认值 = 万象现行（wanxiang.schema.yaml）的 6/2/-14/-6/-100/-18，与出厂模板
// installer/default_config/config.default.toml 的 [association] 键一致；宿主从配置读入后
// 覆盖这里，这个默认只在没有配置的评测工具/单元测试里生效。
// octagram 自身默认是 collocation_max_length=4、collocation_min_length=3、
// collocation_penalty=non_collocation_penalty=-12，我们不用它。
struct GrammarConfig
{
    int collocation_max_length = 6;
    int collocation_min_length = 2;
    double collocation_penalty = -14.0;
    double non_collocation_penalty = -6.0;
    double weak_collocation_penalty = -100.0;
    double rear_penalty = -18.0;

    bool operator==(const GrammarConfig &other) const
    {
        return collocation_max_length == other.collocation_max_length &&
               collocation_min_length == other.collocation_min_length &&
               collocation_penalty == other.collocation_penalty &&
               non_collocation_penalty == other.non_collocation_penalty &&
               weak_collocation_penalty == other.weak_collocation_penalty && rear_penalty == other.rear_penalty;
    }
};

// 一次 query() 的可观测计数。产品路径传 nullptr 即可，诊断工具用它回答
// 「模型在我们词表上到底有没有咬合」。
struct QueryStats
{
    int context_windows = 0; // 尝试过的上下文后缀数
    int matched_windows = 0; // 有 >=1 个结果的窗口数
    int matches = 0;         // 累计结果条数
    double best_value = 0.0; // 命中的最大 scale_value（未命中保持 0）
};

class GramDb
{
  public:
    using Match = Darts::DoubleArray::result_pair_type;

    GramDb() = default;
    ~GramDb();
    GramDb(const GramDb &) = delete;
    GramDb &operator=(const GramDb &) = delete;

    // gram_encoding.cc / gram_db.cc 的常量，公开给实现文件与诊断工具。
    static constexpr int kMaxEncodedUnicode = 8;
    static constexpr int kMaxResults = 8;
    static constexpr double kValueScale = 10000.0;

    // mmap 只读打开，校验 "Rime::Grammar/" 魔数与双数组镜像边界；失败返回
    // false，error() 给出原因。映射与句柄随对象存活——共享实例进程内常驻，
    // 与 kenlm 的共享模型同一条生命线。db_checksum 沿袭 octagram 的 Load()
    // 不校验：魔数加边界检查已足够。文件来源与字节摘要由下载器记录，见
    // server/src/settings/collocation_model.cpp。
    bool open(const std::filesystem::path &file);

    bool valid() const
    {
        return view_ != nullptr;
    }
    const std::string &error() const
    {
        return error_;
    }

    // 复刻 Octagram::Query。context/word 都是原始 UTF-8；is_rear 对应句尾词的
    // 「词+$」rear 项。stats 可为 null。
    double query(const std::string &context, const std::string &word, bool is_rear, const GrammarConfig &config,
                 QueryStats *stats = nullptr) const;

    // gram_encoding.cc 的 UTF-8 压缩编码：ASCII 1 字节、CJK 常用区（0x4000-0xA000）
    // 2 字节、其余按 7bit 组变长，最多编 kMaxEncodedUnicode 个码点。
    static std::string encode(std::string_view utf8);
    static constexpr int max_encoded_codepoints()
    {
        return kMaxEncodedUnicode;
    }

  private:
    // octagram 的 GramDb::Lookup：先 traverse 整段上下文，再从落点做
    // commonPrefixSearch。context_encoded 必须是 encode() 的产物。
    int lookup(const char *context_encoded, const std::string &word_encoded, Match *results) const;

    void *file_handle_ = nullptr; // HANDLE，按 void* 存避免在头文件拉 Windows.h
    void *mapping_handle_ = nullptr;
    const char *view_ = nullptr;
    size_t view_bytes_ = 0;
    uint32_t array_size_ = 0;
    Darts::DoubleArray trie_;
    std::string error_;
};

// 编码串上的游标推进（gram_encoding.cc 的 advance）：一个编码码点占 1 字节
// （ASCII）、2 字节（CJK 常用区）或 1+首字节低 4 位（扩展区变长）。
const char *next_unicode(const char *p);

// 编码串前 byte_length 字节里的码点数（gram_encoding.cc 的 unicode_length）。
size_t unicode_length(std::string_view encoded, size_t byte_length);

// 原始 UTF-8 串的码点数与首/尾 n 个码点。
size_t codepoint_count(std::string_view utf8);
std::string first_codepoints(std::string_view utf8, size_t n);
std::string last_codepoints(std::string_view utf8, size_t n);

// 进程内按路径共享的模型，照 ngram::shared_language_model 的范式：查询全是
// 只读（mmap 页 + const 方法），多会话可同时持有返回的引用；载入失败返回
// valid() == false 的实例，调用方判空降级。全拼和双拼词典各持一份引用、共享
// 同一份映射，不重复付 390MB 的物理内存。
//
// 引用是 shared_ptr：查询按值持有副本，在飞查询不受缓存淘汰影响。设置侧删除
// 模型包后调 shared_gram_db_evict 摘掉缓存引用，词典在下一次
// set_sentence_association（换路径或留空）时松手，最后一个引用松开才真正
// unmap，被删的 .gram 随之可以物理删除。
std::shared_ptr<const GramDb> shared_gram_db(const std::filesystem::path &file);

// 摘掉指定路径的缓存引用（不强制析构：仍有会话持有时由 shared_ptr 兜底）。
void shared_gram_db_evict(const std::filesystem::path &file);

} // namespace gram
