#include "direct_lattice.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string_view>
#include <unordered_map>

namespace direct_helpcode
{
namespace
{
constexpr double kNegInf = -std::numeric_limits<double>::infinity();
// 搭配打分的上下文窗口，与 word_lattice.cpp 一致：.gram 的查询键最多编入 8 个码点。
constexpr std::size_t kCollocationTailCodepoints = 8;

std::vector<std::string> split_utf8_chars(const std::string &text)
{
    std::vector<std::string> chars;
    for (std::size_t i = 0; i < text.size();)
    {
        const auto lead = static_cast<unsigned char>(text[i]);
        const std::size_t len = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : 4;
        chars.push_back(text.substr(i, len));
        i += len;
    }
    return chars;
}

// 以下三个打分函数与 word_lattice.cpp 同义，见那边的注释。
double heuristic_log_prob(std::int64_t weight, std::size_t syllables, const DecodeOptions &options)
{
    const double w = weight > 0 ? static_cast<double>(weight) : 1.0;
    const double z = options.unigram_z > 1.0 ? options.unigram_z : 1e6;
    const double lp = std::log(w);
    if (syllables <= 1)
        return lp - std::log(z);
    return lp + options.phrase_length_bonus * static_cast<double>(syllables);
}

double dictionary_tiebreak(std::int64_t weight, const DecodeOptions &options)
{
    const double w = weight > 0 ? static_cast<double>(weight) : 1.0;
    return options.dictionary_tiebreak * std::log10(w);
}

double reading_prior(std::int64_t weight, std::int64_t span_total, const DecodeOptions &options)
{
    if (options.reading_prior <= 0 || span_total <= 0)
        return 0;
    const double w = weight > 0 ? static_cast<double>(weight) : 0.0;
    const double share = (w + 1.0) / (static_cast<double>(span_total) + 1.0);
    const double threshold = options.reading_prior_share > 0 ? options.reading_prior_share : 1.0;
    if (share >= threshold)
        return 0;
    const double floor = options.reading_prior_floor > 0 ? -options.reading_prior_floor : kNegInf;
    return options.reading_prior * (std::max)(std::log10(share / threshold), floor);
}

const ngram::LanguageModel *active_model(const DecodeOptions &options)
{
    if (options.language_model && options.language_model->valid())
        return options.language_model;
    return nullptr;
}

struct WordEdge
{
    std::size_t end = 0;
    std::string word;
    std::string key;
    double base_score = 0;
    ngram::WordIndex index = 0;
    // 占位边：缩写声母或查不到字的音节，只为让切分走得通，不进语言模型。
    bool placeholder = false;
    std::vector<const SyllableSpelling *> spellings;
};

class WordGraphBuilder
{
  public:
    WordGraphBuilder(const SpellingGraph &graph, const SpanLookup &lookup, const CharAccept &accept,
                     const DecodeOptions &options, WordEdgeMemo *memo)
        : graph_(graph), lookup_(lookup), accept_(accept), options_(options), model_(active_model(options)), memo_(memo)
    {
    }

    std::vector<std::vector<WordEdge>> build()
    {
        std::vector<std::vector<WordEdge>> words(graph_.size);
        for (std::size_t start = 0; start < graph_.size; ++start)
        {
            if (graph_.edges[start].empty())
                continue;
            sequences_ = 0;
            std::vector<const SyllableSpelling *> sequence;
            extend(start, sequence, words[start]);
        }
        return words;
    }

  private:
    void extend(std::size_t pos, std::vector<const SyllableSpelling *> &sequence, std::vector<WordEdge> &out)
    {
        if (pos >= graph_.size)
            return;
        for (const auto &spelling : graph_.edges[pos])
        {
            if (sequences_ >= options_.max_sequences_per_start)
                return;
            if (spelling.kind == SpellingKind::Initial)
            {
                // 声母缩写只单独成边，不拼进多字词：词格查的是完整读音。
                if (sequence.empty())
                    out.push_back(placeholder(spelling));
                continue;
            }
            ++sequences_;
            sequence.push_back(&spelling);
            emit(sequence, out);
            if (static_cast<int>(sequence.size()) < options_.max_phrase_syllables)
                extend(next_spelling_position(graph_, spelling.end), sequence, out);
            sequence.pop_back();
        }
    }

