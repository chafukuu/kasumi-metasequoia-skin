#include "octagram_gram.h"

#include <Windows.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace gram
{
namespace
{

uint32_t read_u32(const char *p)
{
    const auto *b = reinterpret_cast<const unsigned char *>(p);
    return static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) | (static_cast<uint32_t>(b[2]) << 16) |
           (static_cast<uint32_t>(b[3]) << 24);
}

// 读一个原始 UTF-8 码点并推进 i。续字节缺位或非法序列按 Latin-1 单字节兜底：
// 搭配查询遇到乱码最多损失一次命中，不值得为它抛错。
uint32_t next_codepoint(std::string_view text, size_t &i)
{
    const auto lead = static_cast<unsigned char>(text[i++]);
    // 续字节：只在确实存在且形态合法时消费，值按需拼接。
    const auto take = [&text, &i]() -> uint32_t {
        if (i < text.size() && (static_cast<unsigned char>(text[i]) & 0xC0) == 0x80)
            return static_cast<unsigned char>(text[i++]) & 0x3F;
        return 0;
    };
    if (lead < 0x80)
        return lead;
    if ((lead & 0xE0) == 0xC0)
        return (static_cast<uint32_t>(lead & 0x1F) << 6) | take();
    if ((lead & 0xF0) == 0xE0)
    {
        const uint32_t high = take();
        const uint32_t low = take(); // 两次调用必须先后求值，不能写进同一个表达式
        return (static_cast<uint32_t>(lead & 0x0F) << 12) | (high << 6) | low;
    }
    if ((lead & 0xF8) == 0xF0)
    {
        const uint32_t b1 = take();
        const uint32_t b2 = take();
        const uint32_t b3 = take();
        return (static_cast<uint32_t>(lead & 0x07) << 18) | (b1 << 12) | (b2 << 6) | b3;
    }
    return lead;
}

inline double scale_value(int value)
{
    return value >= 0 ? static_cast<double>(value) / GramDb::kValueScale : -1.0;
}

} // namespace

// ---- gram_encoding.cc 的移植 ------------------------------------------------

std::string GramDb::encode(std::string_view utf8)
{
    std::string out;
    out.reserve(utf8.size());
    size_t i = 0;
    while (i < utf8.size())
    {
        uint32_t u = next_codepoint(utf8, i);
        if (u < 0x80)
        {
            // 编码串里不允许出现 NUL（要当 C 字符串用），0 被转成 0xE0 占位。
            out.push_back(u == 0 ? static_cast<char>(0xE0) : static_cast<char>(u));
        }
        else if (u >= 0x4000 && u < 0xA000)
        {
            // CJK 常用区压成 2 字节：首字节平移进 0x4080-0xA07F，避开 ASCII 与
            // 0xE0 变长前缀。
            if ((u & 0xFF) == 0)
            {
                out.push_back(static_cast<char>(0xE1));
                out.push_back(static_cast<char>((u >> 8) + 0x40));
            }
            else
            {
                out.push_back(static_cast<char>((u >> 8) + 0x40));
                out.push_back(static_cast<char>(u & 0xFF));
            }
        }
        else
        {
            int bits = 32;
            while (bits > 0 && (u & 0xFE000000) == 0)
            {
                bits -= 7;
                u <<= 7;
            }
            int bytes_to_encode = (bits + 6) / 7;
            out.push_back(static_cast<char>(0xE0 | bytes_to_encode));
            while (bytes_to_encode > 0)
            {
                --bytes_to_encode;
                out.push_back(static_cast<char>(((u >> 25) & 0x7F) | 0x80));
                u <<= 7;
            }
        }
    }
    return out;
}

const char *next_unicode(const char *p)
{
    const auto first = static_cast<unsigned char>(*p);
    return p + ((first & 0x80) == 0 ? 1 : (first & 0xF0) == 0xE0 ? (first & 0x0F) + 1 : 2);
}

size_t unicode_length(std::string_view encoded, size_t byte_length)
{
    size_t len = 0;
    size_t i = 0;
    while (i < byte_length && i < encoded.size())
    {
        i = static_cast<size_t>(next_unicode(encoded.data() + i) - encoded.data());
        ++len;
    }
    return len;
}

