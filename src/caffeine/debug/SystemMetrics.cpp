#include "debug/SystemMetrics.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#ifdef __linux__
#include <dlfcn.h>
#include <unistd.h>
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif

#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <mach/processor_info.h>
#include <sys/resource.h>
#include <sys/sysctl.h>
#endif

namespace Caffeine::Debug {
namespace {

void* openLibrary(const char* name) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(LoadLibraryA(name));
#elif defined(__linux__)
    return dlopen(name, RTLD_LAZY);
#else
    (void)name;
    return nullptr;
#endif
}

void* librarySymbol(void* library, const char* name) {
    if (!library) return nullptr;
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(library), name));
#elif defined(__linux__)
    return dlsym(library, name);
#else
    (void)name;
    return nullptr;
#endif
}

// NVIDIA's management library: the same functions on Linux (libnvidia-ml.so) and Windows (nvml.dll).
struct NvmlFns {
    using InitFn = int (*)();
    using ShutdownFn = int (*)();
    using DeviceCountFn = int (*)(unsigned int*);
    using DeviceHandleFn = int (*)(unsigned int, void**);
    using GenericFn = void*;

    void* lib = nullptr;
    InitFn init = nullptr;
    ShutdownFn shutdown = nullptr;
    DeviceCountFn deviceCount = nullptr;
    DeviceHandleFn deviceHandle = nullptr;
    GenericFn util = nullptr;
    GenericFn mem = nullptr;
    bool ready = false;
};

NvmlFns& nvml() {
    static NvmlFns fns;
    static bool attempted = false;
    if (attempted) return fns;
    attempted = true;

#ifdef _WIN32
    fns.lib = openLibrary("nvml.dll");
    if (!fns.lib) fns.lib = openLibrary("C:\\Program Files\\NVIDIA Corporation\\NVSMI\\nvml.dll");
#else
    fns.lib = openLibrary("libnvidia-ml.so.1");
    if (!fns.lib) fns.lib = openLibrary("libnvidia-ml.so");
#endif
    if (!fns.lib) return fns;

    fns.init = reinterpret_cast<NvmlFns::InitFn>(librarySymbol(fns.lib, "nvmlInit_v2"));
    if (!fns.init) fns.init = reinterpret_cast<NvmlFns::InitFn>(librarySymbol(fns.lib, "nvmlInit"));
    fns.shutdown = reinterpret_cast<NvmlFns::ShutdownFn>(librarySymbol(fns.lib, "nvmlShutdown"));
    fns.deviceCount = reinterpret_cast<NvmlFns::DeviceCountFn>(librarySymbol(fns.lib, "nvmlDeviceGetCount_v2"));
    if (!fns.deviceCount) {
        fns.deviceCount = reinterpret_cast<NvmlFns::DeviceCountFn>(librarySymbol(fns.lib, "nvmlDeviceGetCount"));
    }
    fns.deviceHandle = reinterpret_cast<NvmlFns::DeviceHandleFn>(librarySymbol(fns.lib, "nvmlDeviceGetHandleByIndex_v2"));
    if (!fns.deviceHandle) {
        fns.deviceHandle =
            reinterpret_cast<NvmlFns::DeviceHandleFn>(librarySymbol(fns.lib, "nvmlDeviceGetHandleByIndex"));
    }

    fns.util = librarySymbol(fns.lib, "nvmlDeviceGetUtilizationRates");
    fns.mem = librarySymbol(fns.lib, "nvmlDeviceGetMemoryInfo");

    if (!fns.init || !fns.deviceCount || !fns.deviceHandle) return fns;
    if (fns.init() != 0) return fns;
    fns.ready = true;
    return fns;
}

