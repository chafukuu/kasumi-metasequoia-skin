#pragma once
// Included only while compiling the pinned decoder's dicttrie/userdict translation units.
// Their unqualified calls resolve these namespace overloads; no CRT macros or vendor edits.
#ifdef _WIN32
#include <cstdio>
#include <filesystem>
#include <io.h>
#include <share.h>
#include <string>
namespace ime_pinyin
{
// _wfsopen / _wsopen_s instead of the deprecated _wfopen / _wopen: the secure
// _wfopen_s opens without sharing, while the decoder's dictionary files are read
// concurrently by the Server, the TSF DLL and the settings process. _SH_DENYNO is
// exactly the sharing mode the deprecated calls used, so this only silences C4996.
inline FILE *fopen(const char *path, const char *mode)
{
    const std::wstring wide_mode(mode, mode + std::char_traits<char>::length(mode));
    return _wfsopen(std::filesystem::u8path(path).c_str(), wide_mode.c_str(), _SH_DENYNO);
}
inline int open(const char *path, int flags)
{
    int fd = -1;
    // pmode is only consulted for _O_CREAT, which no caller passes.
    if (_wsopen_s(&fd, std::filesystem::u8path(path).c_str(), flags, _SH_DENYNO, 0) != 0)
    {
        return -1;
    }
    return fd;
}
} // namespace ime_pinyin
#endif
