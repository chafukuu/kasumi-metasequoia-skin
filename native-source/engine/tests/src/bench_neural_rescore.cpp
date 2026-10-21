// Measure the engine async rerank path with real dictionaries and neural models.
// Run by hand: METASEQUOIA_IME_DATA_DIR=<data directory> bench_neural_rescore [repeats]
// Each row measures the final key. Prefix keys run before the timer starts.
// session_cold and session_warm describe InputSession prefix/query caches.
// Warm mode types the prefix twice without clearing those caches.
// SentenceModel prefix K/V cache is process-wide and is not reset between rows.
// Model loading happens before timing starts.
// Active prefix jobs can delay the final job; async_ms includes that delay.

#include "core/input_session.h"
#include "core/data_path.h"
#include "core/runtime_paths.h"
#include "core/sentence_association_options.h"
#include "contracts/assets/assets.h"
#include "neural/neural_decoder.h"
#include "neural/rescore_worker.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <utility>

namespace
{
using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
constexpr auto kTimeout = std::chrono::seconds(30);

struct CallbackEvents
{
    std::mutex mutex;
    std::condition_variable ready;
    std::deque<TimePoint> times;

    void push()
    {
        std::lock_guard<std::mutex> lock(mutex);
        times.push_back(Clock::now());
        ready.notify_one();
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex);
        times.clear();
    }

    bool wait_until(TimePoint deadline, TimePoint &time)
    {
        std::unique_lock<std::mutex> lock(mutex);
        if (!ready.wait_until(lock, deadline, [&] { return !times.empty(); }))
            return false;
        // A slow initial query can overlap prefix and final callbacks. Use the newest event.
        time = times.back();
        times.clear();
        return true;
    }
};

double milliseconds(TimePoint start, TimePoint end)
{
    return std::chrono::duration<double, std::milli>(end - start).count();
}

bool has_source(const metasequoia::InputSession &session, CandidateSource source)
{
    const auto &candidates = session.candidates();
    return std::any_of(candidates.begin(), candidates.end(),
                       [source](const WordItem &candidate) { return candidate.source == source; });
}

bool type_prefix(metasequoia::InputSession &session, const std::string &input)
{
    for (std::size_t index = 0; index + 1 < input.size(); ++index)
        if (!session.handle_character(input[index]).handled)
            return false;
    return true;
}

std::string context_of_length(std::size_t characters)
{
    constexpr std::array<const char *, 18> phrase = {"今", "天", "下", "午", "我", "们", "讨", "论", "输",
                                                     "入", "法", "排", "序", "和", "候", "选", "体", "验"};
    std::string context;
    for (std::size_t index = 0; index < characters; ++index)
        context += phrase[index % phrase.size()];
    return context;
}

struct Case
{
    const char *model;
    CandidateSource source;
    const char *context_name;
    std::string context;
    const char *length_name;
    const char *pinyin;
    const char *session_cache_name;
    bool warm_session_cache;
};

void print_failure(const Case &item, int repeat, const char *status)
{
    std::printf("%s,%s,%s,%s,%s,%d,NA,NA,NA,NA,%s\n", item.model, item.context_name, item.length_name, item.pinyin,
                item.session_cache_name, repeat, status);
}

