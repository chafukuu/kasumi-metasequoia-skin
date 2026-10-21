#pragma once

#include "engine/core/data_path.h"

#include <filesystem>
#include <string>

namespace test
{
// Every dictionary/engine API takes its paths as UTF-8. path::string() converts through the ANSI
// code page instead, which corrupts a non-ASCII path and throws outright on a code page that cannot
// represent the characters -- and the fixtures using this all live under the temp directory, which
// sits inside the user profile. Tests must therefore convert with this, never with path::string().
inline std::string Utf8(const std::filesystem::path &path)
{
    return metasequoia::path_to_utf8(path);
}
} // namespace test
