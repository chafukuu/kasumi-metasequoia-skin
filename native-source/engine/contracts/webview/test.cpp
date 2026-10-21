#include "validator.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <cstdio>

int run_test(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    std::ifstream input(argv[1]);
    if (!input)
        return 2;
    const std::string text((std::istreambuf_iterator<char>(input)), {});
    const auto fixtures = boost::json::parse(text);
    for (const auto &fixture : fixtures.as_array())
    {
        const auto &test = fixture.as_object();
        const auto *surface = test.if_contains("surface");
        if (metasequoia::webview::Validate(test.at("message"), test.at("direction").as_string(),
                                           surface ? std::string(surface->as_string()) : "") !=
            test.at("valid").as_bool())
        {
            std::cerr << test.at("name") << '\n';
            return 1;
        }
    }
    std::cout << fixtures.as_array().size() << " shared fixtures passed\n";
    return 0;
}

int main(int argc, char **argv)
{
    try
    {
        return run_test(argc, argv);
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
