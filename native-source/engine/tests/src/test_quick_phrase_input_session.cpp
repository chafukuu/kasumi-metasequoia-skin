#include "../../contracts/assets/assets.h"
#include "../../core/data_path.h"
#include "../../core/input_session.h"
#include "../../core/runtime_paths.h"
#include "../../local_modes/quick_phrase_query.h"
#include "../../user_dictionary/user_dictionary_journal.h"
#include "test_directory_cleanup.h"

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstdio>

namespace
{
class Database
{
  public:
    explicit Database(const std::filesystem::path &path)
    {
        if (sqlite3_open(metasequoia::path_to_utf8(path).c_str(), &database_) != SQLITE_OK)
        {
            throw std::runtime_error("Failed to open the quick-phrase test dictionary.");
        }
    }

    ~Database()
    {
        sqlite3_close(database_);
    }

    void execute(const char *sql)
    {
        char *error = nullptr;
        if (sqlite3_exec(database_, sql, nullptr, nullptr, &error) != SQLITE_OK)
        {
            const std::string message = error == nullptr ? "SQLite operation failed." : error;
            sqlite3_free(error);
            throw std::runtime_error(message);
        }
    }

    std::int64_t query_integer(const char *sql)
    {
        sqlite3_stmt *statement = nullptr;
        if (sqlite3_prepare_v2(database_, sql, -1, &statement, nullptr) != SQLITE_OK ||
            sqlite3_step(statement) != SQLITE_ROW)
        {
            sqlite3_finalize(statement);
            throw std::runtime_error("Failed to query the quick-phrase test dictionary.");
        }
        const std::int64_t value = sqlite3_column_int64(statement, 0);
        sqlite3_finalize(statement);
        return value;
    }