// NVML util struct is { unsigned int gpu; unsigned int memory; }
bool sampleNvml(SystemMetrics& out) {
    auto& n = nvml();
    if (!n.ready) return false;

    unsigned int count = 0;
    if (n.deviceCount(&count) != 0 || count == 0) return false;
    void* device = nullptr;
    if (n.deviceHandle(0, &device) != 0 || !device) return false;

    struct NvmlUtil {
        unsigned int gpu;
        unsigned int memory;
    } util{};
    if (n.util) {
        auto fn = reinterpret_cast<int (*)(void*, NvmlUtil*)>(n.util);
        if (fn(device, &util) == 0) {
            out.gpuBusyPercent = static_cast<f32>(util.gpu);
            out.gpuAvailable = true;
        }
    }
    struct NvmlMem {
        unsigned long long total;
        unsigned long long free;
        unsigned long long used;
    } mem{};
    if (n.mem) {
        auto fn = reinterpret_cast<int (*)(void*, NvmlMem*)>(n.mem);
        if (fn(device, &mem) == 0) {
            out.vramTotalMiB = static_cast<f32>(mem.total) / (1024.0f * 1024.0f);
            out.vramUsedMiB = static_cast<f32>(mem.used) / (1024.0f * 1024.0f);
            out.gpuAvailable = true;
        }
    }
    return out.gpuAvailable;
}

#ifdef __linux__
f32 sampleDrmGpuBusy() {
    for (int i = 0; i < 8; ++i) {
        char path[128];
        std::snprintf(path, sizeof(path), "/sys/class/drm/card%d/device/gpu_busy_percent", i);
        std::ifstream file(path);
        if (!file) continue;
        f32 value = 0.0f;
        file >> value;
        if (file) return value;
    }
    return -1.0f;
}

void sampleMemory(SystemMetrics& out) {
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            unsigned long kb = 0;
            if (std::sscanf(line.c_str(), "VmRSS: %lu", &kb) == 1) {
                out.ramUsedMiB = static_cast<f32>(kb) / 1024.0f;
            }
        }
    }

    std::ifstream meminfo("/proc/meminfo");
    while (std::getline(meminfo, line)) {
        if (line.rfind("MemTotal:", 0) == 0) {
            unsigned long kb = 0;
            if (std::sscanf(line.c_str(), "MemTotal: %lu", &kb) == 1) {
                out.ramTotalMiB = static_cast<f32>(kb) / 1024.0f;
            }
            break;
        }
    }
}

f32 sampleProcessCpu() {
    static unsigned long long lastProc = 0;
    static unsigned long long lastIdle = 0;
    static bool hasLast = false;

    std::ifstream stat("/proc/self/stat");
    if (!stat) return 0.0f;
    std::string dummy;
    unsigned long utime = 0;
    unsigned long stime = 0;
    // pid comm state ppid pgrp session tty tpgid flags minflt cminflt majflt cmajflt utime stime
    int pid = 0;
    char comm[256]{};
    char state = 0;
    if (!(stat >> pid >> comm >> state)) return 0.0f;
    for (int i = 0; i < 10; ++i) stat >> dummy;
    stat >> utime >> stime;
    const unsigned long long proc = static_cast<unsigned long long>(utime) + stime;

    std::ifstream cpuStat("/proc/stat");
    std::string cpu;
    unsigned long long user = 0, nice = 0, system = 0, idle = 0, iowait = 0, irq = 0, softirq = 0;
    cpuStat >> cpu >> user >> nice >> system >> idle >> iowait >> irq >> softirq;
    const unsigned long long total = user + nice + system + idle + iowait + irq + softirq;

    f32 percent = 0.0f;
    if (hasLast && total > lastIdle) {
        const unsigned long long dProc = proc - lastProc;
        const unsigned long long dTotal = total - lastIdle;
        if (dTotal > 0) {
            percent = 100.0f * static_cast<f32>(dProc) / static_cast<f32>(dTotal);
        }
    }
    lastProc = proc;
    lastIdle = total;
    hasLast = true;
    return percent;
}

#endif  // __linux__

// ---- The machine: a snapshot of the counters per platform, and one shared computation of the percentages.

