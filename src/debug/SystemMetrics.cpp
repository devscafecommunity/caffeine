#include "debug/SystemMetrics.hpp"

#include <chrono>
#include <cstring>
#include <fstream>
#include <string>

#ifdef __linux__
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace Caffeine::Debug {
namespace {

#ifdef __linux__
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

    fns.lib = dlopen("libnvidia-ml.so.1", RTLD_LAZY);
    if (!fns.lib) fns.lib = dlopen("libnvidia-ml.so", RTLD_LAZY);
    if (!fns.lib) return fns;

    fns.init = reinterpret_cast<NvmlFns::InitFn>(dlsym(fns.lib, "nvmlInit_v2"));
    if (!fns.init) fns.init = reinterpret_cast<NvmlFns::InitFn>(dlsym(fns.lib, "nvmlInit"));
    fns.shutdown = reinterpret_cast<NvmlFns::ShutdownFn>(dlsym(fns.lib, "nvmlShutdown"));
    fns.deviceCount = reinterpret_cast<NvmlFns::DeviceCountFn>(dlsym(fns.lib, "nvmlDeviceGetCount_v2"));
    if (!fns.deviceCount) {
        fns.deviceCount = reinterpret_cast<NvmlFns::DeviceCountFn>(dlsym(fns.lib, "nvmlDeviceGetCount"));
    }
    fns.deviceHandle = reinterpret_cast<NvmlFns::DeviceHandleFn>(dlsym(fns.lib, "nvmlDeviceGetHandleByIndex_v2"));
    if (!fns.deviceHandle) {
        fns.deviceHandle =
            reinterpret_cast<NvmlFns::DeviceHandleFn>(dlsym(fns.lib, "nvmlDeviceGetHandleByIndex"));
    }

    fns.util = dlsym(fns.lib, "nvmlDeviceGetUtilizationRates");
    fns.mem = dlsym(fns.lib, "nvmlDeviceGetMemoryInfo");

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
#endif

}  // namespace

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