    WordEdge placeholder(const SyllableSpelling &spelling) const
    {
        WordEdge edge;
        edge.end = spelling.end;
        edge.placeholder = true;
        edge.base_score = options_.initial_penalty;
        edge.spellings.push_back(&spelling);
        return edge;
    }

    const std::vector<quanpin::LatticeLexeme> &rows_for(const quanpin::Segments &span, bool constrained)
    {
        std::string key = quanpin::join_segments(span);
        if (constrained)
            key.push_back('#');
        auto found = cache_.find(key);
        if (found == cache_.end())
            found = cache_
                        .emplace(std::move(key),
                                 lookup_ ? lookup_(span, constrained) : std::vector<quanpin::LatticeLexeme>{})
                        .first;
        return found->second;
    }

    // 这条拼写序列铺出来的词只取决于各音节的读音与辅码，跨按键记在 memo 里。
    static std::string memo_key(const std::vector<const SyllableSpelling *> &sequence)
    {
        std::string key;
        for (const auto *spelling : sequence)
        {
            key += spelling->quanpin;
            key.push_back(spelling->first ? spelling->first : '-');
            key.push_back(spelling->second ? spelling->second : '-');
            key.push_back('|');
        }
        return key;
    }

    void emit(const std::vector<const SyllableSpelling *> &sequence, std::vector<WordEdge> &out)
    {
        const std::vector<CachedWord> *words = nullptr;
        std::vector<CachedWord> local;
        if (memo_)
        {
            std::string key = memo_key(sequence);
            auto found = memo_->find(key);
            if (found == memo_->end())
                found = memo_->emplace(std::move(key), lookup_words(sequence)).first;
            words = &found->second;
        }
        else
        {
            local = lookup_words(sequence);
            words = &local;
        }

        if (words->empty())
        {
            // 单个音节查不到字时也留一条占位边，免得整条切分断掉；分数比声母缩写还低。
            if (sequence.size() == 1)
            {
                WordEdge edge = placeholder(*sequence.front());
                edge.base_score = options_.initial_penalty * 2;
                out.push_back(std::move(edge));
            }
            return;
        }
        for (const auto &word : *words)
        {
            WordEdge edge;
            edge.end = sequence.back()->end;
            edge.word = word.word;
            edge.key = word.key;
            edge.base_score = word.base_score;
            edge.index = word.index;
            edge.spellings = sequence;
            out.push_back(std::move(edge));
        }
    }

    std::vector<CachedWord> lookup_words(const std::vector<const SyllableSpelling *> &sequence)
    {
        quanpin::Segments span;
        bool constrained = false;
        for (const auto *spelling : sequence)
        {
            span.push_back(spelling->quanpin);
            constrained = constrained || spelling->has_aux();
        }
        const auto &rows = rows_for(span, constrained);

        std::vector<const quanpin::LatticeLexeme *> accepted;
        for (const auto &row : rows)
        {
            if (static_cast<int>(accepted.size()) >= options_.span_limit)
                break;
            if (row.value.empty())
                continue;
            const auto chars = split_utf8_chars(row.value);
            if (chars.size() != sequence.size())
                continue;
            bool ok = true;
            for (std::size_t i = 0; ok && i < sequence.size(); ++i)
            {
                if (sequence[i]->has_aux())
                    ok = accept_ && accept_(chars[i], sequence[i]->first, sequence[i]->second);
            }
            if (ok)
                accepted.push_back(&row);
        }

        std::vector<CachedWord> words;
        // 按读音分开求和，模糊读音的行不压低敲出读音的占比，见 quanpin::build_graph。
        std::unordered_map<std::string, std::int64_t> reading_totals;
        for (const auto *row : accepted)
            reading_totals[row->key] += row->weight > 0 ? row->weight : 0;
        const std::string span_key = quanpin::join_segments(span);
        for (const auto *row : accepted)
        {
            CachedWord word;
            word.word = row->value;
            word.key = row->key.empty() ? span_key : row->key;
            if (model_)
            {
                word.index = model_->index(word.word);
                word.base_score = dictionary_tiebreak(row->weight, options_) +
                                  reading_prior(row->weight, reading_totals[row->key], options_);
            }
            else
            {
                word.base_score = heuristic_log_prob(row->weight, sequence.size(), options_);
            }
            word.base_score -= row->penalty;
            words.push_back(std::move(word));
        }
        return words;
    }