/// Reads the counters of every logical core and of this process. False when the platform has none.
bool readCpuSnapshot(CpuSnapshot& out) {
    out = CpuSnapshot{};
#if defined(__linux__)
    std::ifstream stat("/proc/stat");
    if (!stat) return false;
    std::string line;
    bool haveAll = false;
    while (std::getline(stat, line)) {
        if (line.rfind("cpu", 0) != 0) break;
        char label[16]{};
        unsigned long long user = 0, nice = 0, system = 0, idle = 0, iowait = 0, irq = 0, softirq = 0, steal = 0;
        const int read = std::sscanf(line.c_str(), "%15s %llu %llu %llu %llu %llu %llu %llu %llu", label, &user,
                                     &nice, &system, &idle, &iowait, &irq, &softirq, &steal);
        if (read < 8) continue;
        CpuTimes t;
        t.total = user + nice + system + idle + iowait + irq + softirq + steal;
        t.busy = t.total - (idle + iowait);
        if (std::strcmp(label, "cpu") == 0) {
            out.all = t;
            haveAll = true;
        } else {
            out.cores.push_back(t);
        }
    }
    // utime and stime are the 14th and 15th fields; the command name (field 2) may hold spaces, so read from the
    // closing parenthesis.
    std::ifstream self("/proc/self/stat");
    std::string content((std::istreambuf_iterator<char>(self)), std::istreambuf_iterator<char>());
    const size_t close = content.rfind(')');
    unsigned long utime = 0, stime = 0;
    if (close != std::string::npos &&
        std::sscanf(content.c_str() + close + 1, " %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu", &utime,
                    &stime) == 2) {
        const long ticksPerSecond = sysconf(_SC_CLK_TCK);
        if (ticksPerSecond > 0) {
            out.processSeconds = static_cast<f64>(utime + stime) / static_cast<f64>(ticksPerSecond);
        }
    }
    return haveAll;
#elif defined(_WIN32)
    // NtQuerySystemInformation(SystemProcessorPerformanceInformation): idle, kernel (which includes idle) and user
    // time of every logical processor, in 100 ns units.
    struct ProcessorTimes {
        LARGE_INTEGER idle, kernel, user, dpc, interrupt;
        ULONG interruptCount;
    };
    using QueryFn = LONG(WINAPI*)(ULONG, PVOID, ULONG, PULONG);
    static const QueryFn query = reinterpret_cast<QueryFn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation")));
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const ULONG count = std::min<ULONG>(info.dwNumberOfProcessors, kMaxCpuCores);
    if (!query || count == 0) return false;
    std::vector<ProcessorTimes> times(count);
    ULONG returned = 0;
    constexpr ULONG kSystemProcessorPerformanceInformation = 8;
    if (query(kSystemProcessorPerformanceInformation, times.data(),
              static_cast<ULONG>(times.size() * sizeof(ProcessorTimes)), &returned) != 0) {
        return false;
    }
    const size_t got = std::min<size_t>(returned / sizeof(ProcessorTimes), times.size());
    for (size_t i = 0; i < got; ++i) {
        CpuTimes t;
        t.total = static_cast<unsigned long long>(times[i].kernel.QuadPart + times[i].user.QuadPart);
        t.busy = t.total - static_cast<unsigned long long>(times[i].idle.QuadPart);
        out.cores.push_back(t);
        out.all.busy += t.busy;
        out.all.total += t.total;
    }
    FILETIME creation{}, exitTime{}, kernel{}, user{};
    if (GetProcessTimes(GetCurrentProcess(), &creation, &exitTime, &kernel, &user)) {
        auto toSeconds = [](const FILETIME& f) {
            return static_cast<f64>((static_cast<unsigned long long>(f.dwHighDateTime) << 32) | f.dwLowDateTime) *
                   1e-7;
        };
        out.processSeconds = toSeconds(kernel) + toSeconds(user);
    }
    return got > 0;
#elif defined(__APPLE__)
    natural_t cpuCount = 0;
    processor_info_array_t info = nullptr;
    mach_msg_type_number_t infoCount = 0;
    if (host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO, &cpuCount, &info, &infoCount) !=
        KERN_SUCCESS) {
        return false;
    }
    for (natural_t i = 0; i < cpuCount && i < kMaxCpuCores; ++i) {
        const integer_t* ticks = info + i * CPU_STATE_MAX;
        CpuTimes t;
        const unsigned long long busy = static_cast<unsigned long long>(ticks[CPU_STATE_USER]) +
                                        static_cast<unsigned long long>(ticks[CPU_STATE_SYSTEM]) +
                                        static_cast<unsigned long long>(ticks[CPU_STATE_NICE]);
        t.busy = busy;
        t.total = busy + static_cast<unsigned long long>(ticks[CPU_STATE_IDLE]);
        out.cores.push_back(t);
        out.all.busy += t.busy;
        out.all.total += t.total;
    }
    vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(info), infoCount * sizeof(integer_t));
    struct rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        out.processSeconds = static_cast<f64>(usage.ru_utime.tv_sec) + static_cast<f64>(usage.ru_utime.tv_usec) * 1e-6 +
                             static_cast<f64>(usage.ru_stime.tv_sec) + static_cast<f64>(usage.ru_stime.tv_usec) * 1e-6;
    }
    return !out.cores.empty();
