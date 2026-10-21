#include "neural_model.h"

#include "utils/network_proxy.h"

#include <Windows.h>
#include <ShObjIdl.h>
#include <bcrypt.h>
#include <winhttp.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "winhttp.lib")

namespace neural_model
{

namespace
{

// 与 neural-model/lock.json 的 desktop 条目一致。
constexpr wchar_t kHost[] = L"github.com";
constexpr wchar_t kPath[] =
    L"/metasequoiaime/chinese-ime-lm/releases/download/model-v1/sentence-model-desktop.safetensors";
constexpr long long kExpectedSize = 25480184;
constexpr char kExpectedSha256[] = "0a6ecba69bf1d39c7eb49549c716477dd6c03fdf05757773e1f50435262fb469";
constexpr char kLicense[] = "Apache-2.0";

std::filesystem::path ModelDirectory()
{
    return ModelFile().parent_path();
}

std::filesystem::path PartFile()
{
    // 同一卷上的临时名，校验通过后原子 rename 到位。
    return ModelDirectory() / (std::string(kModelFileName) + ".part");
}

std::string Sha256Hex(const std::filesystem::path &file)
{
    std::ifstream input(file, std::ios::binary);
    if (!input)
        return {};
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
        return {};
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::string hex;
    if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) == 0)
    {
        bool ok = true;
        std::vector<char> buffer(1 << 20);
        while (ok && input)
        {
            input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const std::streamsize read = input.gcount();
            if (read > 0)
                ok = BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), static_cast<ULONG>(read), 0) == 0;
        }
        unsigned char digest[32] = {};
        if (ok && input.eof() && BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0)
        {
            static constexpr char kDigits[] = "0123456789abcdef";
            for (const unsigned char byte : digest)
            {
                hex.push_back(kDigits[byte >> 4]);
                hex.push_back(kDigits[byte & 0x0f]);
            }
        }
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return hex;
}

enum class DownloadState
{
    Idle,
    Downloading,
    Importing,
    Error,
};

struct RuntimeState
{
    DownloadState state = DownloadState::Idle;
    int progress = 0;
    std::string error;
};

std::mutex g_mutex;
RuntimeState g_state;

void SetState(DownloadState state, int progress, std::string error)
{
    std::lock_guard lock(g_mutex);
    g_state.state = state;
    g_state.progress = progress;
    g_state.error = std::move(error);
}

void Fail(std::string error)
{
    SetState(DownloadState::Error, 0, std::move(error));
    std::error_code cleanup;
    std::filesystem::remove(PartFile(), cleanup);
}

// PartFile 写好之后的公共尾段：校验 -> 原子落位 -> NOTICE。下载与导入都走这里，
// 所以「文件存在 = 曾通过校验」对两条来源同样成立。
void InstallPart(bool imported)
{
    std::string verify_error;
    if (!VerifyModelFile(PartFile(), verify_error))
    {
        Fail(verify_error);
        return;
    }
    std::error_code rename_error;
    std::filesystem::rename(PartFile(), ModelFile(), rename_error);
    if (rename_error)
    {
        Fail("模型落位失败");
        return;
    }
    std::ofstream notice(ModelDirectory() / "NOTICE.md", std::ios::binary | std::ios::trunc);
    if (notice)
    {
        notice << "# chinese-ime-lm 神经整句模型（效果优先）\n\n"
               << "- 来源：" << SourceUrl() << (imported ? "（用户自行下载后本地导入）" : "") << "\n"
               << "- 许可：" << kLicense << "\n"
               << "- 文件：" << kModelFileName << "（" << kExpectedSize << " 字节，SHA256 " << kExpectedSha256
               << "）\n";
    }
    SetState(DownloadState::Idle, 100, {});
}

void DownloadThread()
{
    std::error_code fs_error;
    std::filesystem::create_directories(ModelDirectory(), fs_error);
    if (fs_error)
    {
        SetState(DownloadState::Error, 0, "无法创建模型目录");
        return;
    }

    HINTERNET session = NetworkProxy::OpenWinHttpSession(L"MetasequoiaImeServer/1.0");
    if (session == nullptr)
    {
        SetState(DownloadState::Error, 0, "无法初始化网络");
        return;
    }
    WinHttpSetTimeouts(session, 30000, 30000, 30000, 0);
    HINTERNET connection = WinHttpConnect(session, kHost, INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET request = nullptr;
    if (connection != nullptr)
    {
        request = WinHttpOpenRequest(connection, L"GET", kPath, nullptr, WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (request != nullptr)
        {
            // release 资产经 302 跳到 CDN。
            DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
            WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
        }
    }
    bool ok = connection != nullptr && request != nullptr &&
              WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
              WinHttpReceiveResponse(request, nullptr);
    DWORD status = 0;
    DWORD status_size = sizeof(status);
    if (ok)
    {
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size, WINHTTP_NO_HEADER_INDEX);
        ok = status == HTTP_STATUS_OK;
    }

    std::ofstream out;
    if (ok)
    {
        out.open(PartFile(), std::ios::binary | std::ios::trunc);
        ok = static_cast<bool>(out);
    }

    long long received = 0;
    std::vector<char> buffer;
    while (ok)
    {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available))
        {
            ok = false;
            break;
        }
        if (available == 0)
            break;
        buffer.resize(available);
        DWORD read = 0;
        if (!WinHttpReadData(request, buffer.data(), available, &read) || read == 0)
        {
            ok = false;
            break;
        }
        out.write(buffer.data(), static_cast<std::streamsize>(read));
        if (!out)
        {
            ok = false;
            break;
        }
        received += read;
        if (received > kExpectedSize)
        {
            ok = false;
            break;
        }
        // 总长是钉死的，进度不必依赖响应是否声明 Content-Length。
        SetState(DownloadState::Downloading, static_cast<int>((std::min)(100LL, received * 100LL / kExpectedSize)), {});
    }
    if (request != nullptr)
        WinHttpCloseHandle(request);
    if (connection != nullptr)
        WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    out.close();

    if (!ok)
    {
        Fail(status != 0 && status != HTTP_STATUS_OK ? "下载响应异常" : "下载中断");
        return;
    }
    InstallPart(false);
}

