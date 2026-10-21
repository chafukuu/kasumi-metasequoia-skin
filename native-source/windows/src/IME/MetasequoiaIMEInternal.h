#pragma once

// Declarations shared by MetasequoiaIME*.cpp: items that lived in MetasequoiaIME.cpp's anonymous
// namespace before the split and are now used by more than one of those files.
// Include only from windows/src/IME/MetasequoiaIME*.cpp.

#include <Windows.h>

namespace metasequoia_ime_detail
{
constexpr UINT CONNECT_NAMEDPIPE_RETRY_INTERVAL_MS = 50;

UINT NextWindowMessageToken();
} // namespace metasequoia_ime_detail