#else
    return false;
#endif
}

/// Memory of the machine and of this process, in MiB (fields the platform cannot give stay at -1).
void readMemory(MachineMetrics& out) {
#if defined(__linux__)
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        unsigned long kb = 0;
        if (line.rfind("VmRSS:", 0) == 0 && std::sscanf(line.c_str(), "VmRSS: %lu", &kb) == 1) {
            out.ramProcessMiB = static_cast<f32>(kb) / 1024.0f;
            break;
        }
    }
    std::ifstream meminfo("/proc/meminfo");
    unsigned long totalKb = 0, availableKb = 0;
    while (std::getline(meminfo, line)) {
        if (line.rfind("MemTotal:", 0) == 0) std::sscanf(line.c_str(), "MemTotal: %lu", &totalKb);
        if (line.rfind("MemAvailable:", 0) == 0) std::sscanf(line.c_str(), "MemAvailable: %lu", &availableKb);
    }
    if (totalKb > 0) {
        out.ramTotalMiB = static_cast<f32>(totalKb) / 1024.0f;
        out.ramUsedMiB = static_cast<f32>(totalKb - std::min(totalKb, availableKb)) / 1024.0f;
    }
#elif defined(_WIN32)
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory)) {
        out.ramTotalMiB = static_cast<f32>(static_cast<f64>(memory.ullTotalPhys) / (1024.0 * 1024.0));
        out.ramUsedMiB =
            static_cast<f32>(static_cast<f64>(memory.ullTotalPhys - memory.ullAvailPhys) / (1024.0 * 1024.0));
    }
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
        out.ramProcessMiB = static_cast<f32>(static_cast<f64>(counters.WorkingSetSize) / (1024.0 * 1024.0));
    }
#elif defined(__APPLE__)
    unsigned long long total = 0;
    size_t size = sizeof(total);
    if (sysctlbyname("hw.memsize", &total, &size, nullptr, 0) == 0 && total > 0) {
        out.ramTotalMiB = static_cast<f32>(static_cast<f64>(total) / (1024.0 * 1024.0));
        vm_statistics64_data_t vm{};
        mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
        if (host_statistics64(mach_host_self(), HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&vm), &count) ==
            KERN_SUCCESS) {
            const f64 pageBytes = static_cast<f64>(vm_page_size);
            const f64 available = (static_cast<f64>(vm.free_count) + static_cast<f64>(vm.inactive_count)) * pageBytes;
            out.ramUsedMiB = static_cast<f32>(std::max(0.0, static_cast<f64>(total) - available) / (1024.0 * 1024.0));
        }
    }
    mach_task_basic_info_data_t task{};
    mach_msg_type_number_t taskCount = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&task), &taskCount) ==
        KERN_SUCCESS) {
        out.ramProcessMiB = static_cast<f32>(static_cast<f64>(task.resident_size) / (1024.0 * 1024.0));
    }
