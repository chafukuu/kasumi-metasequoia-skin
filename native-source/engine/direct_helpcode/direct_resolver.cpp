#include "direct_resolver.h"

#include "../quanpin/quanpin_query.h"
#include "../shuangpin/shuangpin_query.h"

#include <algorithm>
#include <cctype>

namespace direct_helpcode
{
namespace
{
// 缓存条目上限：超过就整体清掉，免得一次长时间输入把内存撑大。
constexpr std::size_t kSpanCacheLimit = 8192;
constexpr std::size_t kResolutionCacheLimit = 256;
constexpr std::size_t kCollocationMemoLimit = 65536;

std::string lowercase(std::string text)
{
    for (char &ch : text)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return text;
}

bool is_single_utf8_char(const std::string &text)
{
    if (text.empty())
        return false;
    const auto lead = static_cast<unsigned char>(text[0]);
    const std::size_t len = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : 4;
    return text.size() == len;
}

bool accepts_char(const HelpcodeUtils::Keymap *keymap, const std::string &hanzi, char first, char second)
{
    if (keymap == nullptr)
        return false;
    const auto found = keymap->find(hanzi);
    if (found == keymap->end())
        return false;
    SyllableHelpcode helpcode;
    helpcode.first = first;
    helpcode.second = second;
    return syllable_helpcode_matches(helpcode, found->second);
}

// 两个相邻音节之间要不要放一个手动分隔符：前一个音节后面挂着辅码、任一个是单键声母、或原串里
// 两者之间本来就隔着别的字符时都要放。其余情况是连续的两键音节，贪心切分自己就切得出来，不放
// 分隔符正是为了让它们和普通双拼走完全相同的路径。
bool needs_delimiter(const SyllableSpelling &previous, const SyllableSpelling &current)
{
    return previous.end != previous.begin + previous.code.size() || previous.kind == SpellingKind::Initial ||
           current.kind == SpellingKind::Initial || current.begin != previous.end;
}
} // namespace

void apply_spellings(QueryRequest &request, const std::string &typed, const std::vector<SyllableSpelling> &spellings,
                     const ShuangpinProfile &profile)
{
    std::string clean;
    std::vector<std::size_t> source_index;
    std::vector<std::pair<std::size_t, std::string>> decorations;
    SyllableHelpcodes helpcodes;
    for (std::size_t i = 0; i < spellings.size(); ++i)
    {
        const auto &spelling = spellings[i];
        if (i > 0 && needs_delimiter(spellings[i - 1], spelling))
        {
            const auto &previous = spellings[i - 1];
            clean.push_back('\'');
            // 和句中辅助码一致：分隔符对应前一个音节辅码段的起点。
            source_index.push_back(previous.begin + previous.code.size());
        }
        for (std::size_t j = 0; j < spelling.code.size(); ++j)
        {
            clean.push_back(spelling.code[j]);
            source_index.push_back(spelling.begin + j);
        }
        const std::size_t code_end = spelling.begin + spelling.code.size();
        if (spelling.end > code_end)
            decorations.emplace_back(i, typed.substr(code_end, spelling.end - code_end));
        if (spelling.has_aux())
        {
            SyllableHelpcode helpcode;
            helpcode.syllable = i;
            helpcode.first = spelling.first;
            helpcode.second = spelling.second;
            helpcodes.push_back(helpcode);
        }
    }
    source_index.push_back(typed.size());

    request.raw_input_with_syllable_helpcodes = typed;
    request.raw_input = clean;
    request.raw_input_with_cases = clean;
    request.syllable_helpcodes = std::move(helpcodes);
    request.enable_mid_sentence_helpcode = true;
    request.enable_shuangpin_helpcode = false;
    request.has_syllable_helpcode_layout = true;
    request.syllable_helpcode_source_index = std::move(source_index);
    request.syllable_helpcode_decorations = std::move(decorations);
    request.valid = !clean.empty();
    if (!request.valid)
        return;
    const std::string raw_segmentation = shuangpin::segment_input(clean, profile);
    request.raw_segmentation = raw_segmentation;
    request.normalized_segmentation = shuangpin::to_quanpin_segmentation(raw_segmentation, profile);
    request.segmentation = request.normalized_segmentation;
    request.normalized_input = shuangpin::normalize_input(clean, profile);
}

Resolver::Resolver(const ShuangpinProfile &profile) : profile_(profile)
{
}

void Resolver::reset_cache()
{
    span_cache_.clear();
    word_memo_.clear();
    collocation_memo_.clear();
    resolution_cache_.clear();
    last_sentence_ = nullptr;
}

void Resolver::build_aux_index(const ResolveContext &context)
{
    aux_index_built_ = true;
    if (!context.single_char_rows || context.keymap == nullptr)
        return;
    // 一次扫完词库的单字行，对应万象词库里为每个字派生的 拼音;辅码 拼写。比按音节逐个查（每个音节一条
    // 4096 行的查询）便宜得多，也不会把第一次碰到某个音节的那一键拖慢。
    for (const auto &[quanpin, hanzi] : context.single_char_rows())
    {
        const auto code = context.keymap->find(hanzi);
        if (code == context.keymap->end() || code->second.empty())
            continue;
        auto &aux = aux_index_[quanpin];
        const char lead = code->second[0];
        if (lead >= 'a' && lead <= 'z')
            aux.first[static_cast<std::size_t>(lead - 'a')] = true;
        if (code->second.size() > 1)
            aux.pairs.insert(code->second.substr(0, 2));
    }
}

const std::vector<quanpin::LatticeLexeme> &Resolver::span_rows(const quanpin::Segments &span, bool constrained,
                                                               const ResolveContext &context)
{
    std::string key = quanpin::join_segments(span);
    if (constrained)
        key.push_back('#');
    auto found = span_cache_.find(key);
    if (found != span_cache_.end())
        return found->second;
    if (span_cache_.size() >= kSpanCacheLimit)
        span_cache_.clear();
    auto rows = context.lookup ? context.lookup(span, constrained) : std::vector<quanpin::LatticeLexeme>{};
    return span_cache_.emplace(std::move(key), std::move(rows)).first->second;
}

bool Resolver::has_aux(const std::string &quanpin, char first, char second, const ResolveContext &context)
{
    if (context.keymap == nullptr || first < 'a' || first > 'z')
        return false;
    if (!aux_index_built_)
        build_aux_index(context);
    auto found = aux_index_.find(quanpin);
    if (found == aux_index_.end())
    {
        // 全量扫描里没有这个读音（没有全量扫描，或词库键的写法不同，如 ü）：退回按音节查一次，结果同样
        // 记下，之后不再查。
        SyllableAux aux;
        for (const auto &row : span_rows({quanpin}, true, context))
        {
            if (!is_single_utf8_char(row.value))
                continue;
            const auto code = context.keymap->find(row.value);
            if (code == context.keymap->end() || code->second.empty())
                continue;
            const char lead = code->second[0];
            if (lead >= 'a' && lead <= 'z')
                aux.first[static_cast<std::size_t>(lead - 'a')] = true;
            if (code->second.size() > 1)
                aux.pairs.insert(code->second.substr(0, 2));
        }
        found = aux_index_.emplace(quanpin, std::move(aux)).first;
    }
    if (second == 0)
        return found->second.first[static_cast<std::size_t>(first - 'a')];
    return found->second.pairs.count(std::string{first, second}) > 0;
}

bool Resolver::resolve(QueryRequest &request, const ResolveContext &context)
{
    const std::string typed = request.raw_input_with_cases.empty() ? request.raw_input : request.raw_input_with_cases;
    if (typed.empty())
        return false;
    last_sentence_ = nullptr;
    if (context.keymap != indexed_keymap_)
    {
        reset_cache();
        aux_index_.clear();
        aux_index_built_ = false;
        indexed_keymap_ = context.keymap;
    }
    SpellingOptions spelling_options;
    spelling_options.slash_marker = request.direct_helpcode_slash_marker;
    spelling_options.uppercase_marker = request.direct_helpcode_uppercase_marker;
    if (spelling_options != spelling_options_)
    {
        // 同一个原串换了四码标记就是另一张拼写图，按原串记的切分作废。
        resolution_cache_.clear();
        spelling_options_ = spelling_options;
    }

    auto cached = resolution_cache_.find(typed);
    if (cached == resolution_cache_.end())
    {
        const auto graph = build_spelling_graph(
            typed, profile_,
            [&](const std::string &quanpin, char first, char second) {
                return has_aux(quanpin, first, second, context);
            },
            spelling_options_);
        Resolution resolution;
        // 只有一条完整切分时没有可比的切法，跳过整句解码；整句留给词典层自己的词格去解。
        if (!graph.empty() && !single_path(graph, resolution.spellings))
        {
            const SpanLookup lookup = [&](const quanpin::Segments &span, bool constrained) {
                return span_rows(span, constrained, context);
            };
            const CharAccept accept = [&](const std::string &hanzi, char first, char second) {
                return accepts_char(context.keymap, hanzi, first, second);
            };
            if (word_memo_.size() >= kSpanCacheLimit)
                word_memo_.clear();
            // 搭配分只取决于（尾窗、词、是否句尾），是纯函数：记下来跨假设、跨按键复用，结果与不缓存完全一致。
            // 解析器一次要比较所有切法，边数远多于单一切分的词格，不缓存时 .gram 查询是解码的大头。
            DecodeOptions options = context.options;
            if (options.collocation_scorer && options.collocation_weight > 0.0)
            {
                options.collocation_scorer = [this, &context](std::string_view tail, std::string_view word,
                                                              bool is_rear) {
                    // 复用同一个键缓冲：命中时不分配，只有未命中插入时才拷一份。
                    std::string &key = collocation_key_;
                    key.clear();
                    key.append(tail).push_back('\x1f');
                    key.append(word).push_back(is_rear ? '1' : '0');
                    auto found = collocation_memo_.find(key);
                    if (found != collocation_memo_.end())
                        return found->second;
                    if (collocation_memo_.size() >= kCollocationMemoLimit)
                        collocation_memo_.clear();
                    const double score = context.options.collocation_scorer(tail, word, is_rear);
                    collocation_memo_.emplace(key, score);
                    return score;
                };
            }
            if (auto path = decode_best_path(graph, lookup, accept, options, &word_memo_))
            {
                resolution.spellings = path->syllables;
                if (path->complete && !path->sentence.empty())
                    resolution.sentence = std::move(path);
            }
        }
        if (resolution_cache_.size() >= kResolutionCacheLimit)
            resolution_cache_.clear();
        cached = resolution_cache_.emplace(typed, std::move(resolution)).first;
    }
    const auto &spellings = cached->second.spellings;
    if (spellings.empty())
        return false;
    if (cached->second.sentence)
        last_sentence_ = &*cached->second.sentence;

    // 没有辅码、也没有吃掉分隔符以外的字符时，选中的切分就是普通双拼的切分：请求原样放行，
    // 不带辅码的输入和关着开关时走完全一样的路。
    const bool plain = std::none_of(spellings.begin(), spellings.end(), [](const SyllableSpelling &spelling) {
        return spelling.end != spelling.begin + spelling.code.size();
    });
    if (plain)
    {
        std::string joined;
        for (std::size_t i = 0; i < spellings.size(); ++i)
        {
            if (i > 0)
                joined.push_back('\'');
            joined += spellings[i].code;
        }
        if (joined == shuangpin::segment_input(lowercase(typed), profile_))
            return false;
    }

    apply_spellings(request, typed, spellings, profile_);
    return true;
}

} // namespace direct_helpcode
