#include <Windows.h>
#include <fmt/core.h>
#include <sqlite3.h>
#include "fmt/base.h"
#include "shuangpin/engine.h"
#include <chrono>
#include "shuangpin/shuangpin_query.h"
#include "core/query_request.h"

using namespace std;

void testGenerate()
{
    ShuangpinEngine engine;
    QueryRequest request;
    request.scheme = SchemeType::Shuangpin;
    vector<UINT> sequence;
    // sequence = {'N', 'I', 'R'};
    // sequence = {'Y', 'I', 'R', 'F', 'I'};
    // sequence = {'Y', 'I', 'G', 'E'};
    // sequence = {'Y', 'I'};
    // sequence = {'N', 'I', 'H'};
    sequence = {'C', 'L', 'S'};

    for (const auto &c : sequence)
    {
        std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();
        request.key_strokes.push_back(KeyStroke{c, 0, static_cast<WCHAR>(c + ('a' - 'A'))});
        std::chrono::high_resolution_clock::time_point end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        fmt::println("Time: {} us", duration.count());
    }
    request.raw_input = "cls";
    request.valid = true;
    std::vector<WordItem> result = engine.query(request);

    for (const auto &item : result)
    {
        fmt::println("Word: {}", item.word);
    }
}

void testGetChar()
{
    fmt::println("First char: {}", shuangpin::get_first_han_char("𰻝什么东西呢"));
    fmt::println("Last char: {}", shuangpin::get_last_han_char("𰻝什么东西呢"));
    fmt::println("First char: {}", shuangpin::get_first_han_char("你好呀什么东西呢"));
    fmt::println("Last char: {}", shuangpin::get_last_han_char("什么东西呢我的汴梁"));
}

int main(int argc, char *argv[])
{
    testGenerate();
    // testGetChar();
    return 0;
}
