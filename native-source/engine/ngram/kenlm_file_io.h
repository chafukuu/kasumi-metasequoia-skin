#pragma once
// 只在编译 kenlm 的 util/file.cc 时强制包含（见 CMakeLists.txt）。
//
// kenlm 的 Windows 分支用 _open(name, ...) 打开模型文件，而 _open 的窄字符路径
// 走的是进程 ANSI 代码页。本仓的路径一律是 UTF-8（RuntimePaths / data_path.h 的
// 约定），用户名带中文时资源目录就落在 ANSI 页外，_open 会直接失败。
//
// 这里在 namespace util 里提前声明同名函数：file.cc 的调用点是非限定的
// _open(...)，非限定查找先看最内层的 util，于是解析到下面这两个重载，转成宽字符
// 调用。不改上游源码、不改进程代码页，与原先给 googlepinyinime 用的
// core/pinyin_file_io.h 是同一手法。
#ifdef _WIN32
#include <filesystem>
#include <io.h>
#include <share.h>

namespace util
{
// _wsopen_s 而不是弃用的 _wopen：模型文件会被 Server、TSF DLL、设置进程同时读，
// _SH_DENYNO 正是 _wopen 隐含的共享模式。
inline int _open(const char *path, int flags)
{
    int fd = -1;
    // pmode 只在 _O_CREAT 时被用到，这个重载的调用点不传 _O_CREAT。
    if (::_wsopen_s(&fd, std::filesystem::u8path(path).c_str(), flags, _SH_DENYNO, 0) != 0)
    {
        return -1;
    }
    return fd;
}

inline int _open(const char *path, int flags, int pmode)
{
    int fd = -1;
    if (::_wsopen_s(&fd, std::filesystem::u8path(path).c_str(), flags, _SH_DENYNO, pmode) != 0)
    {
        return -1;
    }
    return fd;
}
} // namespace util
#endif