  private:
    sqlite3 *database_ = nullptr;
};

void require(bool condition, const char *message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

void type(metasequoia::InputSession &session, const std::string &text)
{
    for (const char character : text)
    {
        require(session.handle_character(character).handled, "A quick-phrase test character was not handled.");
    }
}

void set_data_directory(const std::filesystem::path &directory)
{
#ifdef _WIN32
    if (_wputenv_s(L"METASEQUOIA_IME_DATA_DIR", directory.c_str()) != 0)
#else
    if (setenv("METASEQUOIA_IME_DATA_DIR", metasequoia::path_to_utf8(directory).c_str(), 1) != 0)
#endif
    {
        throw std::runtime_error("Failed to set the quick-phrase test data directory.");
    }
}

void prepare_main_database(const std::filesystem::path &directory)
{
    std::filesystem::create_directories(directory);
    Database database(directory / "msime.db");
    database.execute("CREATE TABLE tbl_1_n(key TEXT,jp TEXT,value TEXT,weight INTEGER)");
    database.execute("INSERT INTO tbl_1_n VALUES('ni','n','你',300)");
    database.execute("INSERT INTO tbl_1_n VALUES('ni','n','呢',200)");
    database.execute("INSERT INTO tbl_1_n VALUES('ni','n','泥',100)");
    database.execute("CREATE TABLE quick_parases(key TEXT,value TEXT,weight INTEGER)");
    database.execute("INSERT INTO quick_parases VALUES('ni','快捷一',20)");
    database.execute("INSERT INTO quick_parases VALUES('ni','快捷二',10)");
    database.execute("INSERT INTO quick_parases VALUES('ni','泥',5)");
    database.execute("INSERT INTO quick_parases VALUES('dia','的的',10)");
}

std::size_t first_quick_phrase(const std::vector<WordItem> &candidates)
{
    const auto found = std::find_if(candidates.begin(), candidates.end(),
                                    [](const WordItem &item) { return item.source == CandidateSource::QuickPhrase; });
    return static_cast<std::size_t>(found - candidates.begin());
}

std::size_t index_of(const std::vector<WordItem> &candidates, const std::string &word)
{
    const auto found =
        std::find_if(candidates.begin(), candidates.end(), [&](const WordItem &item) { return item.word == word; });
    return static_cast<std::size_t>(found - candidates.begin());
}

std::unique_ptr<metasequoia::InputSession> make_session(bool frequency)
{
    auto session = std::make_unique<metasequoia::InputSession>(SchemeType::Quanpin);
    session->enable_fixed_positions();
    if (!session->set_frequency_adjustment({metasequoia::FrequencyAdjustmentMode::Promote, 1, 1}))
    {
        throw std::runtime_error("Promote frequency adjustment was rejected.");
    }
    metasequoia::LocalModeOptions options;
    options.quick_phrase_frequency = frequency;
    session->set_local_mode_options(options);
    return session;
}

void test_placement()
{
    auto session = make_session(true);
    type(*session, "ni");
    const auto &candidates = session->candidates();
    require(candidates.size() >= 5 && candidates[0].word == "快捷一" && candidates[1].word == "快捷二" &&
                candidates[2].word == "泥" && candidates[2].source == CandidateSource::QuickPhrase &&
                candidates[3].word == "你",
            "Quick phrases were not placed first in weight order.");
    require(std::count_if(candidates.begin(), candidates.end(),
                          [](const WordItem &item) { return item.word == "泥"; }) == 1,
            "An ordinary candidate duplicating a quick phrase was kept.");

    auto partial = make_session(true);
    type(*partial, "n");
    require(first_quick_phrase(partial->candidates()) == partial->candidates().size(),
            "Quick phrases matched a code prefix instead of the exact code.");

    metasequoia::InputSession wubi(SchemeType::Wubi);
    wubi.enable_fixed_positions();
    (void)wubi.handle_character('n');
    (void)wubi.handle_character('i');
    require(first_quick_phrase(wubi.candidates()) == wubi.candidates().size(),
            "Quick phrases were mixed into a wubi composition.");

    // 混排关掉只影响候选，Shift+K 仍可用。
    metasequoia::InputSession disabled(SchemeType::Quanpin);
    disabled.enable_fixed_positions();
    metasequoia::LocalModeOptions disabled_options;
    disabled_options.quick_phrase_candidates = false;
    disabled.set_local_mode_options(disabled_options);
    type(disabled, "ni");
    require(first_quick_phrase(disabled.candidates()) == disabled.candidates().size() &&
                index_of(disabled.candidates(), "泥") < disabled.candidates().size(),
            "Disabled quick-phrase candidates still reached the candidate list.");
    metasequoia::InputSession k_only(SchemeType::Quanpin);
    k_only.set_local_mode_options(disabled_options);
    require(k_only.handle_character('K', true).handled &&
                k_only.local_input_mode() == metasequoia::LocalInputMode::QuickPhrase,
            "Disabling quick-phrase candidates also disabled Shift+K.");
    type(k_only, "n");
    require(!k_only.candidates().empty() && k_only.candidates().front().word == "快捷一",
            "Shift+K lost its prefix query when quick-phrase candidates were disabled.");

    // K 模式关掉不影响混排。
    auto without_k = make_session(true);
    metasequoia::LocalModeOptions without_k_options;
    without_k_options.quick_phrase = false;
    without_k->set_local_mode_options(without_k_options);
    require(!without_k->handle_character('K', true).handled, "A disabled Shift+K was still swallowed.");
    type(*without_k, "ni");
    require(first_quick_phrase(without_k->candidates()) == 0,
            "Disabling Shift+K also removed quick phrases from the candidate list.");

    // 双拼辅助码把 dia 读成 di + 辅助码 a，整串仍是用户编码，短语照样出现。
    metasequoia::InputSession shuangpin(SchemeType::Shuangpin);
    shuangpin.enable_fixed_positions();
    shuangpin.set_shuangpin_helpcode_enabled(true);
    type(shuangpin, "dia");
    require(shuangpin.has_active_helpcode(), "The shuangpin fixture did not read dia as di plus a helpcode.");
    require(!shuangpin.candidates().empty() && shuangpin.candidates().front().word == "的的" &&
                shuangpin.candidates().front().source == CandidateSource::QuickPhrase,
            "An odd-length shuangpin code read as a helpcode hid its quick phrase.");
}

void test_learning(const std::filesystem::path &directory)
{
    const std::string user_db =
        metasequoia::path_to_utf8(metasequoia::RuntimePaths::legacy().user(metasequoia::assets::user_journal));

    // 越过组选普通候选：组往后退一格。
    auto first = make_session(true);
    type(*first, "ni");
    require(first->select_candidate(index_of(first->candidates(), "你")).commit == "你",
            "Selecting an ordinary candidate committed the wrong text.");
    require(user_dictionary::quick_phrase_slot(user_db, "ni") == 1,
            "Bypassing the quick-phrase group did not demote it.");

    auto second = make_session(true);
    type(*second, "ni");
    require(first_quick_phrase(second->candidates()) == 1 && second->candidates()[0].word == "你",
            "A demoted quick-phrase group was not placed after one ordinary candidate.");
    // 选的普通候选排在组前面，不算越过。
    (void)second->select_candidate(0);
    require(user_dictionary::quick_phrase_slot(user_db, "ni") == 1,
            "Selecting a candidate above the group changed its slot.");

    auto third = make_session(true);
    type(*third, "ni");
    (void)third->select_candidate(index_of(third->candidates(), "呢"));
    require(user_dictionary::quick_phrase_slot(user_db, "ni") == 2,
            "Bypassing the group again did not demote it further.");

    // 选中组里非首条的快捷短语：组前移，并在组内升到最前。
    auto fourth = make_session(true);
    type(*fourth, "ni");
    require(first_quick_phrase(fourth->candidates()) == 2, "The quick-phrase group was not at slot 2.");
    const auto commit = fourth->select_candidate(index_of(fourth->candidates(), "快捷二"));
    require(commit.commit == "快捷二" && !commit.diagnostic.has_value(),
            "Selecting a quick phrase did not commit it cleanly.");
    require(user_dictionary::quick_phrase_slot(user_db, "ni") == 1,
            "Selecting a quick phrase did not promote the group.");
    {
        Database database(directory / "msime.db");
        require(database.query_integer("SELECT weight FROM quick_parases WHERE key='ni' AND value='快捷二'") == 21,
                "The selected quick phrase was not raised above its group.");
    }

    auto fifth = make_session(true);
    type(*fifth, "ni");
    require(fifth->candidates()[1].word == "快捷二" && fifth->candidates()[2].word == "快捷一",
            "The learned quick-phrase order was not applied.");

    // 调频开关关掉：组固定在首位，选择也不再学习。
    auto fixed = make_session(false);
    type(*fixed, "ni");
    require(first_quick_phrase(fixed->candidates()) == 0,
            "Disabling quick-phrase frequency did not keep the group first.");
    (void)fixed->select_candidate(index_of(fixed->candidates(), "你"));
    require(user_dictionary::quick_phrase_slot(user_db, "ni") == 1,
            "Quick-phrase learning ran with its switch disabled.");
}

void test_slot_cap()
{
    std::vector<WordItem> candidates;
    for (int index = 0; index < 8; ++index)
    {
        candidates.emplace_back("ni", "词" + std::to_string(index), 100 - index, CandidateSource::Database);
    }
    candidates[1].fixed_position = 1;
    std::vector<WordItem> phrases{WordItem("ni", "短语", 1, CandidateSource::QuickPhrase)};
    metasequoia::local_modes::place_quick_phrases(candidates, phrases, 2);
    require(candidates[0].word == "词1" && candidates[1].word == "词0" && candidates[2].word == "词2" &&
                candidates[3].word == "短语",
            "A pinned candidate was counted toward the quick-phrase slot.");
    metasequoia::local_modes::place_quick_phrases(candidates, phrases, 2);
    require(std::count_if(candidates.begin(), candidates.end(),
                          [](const WordItem &item) { return item.source == CandidateSource::QuickPhrase; }) == 1 &&
                candidates[3].word == "短语",
            "Placing quick phrases twice was not idempotent.");
}
} // namespace

int run_test()
{
    const auto suffix = std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("metasequoia-quick-phrase-session-" + suffix);
    metasequoia::test::ScopedDataDirectoryCleanup cleanup(root);
    const std::filesystem::path directory = root / "data";
    prepare_main_database(directory);
    set_data_directory(directory);

    test_placement();
    test_learning(directory);
    test_slot_cap();
    return 0;
}

int main()
{
    try
    {
        return run_test();
    }
    catch (const std::exception &error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    catch (...)
    {
        std::fprintf(stderr, "An exception escaped the test body.\n");
        return 1;
    }
}
