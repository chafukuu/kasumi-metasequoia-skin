#include "pinyin_decoder.h"
#include "data_path.h"
#include "../googlepinyinime-rev/src/include/pinyinime.h"
#include "../googlepinyinime-rev/src/include/lpicache.h"
#include <mutex>
#include <utf8.h>

namespace metasequoia
{
namespace
{
struct DecoderService
{
    std::mutex mutex;
    std::filesystem::path model;
    std::filesystem::path user_dictionary;
    bool ready = false;
    std::size_t clients = 0;
    ~DecoderService()
    {
        ime_pinyin::im_close_decoder();
    }
};

DecoderService &service()
{
    static DecoderService instance;
    return instance;
}

bool accept_constrained_char(std::size_t index, ime_pinyin::char16 hanzi, void *user)
{
    const auto &constraints = *static_cast<const std::vector<DecoderCharConstraint> *>(user);
    if (index >= constraints.size() || !constraints[index].accept)
        return true;
    const std::u16string text(1, static_cast<char16_t>(hanzi));
    return constraints[index].accept(utf8::utf16to8(text));
}
} // namespace

PinyinDecoder::PinyinDecoder(std::filesystem::path model, std::filesystem::path user_dictionary)
    : model_(std::move(model)), user_dictionary_(std::move(user_dictionary))
{
    auto &decoder = service();
    std::lock_guard lock(decoder.mutex);
    ++decoder.clients;
}

PinyinDecoder::~PinyinDecoder()
{
    auto &decoder = service();
    std::lock_guard lock(decoder.mutex);
    if (--decoder.clients == 0)
    {
        ime_pinyin::im_close_decoder();
        decoder.ready = false;
        decoder.model.clear();
        decoder.user_dictionary.clear();
    }
}

std::string PinyinDecoder::sentence(const std::string &pinyin,
                                    const std::vector<DecoderCharConstraint> &constraints) const
{
    if (pinyin.empty() || pinyin.size() > 128 || model_.empty() || user_dictionary_.empty())
        return {};
    auto &decoder = service();
    std::lock_guard lock(decoder.mutex);
    if (decoder.model != model_ || decoder.user_dictionary != user_dictionary_)
    {
        ime_pinyin::im_close_decoder();
        decoder.ready = false;
        // The upstream lemma cache is also global and contains model-specific IDs.
        auto &cache = ime_pinyin::LpiCache::get_instance();
        for (ime_pinyin::uint16 id = 0; id < ime_pinyin::kFullSplIdStart; ++id)
            cache.put_cache(id, nullptr, 0);
        decoder.model = model_;
        decoder.user_dictionary = user_dictionary_;
        decoder.ready =
            ime_pinyin::im_open_decoder(path_to_utf8(model_).c_str(), path_to_utf8(user_dictionary_).c_str());
        if (!decoder.ready)
            return {};
        ime_pinyin::im_set_max_lens(128, 64);
    }
    if (!decoder.ready)
        return {};
    std::vector<ime_pinyin::uint16> positions;
    positions.reserve(constraints.size());
    for (const auto &constraint : constraints)
    {
        if (constraint.pinyin_offset >= pinyin.size())
            return {};
        positions.push_back(static_cast<ime_pinyin::uint16>(constraint.pinyin_offset));
    }
    // 约束变化时解码器会整段重解，增量搜索只在前后都不带约束时继续生效。
    ime_pinyin::im_set_char_constraints(positions.data(), positions.size(), accept_constrained_char,
                                        const_cast<std::vector<DecoderCharConstraint> *>(&constraints));
    // 不先 im_reset_search：upstream 的 search 自己会与上一次的输入比公共前缀，从分歧处
    // 回退再往后解，连续打字时每键只多解一个字母。前缀为零时它走的就是整段重置。
    const auto count = ime_pinyin::im_search(pinyin.data(), pinyin.size());
    std::string result;
    for (std::size_t i = 0; i < count && result.empty(); ++i)
    {
        ime_pinyin::char16 buffer[256] = {};
        if (!ime_pinyin::im_get_candidate(i, buffer, 255))
            continue;
        std::size_t length = 0;
        while (length < 255 && buffer[length] != 0)
            ++length;
        if (length)
            result = utf8::utf16to8(std::u16string(reinterpret_cast<const char16_t *>(buffer), length));
    }
    // 回调的 user 指针指向调用方的 constraints，出了这个函数就悬空，不能留在解码器里。
    if (!constraints.empty())
        ime_pinyin::im_set_char_constraints(nullptr, 0, nullptr, nullptr);
    return result;
}
} // namespace metasequoia
