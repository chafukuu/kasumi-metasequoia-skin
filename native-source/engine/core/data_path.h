#pragma once

#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#ifdef _WIN32
#include <Windows.h>
#include <shlobj.h>
#endif

namespace metasequoia
{
#ifdef _WIN32
namespace detail
{
inline std::optional<std::wstring> wide_environment_variable(const wchar_t *name)
{
    wchar_t *buffer = nullptr;
    size_t size = 0;
    if (_wdupenv_s(&buffer, &size, name) != 0 || buffer == nullptr)
    {
        return std::nullopt;
    }

    const std::unique_ptr<wchar_t, decltype(&std::free)> owned_buffer(buffer, &std::free);
    return std::wstring(owned_buffer.get());
}

// The installer lets the user put user data on another volume and records that choice here.
// KEY_WOW64_64KEY is mandatory: the 32-bit TSF DLL and any 32-bit host would otherwise be
// redirected to Wow6432Node, where the 64-bit setup never wrote anything.
inline std::optional<std::wstring> read_installed_data_directory()
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Metasequoia\\MetasequoiaIME", 0,
                      KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS)
    {
        return std::nullopt;
    }

    DWORD type = 0;
    DWORD bytes = 0;
    if (RegQueryValueExW(key, L"DataDir", nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS || type != REG_SZ ||
        bytes < sizeof(wchar_t))
    {
        RegCloseKey(key);
        return std::nullopt;
    }

    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    const LSTATUS status =
        RegQueryValueExW(key, L"DataDir", nullptr, &type, reinterpret_cast<LPBYTE>(value.data()), &bytes);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS)
    {
        return std::nullopt;
    }
    // The stored length may or may not include the terminator, and REG_SZ is not required to be
    // terminated at all; cut the string at the first NUL that is actually present.
    value.resize(std::wcslen(value.c_str()));
    if (value.empty())
    {
        return std::nullopt;
    }
    return value;
}

inline std::optional<std::wstring> installed_data_directory()
{
    // data_directory() sits on dictionary lookup paths; the install location cannot change while a
    // process lives, so read it once.
    static const std::optional<std::wstring> cached = read_installed_data_directory();
    return cached;
}
} // namespace detail
#endif

inline std::filesystem::path path_from_utf8(const char *path)
{
#if defined(__cpp_lib_char8_t)
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(path)));
#else
    return std::filesystem::u8path(path);
#endif
}

inline std::filesystem::path data_directory()
{
#ifdef _WIN32
    if (const auto override_path = detail::wide_environment_variable(L"METASEQUOIA_IME_DATA_DIR"))
    {
        const std::filesystem::path path(*override_path);
        if (path.is_absolute())
        {
            return path;
        }
    }
#else
    if (const char *override_path = std::getenv("METASEQUOIA_IME_DATA_DIR"))
    {
        const std::filesystem::path path = path_from_utf8(override_path);
        if (path.is_absolute())
        {
            return path;
        }
    }
#endif

#ifdef _WIN32
    // Chosen during setup. It outranks LOCALAPPDATA but not the environment override, so tests and
    // isolated runs keep working on a machine that has the product installed.
    if (const auto installed_path = detail::installed_data_directory())
    {
        const std::filesystem::path path(*installed_path);
        if (path.is_absolute())
        {
            return path;
        }
    }

    if (const auto local_app_data = detail::wide_environment_variable(L"LOCALAPPDATA"))
    {
        const std::filesystem::path root(*local_app_data);
        if (root.is_absolute())
        {
            return root / L"metasequoiaime";
        }
    }

    PWSTR known_path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &known_path)) && known_path)
    {
        const std::filesystem::path result = std::filesystem::path(known_path) / L"metasequoiaime";
        CoTaskMemFree(known_path);
        return result;
    }
#elif defined(__APPLE__)
    if (const char *home = std::getenv("HOME"))
    {
        const std::filesystem::path root(home);
        if (root.is_absolute())
        {
            return root / "Library" / "Application Support" / "metasequoiaime";
        }
    }
#else
    if (const char *xdg_data_home = std::getenv("XDG_DATA_HOME"))
    {
        const std::filesystem::path root(xdg_data_home);
        if (root.is_absolute())
        {
            return root / "metasequoiaime";
        }
    }
    if (const char *home = std::getenv("HOME"))
    {
        const std::filesystem::path root(home);
        if (root.is_absolute())
        {
            return root / ".local" / "share" / "metasequoiaime";
        }
    }
#endif

    return {};
}

inline std::filesystem::path data_file_path(const std::filesystem::path &relative_path)
{
    const std::filesystem::path directory = data_directory();
    if (directory.empty() || relative_path.empty() || relative_path.is_absolute())
    {
        return {};
    }
    return directory / relative_path;
}

inline std::string path_to_utf8(const std::filesystem::path &path)
{
    const auto utf8_path = path.u8string();
#if defined(__cpp_lib_char8_t)
    return {reinterpret_cast<const char *>(utf8_path.data()), utf8_path.size()};
#else
    return utf8_path;
#endif
}
} // namespace metasequoia
