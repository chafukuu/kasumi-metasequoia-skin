#include "config/credential_protection.h"

#include <windows.h>
#include <wincrypt.h>

#include <vector>

#pragma comment(lib, "crypt32.lib")

namespace
{
constexpr std::string_view kPrefix = "dpapi:";
constexpr char kEntropy[] = "MetasequoiaIME.config.credential.v1";
constexpr DWORD kBase64Flags = CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF;

DATA_BLOB EntropyBlob()
{
    return {static_cast<DWORD>(sizeof(kEntropy) - 1), reinterpret_cast<BYTE *>(const_cast<char *>(kEntropy))};
}

struct LocalBlob
{
    DATA_BLOB blob{};

    LocalBlob() = default;
    LocalBlob(const LocalBlob &) = delete;
    LocalBlob &operator=(const LocalBlob &) = delete;
    ~LocalBlob()
    {
        if (blob.pbData)
        {
            SecureZeroMemory(blob.pbData, blob.cbData);
            LocalFree(blob.pbData);
        }
    }
};
} // namespace

namespace CredentialProtection
{
bool IsProtected(std::string_view stored)
{
    return stored.substr(0, kPrefix.size()) == kPrefix;
}

std::optional<std::string> Protect(const std::string &plaintext)
{
    DATA_BLOB input{static_cast<DWORD>(plaintext.size()),
                    reinterpret_cast<BYTE *>(const_cast<char *>(plaintext.data()))};
    DATA_BLOB entropy = EntropyBlob();
    LocalBlob cipher;
    if (!CryptProtectData(&input, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &cipher.blob))
        return std::nullopt;

    DWORD length = 0;
    if (!CryptBinaryToStringA(cipher.blob.pbData, cipher.blob.cbData, kBase64Flags, nullptr, &length))
        return std::nullopt;
    std::string encoded(length, '\0');
    if (!CryptBinaryToStringA(cipher.blob.pbData, cipher.blob.cbData, kBase64Flags, encoded.data(), &length))
        return std::nullopt;
    encoded.resize(length);
    return std::string(kPrefix) + encoded;
}

std::optional<std::string> Unprotect(const std::string &stored)
{
    if (!IsProtected(stored))
        return stored;

    const std::string_view encoded = std::string_view(stored).substr(kPrefix.size());
    DWORD size = 0;
    if (!CryptStringToBinaryA(encoded.data(), static_cast<DWORD>(encoded.size()), CRYPT_STRING_BASE64, nullptr, &size,
                              nullptr, nullptr))
        return std::nullopt;
    std::vector<BYTE> bytes(size);
    if (!CryptStringToBinaryA(encoded.data(), static_cast<DWORD>(encoded.size()), CRYPT_STRING_BASE64, bytes.data(),
                              &size, nullptr, nullptr))
        return std::nullopt;

    DATA_BLOB input{size, bytes.data()};
    DATA_BLOB entropy = EntropyBlob();
    LocalBlob plain;
    if (!CryptUnprotectData(&input, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &plain.blob))
        return std::nullopt;
    return std::string(reinterpret_cast<const char *>(plain.blob.pbData), plain.blob.cbData);
}
} // namespace CredentialProtection