#else
    (void)out;
#endif
}

void readGpu(MachineMetrics& out) {
    SystemMetrics gpu;
    if (sampleNvml(gpu)) {
        out.gpuBusyPercent = gpu.gpuBusyPercent;
        out.vramUsedMiB = gpu.vramUsedMiB;
        out.vramTotalMiB = gpu.vramTotalMiB;
        return;
    }
#ifdef __linux__
    out.gpuBusyPercent = sampleDrmGpuBusy();
#endif
}

f32 percentOf(unsigned long long busy, unsigned long long total) {
    if (total == 0) return 0.0f;
    return std::min(100.0f, 100.0f * static_cast<f32>(busy) / static_cast<f32>(total));
}

}  // namespace

void cpuLoadFromSnapshots(const CpuSnapshot& previous, const CpuSnapshot& now, f64 seconds,
                          MachineMetrics& out) {
    out.cpuCoreCount = static_cast<u32>(std::min<size_t>(now.cores.size(), kMaxCpuCores));
    if (previous.cores.size() != now.cores.size()) return;  // a core came or went: this reading has no baseline
    for (u32 i = 0; i < out.cpuCoreCount; ++i) {
        // A counter that went backwards (a wrap, a reset) counts as no time.
        const bool forward = now.cores[i].total >= previous.cores[i].total && now.cores[i].busy >= previous.cores[i].busy;
        out.cpuCorePercent[i] =
            forward ? percentOf(now.cores[i].busy - previous.cores[i].busy, now.cores[i].total - previous.cores[i].total)
                    : 0.0f;
    }
    if (now.all.total >= previous.all.total && now.all.busy >= previous.all.busy) {
        out.cpuTotalPercent = percentOf(now.all.busy - previous.all.busy, now.all.total - previous.all.total);
    }
    if (seconds > 1e-3 && now.processSeconds >= previous.processSeconds) {
        out.processCpuPercent = static_cast<f32>(100.0 * (now.processSeconds - previous.processSeconds) / seconds);
    }
}

MachineMetrics sampleMachineMetrics() {
    static CpuSnapshot previous;
    static std::chrono::steady_clock::time_point previousTime;
    static bool hasPrevious = false;

    MachineMetrics metrics;
    CpuSnapshot now;
    const auto time = std::chrono::steady_clock::now();
    if (readCpuSnapshot(now)) {
        if (hasPrevious) {
            cpuLoadFromSnapshots(previous, now, std::chrono::duration<f64>(time - previousTime).count(), metrics);
        } else {
            metrics.cpuCoreCount = static_cast<u32>(std::min<size_t>(now.cores.size(), kMaxCpuCores));
        }
        previous = std::move(now);
        previousTime = time;
        hasPrevious = true;
    }
    readMemory(metrics);
    readGpu(metrics);
    return metrics;
}

SystemMetrics sampleSystemMetrics() {
    static SystemMetrics cached{};
    static auto last = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last).count() < 200) {
        return cached;
    }
    last = now;

    SystemMetrics metrics;
#ifdef __linux__
    metrics.processCpuPercent = sampleProcessCpu();
    sampleMemory(metrics);
    if (!sampleNvml(metrics)) {
        metrics.gpuBusyPercent = sampleDrmGpuBusy();
        metrics.gpuAvailable = metrics.gpuBusyPercent >= 0.0f;
    }
#endif
    cached = metrics;
    return metrics;
}

}  // namespace Caffeine::Debug
