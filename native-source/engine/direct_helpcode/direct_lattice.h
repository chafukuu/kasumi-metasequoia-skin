#pragma once

#include "spelling_graph.h"

#include "../quanpin/quanpin_query.h"

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// 直接辅助码的整句解码：在按键位置上的拼写图里铺词，一次 Viterbi 同时决定切分与用字。
//
// 和 quanpin::decode_word_lattice 用同一套打分（kenlm、词库权重平局、生僻读音先验、octagram
// 搭配项），区别只在图：那边的节点是固定切分的音节序号，这里是输入串的按键位置，同一段按键
// 的不同切法（uia|uiq 与 ui|au|iq）各自成边，由整句分数裁决。这就是 librime 在万象里做的事。
// 打分函数是从 word_lattice.cpp 抄过来的，为的是不动全拼的解码器。
namespace direct_helpcode
{

// 按全拼跨度查词。constrained 为真时跨度里有辅码约束，查询不能在 SQL 里先截断（生僻字会被
// 截没），由解码器筛完约束再截。
using SpanLookup = std::function<std::vector<quanpin::LatticeLexeme>(const quanpin::Segments &span, bool constrained)>;

// 这个汉字（UTF-8 单字）的辅码满不满足 first/second（second 为 0 只看第一码）。
using CharAccept = std::function<bool(const std::string &hanzi, char first, char second)>;

struct DecodeOptions
{
    int beam = 32;
    int span_limit = 32;
    int max_phrase_syllables = 7;
    // 每个起点最多展开多少条拼写序列，防止长串上组合爆炸。
    int max_sequences_per_start = 256;
    const ngram::LanguageModel *language_model = nullptr;
    double dictionary_tiebreak = 0.01;
    double reading_prior = 1.0;
    double reading_prior_share = 0.01;
    double reading_prior_floor = 5.0;
    double unigram_z = 1e6;
    double phrase_length_bonus = 3.0;
    // 单个声母（缩写）占位边的分数：没有字可出，只让切分走得通，任何真正出字的切法都比它好。
    double initial_penalty = -12.0;
    quanpin::LatticeCollocationScorer collocation_scorer;
    double collocation_weight = 0.0;
};

struct DecodedPath
{
    std::vector<SyllableSpelling> syllables;
    std::string sentence;
    // 全拼键（' 分隔）与逐词文本，和 quanpin::LatticePath 同义，供词格合并直接复用。
    std::string key;
    std::vector<std::string> words;
    double log_prob = 0;
    // 路径上有占位边（声母缩写、查不到字的音节）时整句不完整，不能当整句候选。
    bool complete = true;
};

// 一条拼写序列（音节 + 辅码）铺出来的词，跨按键复用：多敲一个键时前面那些序列的查词、辅码筛选
// 和打分都不变。只和词库、码表、打分选项有关，三者变了由调用方清空。
struct CachedWord
{
    std::string word;
    std::string key;
    double base_score = 0;
    ngram::WordIndex index = 0;
};
using WordEdgeMemo = std::unordered_map<std::string, std::vector<CachedWord>>;

std::optional<DecodedPath> decode_best_path(const SpellingGraph &graph, const SpanLookup &lookup,
                                            const CharAccept &accept, const DecodeOptions &options = {},
                                            WordEdgeMemo *memo = nullptr);

} // namespace direct_helpcode
