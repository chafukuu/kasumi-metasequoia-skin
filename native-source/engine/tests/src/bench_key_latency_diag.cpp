// 按键级延迟诊断：逐键打点，并把每键耗时拆到「我们自己的算力 / 缺页 / 磁盘读取 / 被调度出去」
// 四类上，用来给 handle-engine 的尖峰归因（缺页、页回收、调度、杀软）。
//
// 不是测试：不断言、也不进 ctest。产品侧的 handle-engine 就是
// InputSession::handle_character 的同步调用（server/src/ipc/event_listener_keys.cpp 的
// "handle-engine" 阶段），所以这里逐键量到的就是那条同步路径。
//
//   set METASEQUOIA_IME_DATA_DIR=<数据目录的副本>
//   bench_key_latency_diag [repeats] [trim] [trim-every=<n>] [pressure=<MB>]
//                         [purge-standby] [cpu-load=<threads>] [octagram] [model=<path>]
//
// 只敲键和重置，从不选词，也不写用户日志；仍请指向数据目录的副本。
//
// 归因口径（每键同时记 wall / 线程 CPU / 进程缺页 / 进程读字节 / 工作集变化）：
//   wall ≈ cpu，且读字节不动   → 我们自己的算力
//   读字节上升                 → 页被回收后重新触达（硬件缺页，读盘）
//   只有缺页上升、读字节不动   → 软缺页（页还在内存里，只是被换出映射）
//   wall ≫ cpu，且四项都不动   → 线程压根没在跑：调度 / 别的进程抢 CPU / 杀软挂起
#include <windows.h>
#include <psapi.h>

#include "../../contracts/assets/assets.h"
#include "../../core/data_path.h"
#include "../../core/input_session.h"
#include "../../core/runtime_paths.h"
#include "../../core/sentence_association_options.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "psapi.lib")

namespace
{
// 与 bench_input_session 同一批输入，便于两组数字直接对照。
const std::vector<std::string> kInputs = {
    "nihao", "zhongguo", "shuru", "womenyiqiqu", "jintiantianqibucuo", "zhegewentiyinggaizenmejiejue", "yizhi",
};

struct Sample
{
    double wall_ms = 0.0;
    double cpu_ms = 0.0;
    unsigned long long faults = 0;
    unsigned long long read_bytes = 0;
    long long working_set_delta = 0;
    char character = '\0';
};

struct ProcessCounters
{
    unsigned long long faults = 0;
    unsigned long long read_bytes = 0;
    size_t working_set = 0;
};

ProcessCounters read_process_counters()
{
    ProcessCounters counters;
    PROCESS_MEMORY_COUNTERS memory = {};
    memory.cb = sizeof(memory);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory)))
    {
        counters.faults = memory.PageFaultCount;
        counters.working_set = memory.WorkingSetSize;
    }
    IO_COUNTERS io = {};
    if (GetProcessIoCounters(GetCurrentProcess(), &io))
    {
        counters.read_bytes = io.ReadTransferCount;
    }
    return counters;
}

// 本线程累计 CPU 时间（user + kernel，毫秒）。wall 与它的差就是「没在跑」的时间。
double thread_cpu_ms()
{
    FILETIME creation = {};
    FILETIME exit = {};
    FILETIME kernel = {};
    FILETIME user = {};
    if (!GetThreadTimes(GetCurrentThread(), &creation, &exit, &kernel, &user))
    {
        return 0.0;
    }
    ULARGE_INTEGER kernelTicks = {};
    kernelTicks.LowPart = kernel.dwLowDateTime;
    kernelTicks.HighPart = kernel.dwHighDateTime;
    ULARGE_INTEGER userTicks = {};
    userTicks.LowPart = user.dwLowDateTime;
    userTicks.HighPart = user.dwHighDateTime;
    return static_cast<double>(kernelTicks.QuadPart + userTicks.QuadPart) / 10000.0;
}

