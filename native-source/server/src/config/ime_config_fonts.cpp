// 系统字体：用 DirectWrite 枚举字体族（GDI 兜底），并把配置里的字体名解析成 CSS 能匹配的族名。
#include "config/ime_config.h"
#include <Windows.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <cwchar>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include "utils/common_utils.h"

namespace
{
using Microsoft::WRL::ComPtr;

int CALLBACK EnumSystemFontFamExProc(const LOGFONTW *logfont, const TEXTMETRICW *, DWORD, LPARAM lparam)
{
    auto *families = reinterpret_cast<std::set<std::wstring> *>(lparam);
    if (!logfont || !families)
        return TRUE;
    if (logfont->lfFaceName[0] == L'\0' || logfont->lfFaceName[0] == L'@')
        return TRUE;
    families->insert(logfont->lfFaceName);
    return TRUE;
}

ComPtr<IDWriteFactory> GetSharedFontFactory()
{
    static const ComPtr<IDWriteFactory> factory = [] {
        ComPtr<IDWriteFactory> value;
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                            reinterpret_cast<IUnknown **>(value.GetAddressOf()));
        return value;
    }();
    return factory;
}

std::wstring GetPreferredLocalizedFontName(IDWriteLocalizedStrings *names)
{
    if (!names || names->GetCount() == 0)
        return {};

    UINT32 index = 0;
    BOOL exists = FALSE;
    wchar_t user_locale[LOCALE_NAME_MAX_LENGTH]{};
    if (GetUserDefaultLocaleName(user_locale, LOCALE_NAME_MAX_LENGTH) > 0)
        names->FindLocaleName(user_locale, &index, &exists);
    if (!exists)
        names->FindLocaleName(L"zh-cn", &index, &exists);
    if (!exists)
        names->FindLocaleName(L"en-us", &index, &exists);
    if (!exists)
        index = 0;

    UINT32 length = 0;
    if (FAILED(names->GetStringLength(index, &length)))
        return {};
    std::wstring value(length + 1, L'\0');
    if (FAILED(names->GetString(index, value.data(), length + 1)))
        return {};
    value.resize(length);
    return value;
}
} // namespace

std::string ResolveSystemFontFamilyForCss(const std::string &font)
{
    if (font.empty())
        return font;

    static std::mutex cache_mutex;
    static std::map<std::string, std::string> cache;
    {
        const std::lock_guard<std::mutex> lock(cache_mutex);
        const auto cached = cache.find(font);
        if (cached != cache.end())
            return cached->second;
    }

    const auto remember = [&](std::string value) {
        const std::lock_guard<std::mutex> lock(cache_mutex);
        return cache.emplace(font, std::move(value)).first->second;
    };

    const ComPtr<IDWriteFactory> factory = GetSharedFontFactory();
    if (!factory)
        return remember(font);

    ComPtr<IDWriteGdiInterop> gdi_interop;
    if (FAILED(factory->GetGdiInterop(&gdi_interop)) || !gdi_interop)
        return remember(font);

    LOGFONTW logfont{};
    logfont.lfCharSet = DEFAULT_CHARSET;
    logfont.lfWeight = FW_NORMAL;
    const std::wstring requested = string_to_wstring(font);
    if (requested.size() >= LF_FACESIZE)
        return remember(font);
    wcscpy_s(logfont.lfFaceName, requested.c_str());

    ComPtr<IDWriteFont> resolved_font;
    if (FAILED(gdi_interop->CreateFontFromLOGFONT(&logfont, &resolved_font)) || !resolved_font)
        return remember(font);

    ComPtr<IDWriteLocalizedStrings> typographic_names;
    BOOL has_typographic_names = FALSE;
    if (SUCCEEDED(resolved_font->GetInformationalStrings(DWRITE_INFORMATIONAL_STRING_TYPOGRAPHIC_FAMILY_NAMES,
                                                         &typographic_names, &has_typographic_names)) &&
        has_typographic_names)
    {
        const std::wstring name = GetPreferredLocalizedFontName(typographic_names.Get());
        if (!name.empty())
            return remember(wstring_to_string(name));
    }

    ComPtr<IDWriteFontFamily> family;
    ComPtr<IDWriteLocalizedStrings> family_names;
    if (SUCCEEDED(resolved_font->GetFontFamily(&family)) && family && SUCCEEDED(family->GetFamilyNames(&family_names)))
    {
        const std::wstring name = GetPreferredLocalizedFontName(family_names.Get());
        if (!name.empty())
            return remember(wstring_to_string(name));
    }
    return remember(font);
}

const std::vector<std::string> &GetSystemFontFamilies()
{
    static std::vector<std::string> cached;
    static bool loaded = false;
    if (loaded)
        return cached;

    std::set<std::wstring> families;
    const ComPtr<IDWriteFactory> factory = GetSharedFontFactory();
    ComPtr<IDWriteFontCollection> collection;
    if (factory && SUCCEEDED(factory->GetSystemFontCollection(&collection)) && collection)
    {
        const UINT32 count = collection->GetFontFamilyCount();
        for (UINT32 index = 0; index < count; ++index)
        {
            ComPtr<IDWriteFontFamily> family;
            ComPtr<IDWriteLocalizedStrings> names;
            if (SUCCEEDED(collection->GetFontFamily(index, &family)) && family &&
                SUCCEEDED(family->GetFamilyNames(&names)))
            {
                const std::wstring name = GetPreferredLocalizedFontName(names.Get());
                if (!name.empty() && name[0] != L'@')
                    families.insert(name);
            }
        }
    }

    // Older systems can fail DirectWrite collection creation. Keep the old GDI
    // enumeration as a fallback, but do not mix it into the normal list: GDI
    // face names often contain a weight suffix that Chromium cannot match as a
    // CSS font family.
    if (families.empty())
    {
        HDC hdc = GetDC(nullptr);
        if (hdc)
        {
            LOGFONTW probe{};
            probe.lfCharSet = DEFAULT_CHARSET;
            EnumFontFamiliesExW(hdc, &probe, EnumSystemFontFamExProc, reinterpret_cast<LPARAM>(&families), 0);
            ReleaseDC(nullptr, hdc);
        }
    }

    cached.reserve(families.size());
    for (const std::wstring &face : families)
    {
        cached.push_back(wstring_to_string(face));
    }
    loaded = true;
    return cached;
}
