#pragma once
#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace metasequoia
{
// 句中辅助码对 Google 解码的约束：拼音串里从 pinyin_offset 起的那个音节，只能解成
// accept 认可的汉字（UTF-8 单字）。约束在解码器内部建格子时生效，不是解完再筛。
struct DecoderCharConstraint
{
    std::size_t pinyin_offset = 0;
    std::function<bool(const std::string &hanzi)> accept;
};

// Google Pinyin exposes process-wide singletons. This adapter owns their lifecycle and
// executes a complete, reset-before-search operation under one lock. No caller can retain
// candidate pointers or incremental search state between requests.
class PinyinDecoder
{
  public:
    PinyinDecoder(std::filesystem::path model, std::filesystem::path user_dictionary);
    ~PinyinDecoder();
    PinyinDecoder(const PinyinDecoder &) = delete;
    PinyinDecoder &operator=(const PinyinDecoder &) = delete;
    // 约束为空就是普通整句解码。解码器是全进程共享的，每次调用都会重设约束，
    // 上一个调用方留下的约束不会漏到这一次。
    std::string sentence(const std::string &pinyin, const std::vector<DecoderCharConstraint> &constraints = {}) const;

  private:
    std::filesystem::path model_;
    std::filesystem::path user_dictionary_;
};
} // namespace metasequoia