size_t codepoint_count(std::string_view utf8)
{
    size_t n = 0;
    for (size_t i = 0; i < utf8.size();)
    {
        next_codepoint(utf8, i);
        ++n;
    }
    return n;
}

std::string first_codepoints(std::string_view utf8, size_t n)
{
    size_t seen = 0;
    size_t i = 0;
    while (i < utf8.size() && seen < n)
    {
        next_codepoint(utf8, i);
        ++seen;
    }
    return std::string(utf8.substr(0, i));
}

std::string last_codepoints(std::string_view utf8, size_t n)
{
    const size_t total = codepoint_count(utf8);
    if (total <= n)
        return std::string(utf8);
    size_t skip = total - n;
    size_t i = 0;
    while (i < utf8.size() && skip > 0)
    {
        next_codepoint(utf8, i);
        --skip;
    }
    return std::string(utf8.substr(i));
}

// ---- gram_db.cc 的移植（装载换成 Win32 mmap）--------------------------------

GramDb::~GramDb()
{
    if (view_ != nullptr)
        UnmapViewOfFile(view_);
    if (mapping_handle_ != nullptr)
        CloseHandle(mapping_handle_);
    if (file_handle_ != nullptr)
        CloseHandle(file_handle_);
}

bool GramDb::open(const std::filesystem::path &file)
{
    error_.clear();

    // std::filesystem::path 在 Windows 上是宽字符，直接喂 Create*W，不经窄化。
    file_handle_ = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file_handle_ == INVALID_HANDLE_VALUE)
    {
        // 归一成 void* 空值：INVALID_HANDLE_VALUE 不是 nullptr，但语义上就是没打开。
        CloseHandle(file_handle_);
        file_handle_ = nullptr;
        error_ = "cannot open file";
        return false;
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file_handle_, &size) || size.QuadPart <= 0)
    {
        error_ = "empty file";
        return false;
    }
    view_bytes_ = static_cast<size_t>(size.QuadPart);

    mapping_handle_ = CreateFileMappingW(file_handle_, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (mapping_handle_ == nullptr)
    {
        error_ = "cannot create mapping";
        return false;
    }
    view_ = static_cast<const char *>(MapViewOfFile(mapping_handle_, FILE_MAP_READ, 0, 0, 0));
    if (view_ == nullptr)
    {
        error_ = "cannot map view";
        return false;
    }

    // 头部布局（rime MappedFile 上的 Metadata，偏移 0）：format char[32]、
    // uint32 checksum、uint32 double_array_size（单位数，非字节）、
    // int32 OffsetPtr<char>（相对自身字段的偏移）。
    constexpr size_t kFormatOffset = 0;
    constexpr size_t kArraySizeOffset = 36;
    constexpr size_t kOffsetPtrOffset = 40;
    constexpr size_t kHeaderBytes = 44;
    if (view_bytes_ < kHeaderBytes)
    {
        error_ = "file too small for Metadata";
        return false;
    }
    const std::string_view format(view_ + kFormatOffset, 14);
    if (format != "Rime::Grammar/")
    {
        error_ = "format magic mismatch: " + std::string(format);
        return false;
    }
    array_size_ = read_u32(view_ + kArraySizeOffset);
    const int32_t offset = static_cast<int32_t>(read_u32(view_ + kOffsetPtrOffset));
    const char *self = view_ + kOffsetPtrOffset;
    const char *array = self + offset;
    // darts 单元 4 字节；镜像必须完整落在映射内。
    if (offset < 0 || array < view_ || array_size_ > (view_bytes_ - static_cast<size_t>(array - view_)) / 4)
    {
        error_ = "double-array image out of bounds";
        return false;
    }
    trie_.set_array(array, array_size_);
    return true;
}

int GramDb::lookup(const char *context_encoded, const std::string &word_encoded, Match *results) const
{
    size_t node_pos = 0;
    size_t key_pos = 0;
    trie_.traverse(context_encoded, node_pos, key_pos);
    if (key_pos == std::strlen(context_encoded))
        return static_cast<int>(trie_.commonPrefixSearch(word_encoded.c_str(), results, kMaxResults, 0, node_pos));
    return 0;
}

