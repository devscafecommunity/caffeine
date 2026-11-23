#pragma once

#include "core/Types.hpp"

#include <vector>

namespace Caffeine::Debug {

struct SystemMetrics {
    f32 processCpuPercent = 0.0f;  // % of one core (can exceed 100)
    f32 gpuBusyPercent = -1.0f;    // -1 if unavailable
    f32 ramUsedMiB = 0.0f;
    f32 ramTotalMiB = -1.0f;
    f32 vramUsedMiB = -1.0f;
    f32 vramTotalMiB = -1.0f;
    bool gpuAvailable = false;
};

/// Samples process CPU/RAM and GPU util when a backend is present (NVML / sysfs).
SystemMetrics sampleSystemMetrics();

/// The most logical cores `sampleMachineMetrics` reports.
inline constexpr u32 kMaxCpuCores = 64;

/// The machine as the profiler's graphs show it. Percentages are over the time since the previous call (the first
/// call reports zeros), so call it at a steady rate (a few times a second). Fields a platform cannot provide stay
/// at their "unavailable" values: `cpuCoreCount` 0, the GPU and VRAM fields -1.
struct MachineMetrics {
    u32 cpuCoreCount = 0;
    f32 cpuCorePercent[kMaxCpuCores] = {};  ///< busy share of each logical core, 0..100
    f32 cpuTotalPercent = 0.0f;             ///< the whole machine, 0..100
    f32 processCpuPercent = 0.0f;           ///< this process, in percent of ONE core (can exceed 100)
    f32 ramProcessMiB = -1.0f;              ///< resident memory of this process
    f32 ramUsedMiB = -1.0f;                 ///< memory in use on the whole machine
    f32 ramTotalMiB = -1.0f;
    f32 gpuBusyPercent = -1.0f;
    f32 vramUsedMiB = -1.0f;
    f32 vramTotalMiB = -1.0f;
};

/// Busy and total time of one logical core (or of all of them), in whatever unit the platform counts.
struct CpuTimes {
    unsigned long long busy = 0;
    unsigned long long total = 0;
};

/// The CPU counters at one moment: every logical core, all of them together, and this process's CPU time.
struct CpuSnapshot {
    std::vector<CpuTimes> cores;
    CpuTimes all;
    f64 processSeconds = 0.0;
};

/// Fills the CPU fields of `out` from two snapshots `seconds` apart: the busy share of each core, of the machine,
/// and this process's share of one core. A core count that differs between the snapshots, or a counter that went
/// backwards, leaves the affected figures at 0. Pure, so every platform's reading is computed the same way.
void cpuLoadFromSnapshots(const CpuSnapshot& previous, const CpuSnapshot& now, f64 seconds, MachineMetrics& out);

/// Samples the machine. Linux: /proc and NVML or sysfs for the GPU. Windows: the processor performance counters,
/// GlobalMemoryStatusEx and NVML (nvml.dll). macOS: the Mach host and task counters (no GPU figures). Written
/// without a Windows or macOS machine to run on: only the shared computation is tested there.
MachineMetrics sampleMachineMetrics();

}  // namespace Caffeine::Debug