    const SpellingGraph &graph_;
    const SpanLookup &lookup_;
    const CharAccept &accept_;
    const DecodeOptions &options_;
    const ngram::LanguageModel *model_;
    WordEdgeMemo *memo_;
    std::unordered_map<std::string, std::vector<quanpin::LatticeLexeme>> cache_;
    int sequences_ = 0;
};

// 搭配打分的尾窗。每条状态转移都要生成一个新尾窗，用 std::string 时每次都是堆分配（拼接一次、截断
// 一次、存进假设一次），是解码的大头。合法 UTF-8 下 8 个码点至多 32 字节，放在定长缓冲里；词库行
// 不保证是合法 UTF-8，真超出时退回 std::string。截断规则与 word_lattice.cpp 的 tail_codepoints 完全一致。
class CollocationTail
{
  public:
    std::string_view view() const
    {
        return overflow_.empty() ? std::string_view(bytes_.data(), size_) : std::string_view(overflow_);
    }

    // 等价于 word_lattice.cpp 的 tail_codepoints(view() + word, kCollocationTailCodepoints)。
    CollocationTail extended(std::string_view word) const
    {
        const std::string_view tail = view();
        const std::size_t total = tail.size() + word.size();
        CollocationTail next;
        if (total <= kScratch)
        {
            std::array<char, kScratch> scratch;
            std::copy(tail.begin(), tail.end(), scratch.begin());
            std::copy(word.begin(), word.end(), scratch.begin() + static_cast<std::ptrdiff_t>(tail.size()));
            next.assign(keep_tail(std::string_view(scratch.data(), total)));
        }
        else
        {
            const std::string joined = std::string(tail) + std::string(word);
            next.assign(keep_tail(joined));
        }
        return next;
    }

  private:
    static constexpr std::size_t kCapacity = 48;
    static constexpr std::size_t kScratch = 160;

    // tail_codepoints 的同一套规则：数非续字节当码点，从头跳过多出来的码点。
    static std::string_view keep_tail(std::string_view text)
    {
        std::size_t total = 0;
        for (unsigned char c : text)
        {
            if ((c & 0xC0) != 0x80)
                ++total;
        }
        if (total <= kCollocationTailCodepoints)
            return text;
        std::size_t skip = total - kCollocationTailCodepoints;
        std::size_t i = 0;
        while (i < text.size() && skip > 0)
        {
            ++i;
            --skip;
            while (i < text.size() && (static_cast<unsigned char>(text[i]) & 0xC0) == 0x80)
                ++i;
        }
        return text.substr(i);
    }

    void assign(std::string_view text)
    {
        if (text.size() <= kCapacity)
        {
            std::copy(text.begin(), text.end(), bytes_.begin());
            size_ = static_cast<std::uint8_t>(text.size());
            overflow_.clear();
        }
        else
        {
            size_ = 0;
            overflow_.assign(text);
        }
    }