double GramDb::query(const std::string &context, const std::string &word, bool is_rear, const GrammarConfig &config,
                     QueryStats *stats) const
{
    double result = config.non_collocation_penalty;
    QueryStats local{};
    QueryStats &s = stats != nullptr ? *stats : local;
    if (!valid() || context.empty())
        return result;

    // 与 octagram.cc 一致：上下文取尾部 (collocation_max_length - 1) 个码点，
    // 词取头部同样数量，再各自 encode。collocation_len = 上下文现存码点数 +
    // 命中键的码点数，决定吃 collocation 惩罚还是更狠的 weak 惩罚。
    const int n = (std::min)(kMaxEncodedUnicode, config.collocation_max_length - 1);
    if (n < 1)
        return result;

    const std::string context_tail = last_codepoints(context, static_cast<size_t>(n));
    int remaining_windows = codepoint_count(context_tail);
    const std::string context_query = encode(context_tail);
    const std::string word_head = first_codepoints(word, static_cast<size_t>(n));
    const int word_head_len = static_cast<int>(codepoint_count(word_head));
    const std::string word_query = encode(word_head);

    const char *context_ptr = context_query.c_str();
    for (; remaining_windows > 0; --remaining_windows, context_ptr = next_unicode(context_ptr))
    {
        ++s.context_windows;
        Match matches[kMaxResults];
        const int num = lookup(context_ptr, word_query, matches);
        if (num <= 0)
            continue;
        ++s.matched_windows;
        const bool at_window_start = context_ptr == context_query.c_str();
        for (int i = 0; i < num; ++i)
        {
            ++s.matches;
            const Match &match = matches[i];
            const double scaled = scale_value(match.value);
            if (scaled > s.best_value)
                s.best_value = scaled;
            const int match_len = static_cast<int>(unicode_length(word_query, match.length));
            const int collocation_len = remaining_windows + match_len;
            const bool whole_query = at_window_start && match.length == word_query.size();
            const double penalty = (collocation_len >= config.collocation_min_length || whole_query)
                                       ? config.collocation_penalty
                                       : config.weak_collocation_penalty;
            const double candidate = scaled + penalty;
            if (candidate > result)
                result = candidate;
        }
    }

    if (is_rear)
    {
        // 句尾标记只在「词未被头部截断」时查：截过的词查 $ 无意义。
        if (word_head_len == static_cast<int>(codepoint_count(word)))
        {
            Match matches[kMaxResults];
            if (lookup(word_query.c_str(), "$", matches) > 0)
            {
                const double candidate = scale_value(matches[0].value) + config.rear_penalty;
                if (candidate > result)
                    result = candidate;
            }
        }
    }
    return result;
}

// ---- 进程内共享 --------------------------------------------------------------

namespace
{
// 缓存的锁与存储提升到文件作用域：shared_gram_db_evict 要在同一把锁下摘引用。
std::mutex &shared_cache_mutex()
{
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<std::string, std::shared_ptr<GramDb>> &shared_cache()
{
    static std::unordered_map<std::string, std::shared_ptr<GramDb>> cache;
    return cache;
}
} // namespace

std::shared_ptr<const GramDb> shared_gram_db(const std::filesystem::path &file)
{
    // 照 ngram::shared_language_model：实例按路径缓存、多会话共享。缓存引用可以
    // 被 shared_gram_db_evict 摘掉（设置侧删除模型包后），词典在下一次
    // set_sentence_association 时松手；在飞查询按值持有 shared_ptr 副本，实例的
    // 析构时机由引用计数兜底——没有「永不析构」，也就没有删不掉的文件。
    const std::string key = file.u8string();
    std::lock_guard lock(shared_cache_mutex());
    auto &cache = shared_cache();
    auto &slot = cache[key];
    if (!slot)
    {
        auto db = std::make_shared<GramDb>();
        db->open(file);
        slot = std::move(db);
    }
    return slot;
}

void shared_gram_db_evict(const std::filesystem::path &file)
{
    const std::string key = file.u8string();
    std::lock_guard lock(shared_cache_mutex());
    shared_cache().erase(key);
}

} // namespace gram
