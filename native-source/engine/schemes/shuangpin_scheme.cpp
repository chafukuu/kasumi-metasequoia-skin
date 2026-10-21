#include "shuangpin_scheme.h"
#include "../shuangpin/shuangpin_query.h"
#include <cctype>

namespace
{
bool is_alpha_vk(ImeKeyCode vk)
{
    return vk >= 'A' && vk <= 'Z';
}

bool is_microsoft_ing_key(ImeKeyCode vk, ImeCharacter wch, const std::string &raw_input,
                          const ShuangpinProfile &profile, shuangpin::HelpcodeUppercaseTriggers triggers)
{
    if (!ShuangpinProfileUsesSemicolonFinal(profile) || vk != ImeKey::Semicolon || wch != u';')
    {
        return false;
    }
    // 大写触发开着时大写段不算这一节的键，规则与 TSF、Server 共用。
    return FanyImeMidSentenceHelpcode::AcceptsSemicolonFinalAt(raw_input.data(), raw_input.size(), raw_input.size(),
                                                               triggers);
}
} // namespace

ShuangpinScheme::ShuangpinScheme(const ShuangpinProfile &profile) : profile_(profile)
{
}

void ShuangpinScheme::reset()
{
    raw_input_.clear();
    key_strokes_.clear();
}

void ShuangpinScheme::set_raw_input(const std::string &raw_input, const std::string &raw_input_with_cases)
{
    raw_input_ = raw_input_with_cases.empty() ? raw_input : raw_input_with_cases;
    key_strokes_.clear();
}

void ShuangpinScheme::handle_key(ImeKeyCode vk, ImeModifierMask modifiers_down, ImeCharacter wch)
{
    if (vk == ImeKey::Backspace)
    {
        if (!raw_input_.empty())
        {
            raw_input_.pop_back();
        }
        if (!key_strokes_.empty())
        {
            key_strokes_.pop_back();
        }
        return;
    }

    if (vk == ImeKey::Escape || vk == ImeKey::Return)
    {
        reset();
        return;
    }

    if (vk == ImeKey::Apostrophe)
    {
        if (raw_input_.empty() || raw_input_.back() != '\'')
        {
            key_strokes_.push_back(KeyStroke{vk, modifiers_down, wch});
            raw_input_.push_back('\'');
        }
        return;
    }

    // 句中辅助码的反引号。开关与「前面是不是一节完整的偶数键」由调用方判断，这里只负责记下。
    if (vk == ImeKey::Backquote && wch == u'`')
    {
        key_strokes_.push_back(KeyStroke{vk, modifiers_down, wch});
        raw_input_.push_back(shuangpin::kMidSentenceHelpcodeMarker);
        return;
    }

    // 末尾辅助码（间接）的段标记，宿主把触发键（反引号或分号）换成它再交过来；收不收同样由调用方判断。
    if (wch == static_cast<ImeCharacter>(shuangpin::kEndHelpcodeMarker))
    {
        key_strokes_.push_back(KeyStroke{vk, modifiers_down, wch});
        raw_input_.push_back(shuangpin::kEndHelpcodeMarker);
        return;
    }

    if (direct_helpcode_ && wch == u'/')
    {
        key_strokes_.push_back(KeyStroke{vk, modifiers_down, wch});
        raw_input_.push_back('/');
        return;
    }

    const bool microsoft_ing_key =
        direct_helpcode_ ? ShuangpinProfileUsesSemicolonFinal(profile_) && wch == u';' && !raw_input_.empty() &&
                               std::isalpha(static_cast<unsigned char>(raw_input_.back()))
                         : is_microsoft_ing_key(vk, wch, raw_input_, profile_,
                                                {mid_sentence_uppercase_trigger_, end_helpcode_uppercase_trigger_});
    if (!is_alpha_vk(vk) && !microsoft_ing_key)
    {
        return;
    }

    key_strokes_.push_back(KeyStroke{vk, modifiers_down, wch});
    if (wch >= L'A' && wch <= L'Z')
    {
        raw_input_.push_back(static_cast<char>(wch));
    }
    else if (wch >= L'a' && wch <= L'z')
    {
        raw_input_.push_back(static_cast<char>(wch));
    }
    else if (microsoft_ing_key)
    {
        raw_input_.push_back(';');
    }
    else
    {
        raw_input_.push_back(static_cast<char>(vk + ('a' - 'A')));
    }
}

QueryRequest ShuangpinScheme::build_request() const
{
    QueryRequest request;
    request.scheme = type();
    request.raw_input_with_cases = raw_input_;
    const shuangpin::HelpcodeUppercaseTriggers triggers(mid_sentence_uppercase_trigger_,
                                                        end_helpcode_uppercase_trigger_);
    if (shuangpin::has_mid_sentence_helpcode(raw_input_, triggers))
    {
        // 下游只认 ' 分隔的双拼：每段（反引号段或大写段）换成分隔符，约束单独带着，原串留给宿主回读。
        // 段的位置随请求带走，下游推进与显示不必再按当时的触发规则反推。末尾的末尾段直接去掉，辅码
        // 另外带着。
        auto parsed = shuangpin::parse_mid_sentence_helpcodes(raw_input_, profile_, triggers);
        request.raw_input_with_cases = std::move(parsed.input);
        request.raw_input_with_syllable_helpcodes = raw_input_;
        request.syllable_helpcodes = std::move(parsed.helpcodes);
        request.has_syllable_helpcode_layout = true;
        request.syllable_helpcode_source_index = std::move(parsed.source_index);
        request.syllable_helpcode_decorations = std::move(parsed.decorations);
        request.end_helpcodes = std::move(parsed.end_helpcodes);
    }
    request.raw_input.reserve(request.raw_input_with_cases.size());
    for (const char ch : request.raw_input_with_cases)
    {
        request.raw_input.push_back(ch == '\'' ? ch : static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    request.key_strokes = key_strokes_;
    request.valid = shuangpin::effective_input_length(request.raw_input) > 0;

    if (!request.valid)
    {
        return request;
    }

    const std::string raw_segmentation = shuangpin::segment_input(request.raw_input, profile_);
    request.raw_segmentation = shuangpin::apply_segmentation_cases(raw_segmentation, request.raw_input_with_cases);
    request.normalized_segmentation = shuangpin::to_quanpin_segmentation(raw_segmentation, profile_);
    request.segmentation = request.normalized_segmentation;
    request.normalized_input = shuangpin::normalize_input(request.raw_input, profile_);
    return request;
}

std::string ShuangpinScheme::get_preedit() const
{
    return raw_input_;
}

SchemeType ShuangpinScheme::type() const
{
    return SchemeType::Shuangpin;
}