    std::array<char, kCapacity> bytes_{};
    std::uint8_t size_ = 0;
    std::string overflow_;
};

struct Hyp
{
    double score = kNegInf;
    int prev_pos = -1;
    int prev_idx = -1;
    // 走到这个假设的那条边；分隔符直通时为空。
    const WordEdge *edge = nullptr;
    ngram::State state;
    CollocationTail collocation_tail;
};

void keep_beam(std::vector<Hyp> &column, int beam)
{
    if (static_cast<int>(column.size()) <= beam)
        return;
    std::partial_sort(column.begin(), column.begin() + beam, column.end(),
                      [](const Hyp &a, const Hyp &b) { return a.score > b.score; });
    column.resize(static_cast<std::size_t>(beam));
}
} // namespace

std::optional<DecodedPath> decode_best_path(const SpellingGraph &graph, const SpanLookup &lookup,
                                            const CharAccept &accept, const DecodeOptions &options, WordEdgeMemo *memo)
{
    if (graph.empty() || graph.size == 0)
        return std::nullopt;

    const std::size_t n = graph.size;
    const ngram::LanguageModel *model = active_model(options);
    const bool collocation = options.collocation_scorer && options.collocation_weight > 0.0;
    const auto words = WordGraphBuilder(graph, lookup, accept, options, memo).build();

    std::vector<std::vector<Hyp>> columns(n + 1);
    Hyp start;
    start.score = 0.0;
    // 与 decode_word_lattice 一样从空上下文起步，见那边的注释。
    if (model)
        start.state = model->null_state();
    columns[0].push_back(std::move(start));

    for (std::size_t pos = 0; pos < n; ++pos)
    {
        keep_beam(columns[pos], options.beam);
        if (columns[pos].empty())
            continue;
        for (int hi = 0; hi < static_cast<int>(columns[pos].size()); ++hi)
        {
            const Hyp &hyp = columns[pos][static_cast<std::size_t>(hi)];
            if (graph.skip[pos])
            {
                Hyp next = hyp;
                next.prev_pos = static_cast<int>(pos);
                next.prev_idx = hi;
                next.edge = nullptr;
                columns[pos + 1].push_back(std::move(next));
            }
            for (const auto &edge : words[pos])
            {
                Hyp next;
                next.score = hyp.score + edge.base_score;
                if (edge.placeholder || !model)
                    next.state = hyp.state;
                // 关闭搭配项时尾窗始终为空，不为每个假设白拷一次。
                if (collocation && edge.placeholder)
                    next.collocation_tail = hyp.collocation_tail;
                if (!edge.placeholder)
                {
                    if (model)
                        next.score += model->score(hyp.state, edge.index, next.state);
                    if (collocation)
                    {
                        const bool is_rear = next_spelling_position(graph, edge.end) == n;
                        next.score += options.collocation_weight *
                                      options.collocation_scorer(hyp.collocation_tail.view(), edge.word, is_rear);
                        next.collocation_tail = hyp.collocation_tail.extended(edge.word);
                    }
                }
                next.prev_pos = static_cast<int>(pos);
                next.prev_idx = hi;
                next.edge = &edge;
                columns[edge.end].push_back(std::move(next));
            }
        }
    }

    const auto &final_col = columns[n];
    if (final_col.empty())
        return std::nullopt;
    const auto best = std::max_element(final_col.begin(), final_col.end(),
                                       [](const Hyp &a, const Hyp &b) { return a.score < b.score; });

    DecodedPath path;
    path.log_prob = best->score;
    std::vector<const WordEdge *> edges;
    int pos = static_cast<int>(n);
    int idx = static_cast<int>(std::distance(final_col.begin(), best));
    while (pos > 0 && idx >= 0)
    {
        const Hyp &hyp = columns[static_cast<std::size_t>(pos)][static_cast<std::size_t>(idx)];
        if (hyp.edge)
            edges.push_back(hyp.edge);
        pos = hyp.prev_pos;
        idx = hyp.prev_idx;
    }
    std::reverse(edges.begin(), edges.end());
    for (const auto *edge : edges)
    {
        path.complete = path.complete && !edge->placeholder;
        path.sentence += edge->word;
        if (!edge->placeholder)
        {
            if (!path.key.empty())
                path.key.push_back('\'');
            path.key += edge->key;
            path.words.push_back(edge->word);
        }
        for (const auto *spelling : edge->spellings)
            path.syllables.push_back(*spelling);
    }
    return path;
}

} // namespace direct_helpcode