double percentile(std::vector<double> values, double p)
{
    if (values.empty())
    {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const size_t index = (std::min)(values.size() - 1, static_cast<size_t>(p * (values.size() - 1) + 0.5));
    return values[index];
}

// 占住物理内存，逼操作系统回收文件缓存——也就是我们 mmap 的 sc.lm 与 .gram 的页。
// 免提权，是「页回收」这一类的可复现手段。
class MemoryPressure
{
  public:
    explicit MemoryPressure(size_t megabytes)
    {
        const size_t total = megabytes * 1024ull * 1024ull;
        const size_t block = 64ull * 1024ull * 1024ull;
        for (size_t written = 0; written < total; written += block)
        {
            const size_t size = (std::min)(block, total - written);
            std::unique_ptr<unsigned char[]> chunk(new (std::nothrow) unsigned char[size]);
            if (chunk == nullptr)
            {
                break;
            }
            std::memset(chunk.get(), 0x5a, size); // 触达每一页，页表里才算真的占了
            blocks_.push_back(std::move(chunk));
            held_bytes_ += size;
        }
    }

    size_t held_bytes() const
    {
        return held_bytes_;
    }

  private:
    std::vector<std::unique_ptr<unsigned char[]>> blocks_;
    size_t held_bytes_ = 0;
};

// 抢 CPU，用来验证「wall ≫ cpu 且计数器都不动」这条判据确实能把调度类成本认出来。
class CpuLoad
{
  public:
    explicit CpuLoad(int threads)
    {
        running_.store(true, std::memory_order_relaxed);
        for (int i = 0; i < threads; ++i)
        {
            workers_.emplace_back([this]() {
                SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
                volatile double sink = 0.0;
                while (running_.load(std::memory_order_relaxed))
                {
                    for (int n = 1; n < 200000; ++n)
                    {
                        sink += static_cast<double>(n) * 0.5;
                    }
                }
            });
        }
    }

    ~CpuLoad()
    {
        running_.store(false, std::memory_order_relaxed);
        for (std::thread &worker : workers_)
        {
            worker.join();
        }
    }

  private:
    std::atomic<bool> running_{false};
    std::vector<std::thread> workers_;
};

// 唯一能把 mmap 的文件页真正读回磁盘的做法：清空系统 standby list。它是未公开接口，
// 需要 SeProfileSingleProcessPrivilege（提权），失败时如实回报，不假装成功。
std::string purge_standby_list()
{
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
    {
        TOKEN_PRIVILEGES privileges = {};
        privileges.PrivilegeCount = 1;
        privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        // 直接写名字：SE_PROFILE_SINGLE_PROCESS_NAME 依赖 winnt.h 的版本开关，未必可用。
        if (LookupPrivilegeValueW(nullptr, L"SeProfileSingleProcessPrivilege", &privileges.Privileges[0].Luid))
        {
            AdjustTokenPrivileges(token, FALSE, &privileges, 0, nullptr, nullptr);
        }
        CloseHandle(token);
    }

    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    using NtSetSystemInformationFn = LONG(NTAPI *)(ULONG, PVOID, ULONG);
    NtSetSystemInformationFn set_system_information =
        ntdll == nullptr ? nullptr
                         : reinterpret_cast<NtSetSystemInformationFn>(GetProcAddress(ntdll, "NtSetSystemInformation"));
    if (set_system_information == nullptr)
    {
        return "NtSetSystemInformation 不可用，跳过 standby list 清理";
    }
    const ULONG kSystemMemoryListInformation = 0x50;
    const ULONG kMemoryPurgeStandbyList = 4;
    const LONG status = set_system_information(
        kSystemMemoryListInformation, const_cast<ULONG *>(&kMemoryPurgeStandbyList), sizeof(kMemoryPurgeStandbyList));
    if (status < 0)
    {
        char message[128] = {};
        std::snprintf(message, sizeof(message), "standby list 清理失败，NTSTATUS=0x%08lX%s（需要提权运行）",
                      static_cast<unsigned long>(status),
                      status == static_cast<LONG>(0xC0000061) ? " STATUS_PRIVILEGE_NOT_HELD" : "");
        return message;
    }
    return "standby list 已清空（mmap 文件页已读回磁盘）";
}

bool is_trim_key(int key_index, int trim_every)
{
    return trim_every > 0 && key_index % trim_every == 0;
}
} // namespace

