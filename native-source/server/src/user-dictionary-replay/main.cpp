#include "engine/core/data_path.h"
#include "engine/user_dictionary/user_dictionary_journal.h"

#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <windows.h>

namespace
{
int run(int argc, wchar_t **argv)
{
    // The journal API speaks UTF-8 (see default_user_db_path), so never convert through
    // path::string()/path(std::string): those go via the ANSI code page, which corrupts a
    // non-ASCII (e.g. Chinese) user profile path on a GBK system and throws outright on a
    // code page that cannot represent the characters.
    std::filesystem::path data_dir =
        metasequoia::path_from_utf8(user_dictionary::default_user_db_path().c_str()).parent_path();
    for (int i = 1; i < argc; ++i)
    {
        if (std::wstring(argv[i]) == L"--data-dir" && i + 1 < argc)
        {
            data_dir = argv[++i];
        }
        else
        {
            std::wcerr << L"Usage: MetasequoiaImeDictionaryReplay.exe [--data-dir <directory>]\n";
            return 2;
        }
    }

    const auto result = user_dictionary::replay(metasequoia::path_to_utf8(data_dir / L"msime_user.db"),
                                                metasequoia::path_to_utf8(data_dir / L"msime.db"),
                                                metasequoia::path_to_utf8(data_dir / L"english.db"));
    if (!result.error.empty())
    {
        std::cerr << result.error << '\n';
        return 1;
    }
    std::cout << "Applied " << result.applied << " user dictionary operations.\n";
    if (result.rebased > 0)
        std::cout << "Kept the new dictionary weight for " << result.rebased << " learned entries.\n";
    return 0;
}
} // namespace

int wmain(int argc, wchar_t **argv)
{
    // The installer aborts the upgrade on any non-zero exit, so an escaping exception would
    // fastfail (0xC0000409) and leave the user with a failed install and no diagnosis.
    try
    {
        return run(argc, argv);
    }
    catch (const std::exception &error)
    {
        std::cerr << "user dictionary replay failed: " << error.what() << '\n';
        return 1;
    }
    catch (...)
    {
        std::cerr << "user dictionary replay failed with an unknown error\n";
        return 1;
    }
}
