#pragma once

#include "core/Types.hpp"

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

}  // namespace Caffeine::Debug