int main(int argc, char **argv)
{
    int repeats = 3;
    int trim_every = 0;
    size_t pressure_mb = 0;
    int cpu_load_threads = 0;
    bool purge_standby = false;
    bool octagram = false;
    std::string model_override;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "trim")
        {
            trim_every = 1;
        }
        else if (arg.rfind("trim-every=", 0) == 0)
        {
            trim_every = (std::max)(1, std::atoi(arg.c_str() + 11));
        }
        else if (arg.rfind("pressure=", 0) == 0)
        {
            pressure_mb = static_cast<size_t>(std::strtoull(arg.c_str() + 9, nullptr, 10));
        }
        else if (arg.rfind("cpu-load=", 0) == 0)
        {
            cpu_load_threads = (std::max)(0, std::atoi(arg.c_str() + 9));
        }
        else if (arg == "purge-standby")
        {
            purge_standby = true;
        }
        else if (arg == "octagram")
        {
            octagram = true;
        }
        else if (arg.rfind("model=", 0) == 0)
        {
            model_override = arg.substr(6);
        }
        else
        {
            repeats = (std::max)(1, std::atoi(argv[i]));
        }
    }

    SentenceAssociationOptions options;
    options.word_lattice = true;
    options.google = true;
    options.neural_keyboard = true;
    options.neural_desktop_model = metasequoia::path_to_utf8(
        metasequoia::RuntimePaths::legacy().resource(metasequoia::assets::neural_model_desktop));
    // 与出厂默认一致：octagram 默认关（config.default.toml 的 sentence_collocation_enabled=false），
    // 写 octagram 或给 model= 时才挂上模型，用来单独量那 390MB 映射的成本。
    std::filesystem::path collocation;
    if (!model_override.empty())
    {
        collocation = model_override;
    }
    else if (const char *env_model = std::getenv("METASEQUOIA_IME_TEST_COLLOCATION_MODEL"))
    {
        collocation = env_model;
    }
    else
    {
        collocation = metasequoia::data_directory() / "models" / "wanxiang-lts-zh-hans" / "wanxiang-lts-zh-hans.gram";
    }
    if (octagram && std::filesystem::exists(collocation))
    {
        options.collocation_rerank = true;
        options.collocation_model = metasequoia::path_to_utf8(collocation);
    }

    std::printf("data dir      : %s\n", metasequoia::path_to_utf8(metasequoia::data_directory()).c_str());
    std::printf("repeats       : %d\n", repeats);
    std::printf("trim-every    : %d (0 = 不裁剪工作集)\n", trim_every);
    std::printf("pressure      : %zu MB\n", pressure_mb);
    std::printf("cpu-load      : %d threads\n", cpu_load_threads);
    std::printf("octagram      : %s\n", options.collocation_model.empty() ? "off" : options.collocation_model.c_str());
    if (purge_standby)
    {
        const std::string result = purge_standby_list();
        std::printf("purge-standby : %s\n", result.c_str());
    }

    std::unique_ptr<MemoryPressure> pressure;
    std::unique_ptr<CpuLoad> cpu_load;
    if (cpu_load_threads > 0)
    {
        cpu_load = std::make_unique<CpuLoad>(cpu_load_threads);
    }

    metasequoia::InputSession session(SchemeType::Quanpin);
    session.set_sentence_association(options);

    std::vector<Sample> samples;
    const auto run_started = std::chrono::steady_clock::now();
    int key_index = 0;
    double first_key_ms = -1.0;
    for (int round = -1; round < repeats; ++round)
    {
        if (round == 0 && pressure_mb > 0)
        {
            // 预热轮先跑完再施加压力：否则预热轮自己就把模型页重新触达一遍，
            // 等真正开始记数时页面又都在内存里，什么都测不到。
            pressure = std::make_unique<MemoryPressure>(pressure_mb);
            std::printf("held memory   : %.1f MB (pressure applied after warm-up)\n",
                        static_cast<double>(pressure->held_bytes()) / (1024.0 * 1024.0));
        }
        for (const std::string &input : kInputs)
        {
            // 与 bench_input_session 同口径：清全套缓存，模拟「这个词的每个前缀都没见过」。
            session.reset_state();
            session.reset_cache();
            for (const char character : input)
            {
                if (trim_every > 0 && is_trim_key(key_index, trim_every))
                {
                    EmptyWorkingSet(GetCurrentProcess());
                }
                const ProcessCounters before = read_process_counters();
                const double cpu_before = thread_cpu_ms();
                const auto start = std::chrono::steady_clock::now();
                session.handle_character(character);
                const double wall_ms =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                const double cpu_after = thread_cpu_ms();
                const ProcessCounters after = read_process_counters();
                if (key_index == 0)
                {
                    // 整个进程的第一个键：模型是 LAZY mmap，这一击里才把 kenlm / .gram /
                    // 词库的首页拉进来。预热能省掉的就是这一段。
                    first_key_ms = wall_ms;
                }
                ++key_index;
                if (round < 0)
                {
                    continue; // 预热轮：模型加载与首次触页不算进分布
                }
                Sample sample;
                sample.wall_ms = wall_ms;
                sample.cpu_ms = cpu_after - cpu_before;
                sample.faults = after.faults - before.faults;
                sample.read_bytes = after.read_bytes - before.read_bytes;
                sample.working_set_delta =
                    static_cast<long long>(after.working_set) - static_cast<long long>(before.working_set);
                sample.character = character;
                samples.push_back(sample);
            }
        }
    }
    session.reset_state();
    const double total_wall_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - run_started).count();

    std::vector<double> walls;
    walls.reserve(samples.size());
    double sum_cpu = 0.0;
    unsigned long long sum_faults = 0;
    unsigned long long sum_read = 0;
    for (const Sample &sample : samples)
    {
        walls.push_back(sample.wall_ms);
        sum_cpu += sample.cpu_ms;
        sum_faults += sample.faults;
        sum_read += sample.read_bytes;
    }

    constexpr double kSlowMs = 50.0;
    size_t slow = 0;
    size_t slow_with_read = 0;
    size_t slow_with_faults = 0;
    size_t slow_descheduled = 0;
    for (const Sample &sample : samples)
    {
        if (sample.wall_ms < kSlowMs)
        {
            continue;
        }
        ++slow;
        if (sample.read_bytes > 0)
        {
            ++slow_with_read;
        }
        if (sample.faults > 200)
        {
            ++slow_with_faults;
        }
        if (sample.cpu_ms < sample.wall_ms * 0.2 && sample.read_bytes == 0 && sample.faults < 50)
        {
            ++slow_descheduled;
        }
    }

    std::printf("\nkeys=%zu  total=%.1fs  per key: median %.2f  p90 %.2f  p99 %.2f  max %.2f ms\n", samples.size(),
                total_wall_s, percentile(walls, 0.5), percentile(walls, 0.9), percentile(walls, 0.99),
                percentile(walls, 1.0));
    std::printf("first key (cold mmap): %.2f ms\n", first_key_ms);
    std::printf("sum(wall)=%.1fms  sum(cpu)=%.1fms  faults=%llu  read=%.2fMB   (cpu/wall=%.2f)\n",
                std::accumulate(walls.begin(), walls.end(), 0.0), sum_cpu, sum_faults,
                static_cast<double>(sum_read) / (1024.0 * 1024.0),
                std::accumulate(walls.begin(), walls.end(), 0.0) > 0.0
                    ? sum_cpu / std::accumulate(walls.begin(), walls.end(), 0.0)
                    : 0.0);
    std::printf("keys > %.0fms: %zu  (其中 读盘 %zu / 缺页>200 %zu / 疑似被调度出去 %zu)\n", kSlowMs, slow,
                slow_with_read, slow_with_faults, slow_descheduled);

    std::vector<Sample> offenders = samples;
    std::sort(offenders.begin(), offenders.end(),
              [](const Sample &a, const Sample &b) { return a.wall_ms > b.wall_ms; });
    const size_t top_count = (std::min)(offenders.size(), static_cast<size_t>(10));
    if (top_count > 0)
    {
        std::printf("\n%6s %8s %8s %8s %10s %12s\n", "key", "wall", "cpu", "faults", "read_bytes", "ws_delta");
        for (size_t i = 0; i < top_count; ++i)
        {
            const Sample &sample = offenders[i];
            std::printf("%6c %8.2f %8.2f %8llu %10llu %12lld\n", sample.character, sample.wall_ms, sample.cpu_ms,
                        sample.faults, sample.read_bytes, sample.working_set_delta);
        }
    }
    return 0;
}