// 先复制再校验，而不是直接校验原文件：落位的 rename 必须在同一卷上原子完成，原文件
// 可能在别的盘，也不该被挪走。
void ImportThread(std::filesystem::path source)
{
    std::error_code fs_error;
    std::filesystem::create_directories(ModelDirectory(), fs_error);
    if (fs_error)
    {
        SetState(DownloadState::Error, 0, "无法创建模型目录");
        return;
    }
    std::filesystem::copy_file(source, PartFile(), std::filesystem::copy_options::overwrite_existing, fs_error);
    if (fs_error)
    {
        Fail("无法读取所选文件");
        return;
    }
    InstallPart(true);
}

bool IsBusy(DownloadState state)
{
    return state == DownloadState::Downloading || state == DownloadState::Importing;
}

// 下载与导入的公共前置。占位成功返回 true；已就绪或同类任务已在进行时 satisfied 置 true。
bool ClaimSlot(DownloadState state, bool &satisfied)
{
    satisfied = false;
    std::error_code fs_error;
    // 落位只在校验之后发生，文件存在即曾通过校验，重下只会白白覆盖一份好文件。
    if (std::filesystem::exists(ModelFile(), fs_error) && !fs_error)
    {
        satisfied = true;
        return false;
    }
    std::lock_guard lock(g_mutex);
    if (IsBusy(g_state.state))
    {
        satisfied = g_state.state == state;
        return false;
    }
    g_state.state = state;
    g_state.progress = 0;
    g_state.error.clear();
    return true;
}

std::string Utf8FromWide(const wchar_t *wide)
{
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    std::string narrow(bytes > 0 ? static_cast<size_t>(bytes) - 1 : 0, '\0');
    if (bytes > 1)
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, narrow.data(), bytes, nullptr, nullptr);
    return narrow;
}

} // namespace

std::string SourceUrl()
{
    return "https://" + Utf8FromWide(kHost) + Utf8FromWide(kPath);
}

bool VerifyModelFile(const std::filesystem::path &file, std::string &error)
{
    std::error_code fs_error;
    const auto size = std::filesystem::file_size(file, fs_error);
    if (fs_error || static_cast<long long>(size) != kExpectedSize)
    {
        error = "文件大小与 chinese-ime-lm model-v1 不符";
        return false;
    }
    if (Sha256Hex(file) != kExpectedSha256)
    {
        error = "文件校验和与 chinese-ime-lm model-v1 不符";
        return false;
    }
    return true;
}

ModelStatus GetModelStatus()
{
    {
        // runtime 的下载与错误态优先于磁盘：刚失败的一次比旧文件还在更值得看见。
        std::lock_guard lock(g_mutex);
        if (g_state.state == DownloadState::Downloading)
            return {"downloading", g_state.progress, {}};
        if (g_state.state == DownloadState::Importing)
            return {"importing", 0, {}};
        if (g_state.state == DownloadState::Error)
            return {"error", 0, g_state.error};
    }
    std::error_code fs_error;
    if (std::filesystem::exists(ModelFile(), fs_error) && !fs_error)
        return {"ready", 100, {}};
    return {"absent", 0, {}};
}

bool StartDownload()
{
    bool satisfied = false;
    if (!ClaimSlot(DownloadState::Downloading, satisfied))
        return satisfied;
    std::thread(DownloadThread).detach();
    return true;
}

bool ImportModel(const std::filesystem::path &source)
{
    std::error_code fs_error;
    if (!std::filesystem::is_regular_file(source, fs_error) || fs_error)
        return false;
    bool satisfied = false;
    if (!ClaimSlot(DownloadState::Importing, satisfied))
        return satisfied;
    std::thread(ImportThread, source).detach();
    return true;
}

std::filesystem::path PromptForModelFile(HWND owner)
{
    // 设置窗口的 UI 线程启动时已初始化过 STA，这里的 CoInitializeEx 只是配平引用计数。
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    std::filesystem::path selected;
    IFileOpenDialog *dialog = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
    {
        const COMDLG_FILTERSPEC filters[] = {{L"神经整句模型 (*.safetensors)", L"*.safetensors"},
                                             {L"所有文件", L"*.*"}};
        dialog->SetFileTypes(ARRAYSIZE(filters), filters);
        dialog->SetTitle(L"选择已下载的 sentence-model-desktop.safetensors");
        DWORD options = 0;
        if (SUCCEEDED(dialog->GetOptions(&options)))
            dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
        IShellItem *item = nullptr;
        if (SUCCEEDED(dialog->Show(owner)) && SUCCEEDED(dialog->GetResult(&item)))
        {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
            {
                selected = path;
                CoTaskMemFree(path);
            }
            item->Release();
        }
        dialog->Release();
    }
    if (SUCCEEDED(init))
        CoUninitialize();
    return selected;
}

bool DeleteModel()
{
    {
        std::lock_guard lock(g_mutex);
        if (IsBusy(g_state.state))
            return false;
        // 删除也清掉上一次的错误态，页面回到「未下载」。
        g_state = {};
    }
    std::error_code error;
    std::filesystem::remove_all(ModelDirectory(), error);
    return !std::filesystem::exists(ModelFile(), error);
}

} // namespace neural_model