bool run_case(const Case &item, int repeat, CallbackEvents &events)
{
    neural::RescoreWorker &worker = neural::RescoreWorker::instance();
    worker.clear();
    events.clear();

    metasequoia::InputSession session(SchemeType::Quanpin);
    SentenceAssociationOptions options;
    options.word_lattice = true;
    options.show_next_on_duplicate = true;
    options.neural_keyboard = item.source == CandidateSource::NeuralKeyboard;
    options.neural_desktop = item.source == CandidateSource::NeuralDesktop;
    options.neural_desktop_model = metasequoia::path_to_utf8(
        metasequoia::RuntimePaths::legacy().resource(metasequoia::assets::neural_model_desktop));
    session.set_sentence_association(options);
    session.set_rescoring_context(item.context);

    const std::string input = item.pinyin;
    if (item.warm_session_cache)
    {
        if (!type_prefix(session, input))
        {
            print_failure(item, repeat, "invalid_prefix");
            return false;
        }
        session.reset_state();
    }
    else
    {
        session.reset_cache();
    }

    if (!type_prefix(session, input))
    {
        print_failure(item, repeat, "invalid_prefix");
        return false;
    }
    events.clear();

    const TimePoint start = Clock::now();
    if (!session.handle_character(input.back()).handled)
    {
        print_failure(item, repeat, "invalid_final_key");
        return false;
    }
    const TimePoint initial = Clock::now();
    const bool static_visible = has_source(session, CandidateSource::Generated);
    const bool neural_visible = has_source(session, item.source);
    if (!static_visible || neural_visible)
    {
        print_failure(item, repeat, static_visible ? "neural_already_visible" : "no_static_sentence");
        return false;
    }

    const TimePoint deadline = initial + kTimeout;
    TimePoint callback;
    while (events.wait_until(deadline, callback))
    {
        if (callback < start)
            continue;
        session.reset_sentence_cache();
        session.recompute_candidates();
        const TimePoint requery_done = Clock::now();
        const bool settled = has_source(session, item.source);
        if (!settled)
            continue;
        std::printf("%s,%s,%s,%s,%s,%d,%.3f,%.3f,%.3f,%.3f,ok\n", item.model, item.context_name, item.length_name,
                    item.pinyin, item.session_cache_name, repeat, milliseconds(start, initial),
                    milliseconds(initial, callback), milliseconds(callback, requery_done),
                    milliseconds(start, requery_done));
        return true;
    }
    print_failure(item, repeat, "neural_timeout");
    return false;
}

bool run_all(long repeats, CallbackEvents &events)
{
    for (const auto &[model, source] :
         {std::pair{"keyboard", CandidateSource::NeuralKeyboard}, std::pair{"desktop", CandidateSource::NeuralDesktop}})
    {
        for (const auto &[context_name, context_chars] :
             {std::pair{"empty", 0u}, std::pair{"16-char", 16u}, std::pair{"64-char", 64u}})
        {
            for (const auto &[length_name, pinyin] :
                 {std::pair{"short", "nihaoshijie"}, std::pair{"medium", "womenzaijianmian"},
                  std::pair{"long", "zhegewentiyinggaizenmejiejue"}})
            {
                for (bool warm : {false, true})
                {
                    const Case item{model,
                                    source,
                                    context_name,
                                    context_of_length(context_chars),
                                    length_name,
                                    pinyin,
                                    warm ? "session_warm" : "session_cold",
                                    warm};
                    for (int repeat = 1; repeat <= repeats; ++repeat)
                        if (!run_case(item, repeat, events))
                            return false;
                }
            }
        }
    }
    return true;
}
} // namespace

int main(int argc, char **argv)
{
    if (argc > 2)
    {
        std::fprintf(stderr, "Usage: bench_neural_rescore [repeats]\n");
        return 2;
    }
    char *end = nullptr;
    const long repeats = argc == 2 ? std::strtol(argv[1], &end, 10) : 3;
    if (repeats < 1 || repeats > 100 || (argc == 2 && *end != '\0'))
    {
        std::fprintf(stderr, "repeats must be between 1 and 100\n");
        return 2;
    }

    const auto paths = metasequoia::RuntimePaths::legacy();
    for (const char *name :
         {metasequoia::assets::main_dictionary, metasequoia::assets::pinyin_model, metasequoia::assets::language_model,
          metasequoia::assets::neural_model_keyboard, metasequoia::assets::neural_model_desktop})
    {
        if (!std::filesystem::is_regular_file(paths.resource(name)))
        {
            std::fprintf(stderr, "Missing benchmark asset: %s\n", name);
            return 2;
        }
    }
    for (const char *name : {metasequoia::assets::neural_model_keyboard, metasequoia::assets::neural_model_desktop})
    {
        if (neural::shared_sentence_model(metasequoia::path_to_utf8(paths.resource(name))) == nullptr)
        {
            std::fprintf(stderr, "Invalid benchmark model: %s\n", name);
            return 2;
        }
    }

    CallbackEvents events;
    neural::RescoreWorker &worker = neural::RescoreWorker::instance();
    worker.set_ready_callback([&events] { events.push(); });
    std::printf(
        "model,context,pinyin_class,pinyin,session_cache,repeat,initial_ms,async_ms,requery_ms,settle_ms,status\n");
    std::fflush(stdout);
    const bool passed = run_all(repeats, events);
    worker.set_ready_callback(nullptr);
    worker.shutdown();
    return passed ? 0 : 1;
}
