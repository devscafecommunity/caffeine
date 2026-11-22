#pragma once

#include "../debug/Profiler.hpp"
#include "../rhi/CommandBuffer.hpp"

namespace Caffeine::Render {

/// A profiler scope for GPU work. With GPU timing off it is a plain CPU scope (cost: a timer). With it on, the
/// scope waits for the GPU at both edges, so its time is what the pass really takes (recording included).
/// Use it around work that is not inside a render or compute pass; inside one it falls back to CPU time.
class GpuProfileScope {
public:
    GpuProfileScope(RHI::CommandBuffer* cmd, const char* name)
        : m_cmd(cmd),
          m_wait(cmd && Debug::Profiler::instance().isEnabled() &&
                 Debug::Profiler::instance().gpuTiming()) {
        if (m_wait) m_cmd->flushAndWait();
        Debug::Profiler::instance().beginScope(name);
        m_name = name;
    }
    ~GpuProfileScope() {
        if (m_wait) m_cmd->flushAndWait();
        Debug::Profiler::instance().endScope(m_name);
    }
    GpuProfileScope(const GpuProfileScope&) = delete;
    GpuProfileScope& operator=(const GpuProfileScope&) = delete;

private:
    RHI::CommandBuffer* m_cmd;
    bool m_wait;
    const char* m_name = nullptr;
};

}  // namespace Caffeine::Render

#define CF_GPU_SCOPE_CAT2(a, b) a##b
#define CF_GPU_SCOPE_CAT(a, b) CF_GPU_SCOPE_CAT2(a, b)
#define CF_GPU_SCOPE(cmd, name) \
    Caffeine::Render::GpuProfileScope CF_GPU_SCOPE_CAT(_cfGpuScope_, __LINE__)((cmd), (name))
