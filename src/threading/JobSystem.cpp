// ============================================================================
// @file    JobSystem.cpp
// @brief   Job System implementation — work-stealing thread pool
// @note    Part of threading/ module
// ============================================================================

#include "threading/JobSystem.hpp"

#include <algorithm>
#include <chrono>
#include <random>

namespace Caffeine::Threading {

// ============================================================================
// JobHandle
// ============================================================================

JobHandle::JobHandle(u32 index, u32 version, std::atomic<u32>* completionFlag)
    : m_index(index)
    , m_version(version)
    , m_completionFlag(completionFlag) {}

void JobHandle::wait() const {
    if (!m_completionFlag) return;
    while (m_completionFlag->load(std::memory_order_acquire) == 0) {
        std::this_thread::yield();
    }
}

bool JobHandle::isComplete() const {
    if (!m_completionFlag) return false;
    return m_completionFlag->load(std::memory_order_acquire) != 0;
}

// ============================================================================
// JobBarrier
// ============================================================================

JobBarrier::JobBarrier(u32 targetCount)
    : m_count(targetCount) {}

void JobBarrier::add() {
    m_count.fetch_add(1, std::memory_order_acq_rel);
}

void JobBarrier::release() {
    u32 prev = m_count.fetch_sub(1, std::memory_order_acq_rel);
    if (prev == 1) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_cv.notify_all();
    }
}

void JobBarrier::wait() {
    if (m_count.load(std::memory_order_acquire) == 0) return;
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait(lock, [this]() {
        return m_count.load(std::memory_order_acquire) == 0;
    });
}

// ============================================================================
// WorkStealDeque
// ============================================================================

void JobSystem::WorkStealDeque::push(JobEntry&& entry) {
    std::lock_guard<std::mutex> lock(mutex);
    jobs.push_front(std::move(entry));
}

bool JobSystem::WorkStealDeque::pop(JobEntry& out) {
    std::lock_guard<std::mutex> lock(mutex);
    if (jobs.empty()) return false;
    out = std::move(jobs.front());
    jobs.pop_front();
    return true;
}

bool JobSystem::WorkStealDeque::steal(JobEntry& out) {
    std::lock_guard<std::mutex> lock(mutex);
    if (jobs.empty()) return false;
    out = std::move(jobs.back());
    jobs.pop_back();
    return true;
}

bool JobSystem::WorkStealDeque::empty() const {
    return jobs.empty();
}

u32 JobSystem::WorkStealDeque::size() const {
    return static_cast<u32>(jobs.size());
}

// ============================================================================
// JobSystem
// ============================================================================

JobSystem::JobSystem(u32 workerCount) {
    if (workerCount == 0) {
        u32 hw = std::thread::hardware_concurrency();
        m_workerCount = (hw > 1) ? (hw - 1) : 1;
    } else {
        m_workerCount = workerCount;
    }

    m_localQueues.reserve(static_cast<usize>(m_workerCount) * PRIORITY_COUNT);
    for (usize i = 0; i < static_cast<usize>(m_workerCount) * PRIORITY_COUNT; ++i) {
        m_localQueues.push_back(std::make_unique<WorkStealDeque>());
    }

    m_running.store(true, std::memory_order_release);

    m_workers.reserve(m_workerCount);
    for (u32 i = 0; i < m_workerCount; ++i) {
        m_workers.emplace_back(&JobSystem::workerMain, this, i);
    }
}

JobSystem::~JobSystem() {
    waitAll();
    m_running.store(false, std::memory_order_release);
    m_wakeCV.notify_all();

    for (auto& t : m_workers) {
        if (t.joinable()) t.join();
    }
}

std::pair<u32, u32> JobSystem::allocateSlot() {
    u32 idx = m_nextSlot.fetch_add(1, std::memory_order_relaxed) % MAX_SLOTS;
    u32 ver = m_slots[idx].version.fetch_add(1, std::memory_order_relaxed) + 1;
    m_slots[idx].flag.store(0, std::memory_order_release);
    return {idx, ver};
}

JobHandle JobSystem::schedule(std::unique_ptr<IJob> job,
                              JobBarrier*           barrier,
                              JobPriority           prio) {
    auto [idx, ver] = allocateSlot();

    JobEntry entry;
    entry.job         = std::move(job);
    entry.barrier     = barrier;
    entry.slotIndex   = idx;
    entry.slotVersion = ver;

    m_pendingJobs.fetch_add(1, std::memory_order_acq_rel);

    u32 prioIdx = static_cast<u32>(prio);
    m_globalQueues[prioIdx].push(std::move(entry));

    m_wakeCV.notify_one();

    return JobHandle(idx, ver, &m_slots[idx].flag);
}

void JobSystem::pushToQueue(JobEntry&& entry, JobPriority prio, u32 workerHint) {
    u32 prioIdx = static_cast<u32>(prio);
    if (workerHint < m_workerCount) {
        m_localQueues[static_cast<usize>(workerHint) * PRIORITY_COUNT + prioIdx]->push(std::move(entry));
    } else {
        m_globalQueues[prioIdx].push(std::move(entry));
    }
}

bool JobSystem::tryExecuteOne(u32 workerIndex) {
    JobEntry entry;

    // 1. Try local queues in priority order (Critical first)
    for (u32 p = 0; p < PRIORITY_COUNT; ++p) {
        auto& localQ = *m_localQueues[static_cast<usize>(workerIndex) * PRIORITY_COUNT + p];
        if (localQ.pop(entry)) goto execute;
    }

    // 2. Try global queues in priority order
    for (u32 p = 0; p < PRIORITY_COUNT; ++p) {
        if (m_globalQueues[p].pop(entry)) goto execute;
    }

    // 3. Work-stealing: try to steal from other workers (priority order)
    for (u32 p = 0; p < PRIORITY_COUNT; ++p) {
        for (u32 w = 1; w <= m_workerCount; ++w) {
            u32 victim = (workerIndex + w) % m_workerCount;
            auto& victimQ = *m_localQueues[static_cast<usize>(victim) * PRIORITY_COUNT + p];
            if (victimQ.steal(entry)) goto execute;
        }
    }

    return false;

execute:
    m_activeWorkers.fetch_add(1, std::memory_order_relaxed);

    auto startTime = std::chrono::high_resolution_clock::now();

    entry.job->execute();

    auto endTime = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime).count();
    m_totalJobTimeNs.fetch_add(static_cast<u64>(elapsed), std::memory_order_relaxed);

    m_slots[entry.slotIndex].flag.store(1, std::memory_order_release);

    if (entry.barrier) {
        entry.barrier->release();
    }

    m_completedTotal.fetch_add(1, std::memory_order_relaxed);
    m_activeWorkers.fetch_sub(1, std::memory_order_relaxed);

    u32 prev = m_pendingJobs.fetch_sub(1, std::memory_order_acq_rel);
    if (prev == 1) {
        std::lock_guard<std::mutex> lock(m_waitAllMutex);
        m_waitAllCV.notify_all();
    }

    return true;
}

void JobSystem::workerMain(u32 workerIndex) {
    while (m_running.load(std::memory_order_acquire)) {
        if (!tryExecuteOne(workerIndex)) {
            std::unique_lock<std::mutex> lock(m_wakeMutex);
            m_wakeCV.wait_for(lock, std::chrono::milliseconds(1), [this]() {
                return !m_running.load(std::memory_order_acquire) ||
                       m_pendingJobs.load(std::memory_order_acquire) > 0;
            });
        }
    }

    // Drain remaining jobs on shutdown
    while (tryExecuteOne(workerIndex)) {}
}

void JobSystem::waitAll() {
    if (m_pendingJobs.load(std::memory_order_acquire) == 0) return;
    std::unique_lock<std::mutex> lock(m_waitAllMutex);
    m_waitAllCV.wait(lock, [this]() {
        return m_pendingJobs.load(std::memory_order_acquire) == 0;
    });
}

JobSystem::Stats JobSystem::stats() const {
    Stats s;
    s.activeWorkers     = m_activeWorkers.load(std::memory_order_relaxed);
    s.completedJobsTotal = m_completedTotal.load(std::memory_order_relaxed);

    u32 pending = 0;
    for (u32 p = 0; p < PRIORITY_COUNT; ++p) {
        pending += m_globalQueues[p].size();
    }
    for (u32 w = 0; w < m_workerCount; ++w) {
        for (u32 p = 0; p < PRIORITY_COUNT; ++p) {
            pending += m_localQueues[static_cast<usize>(w) * PRIORITY_COUNT + p]->size();
        }
    }
    s.pendingJobs = pending;

    u64 completed = s.completedJobsTotal;
    if (completed > 0) {
        u64 totalNs = m_totalJobTimeNs.load(std::memory_order_relaxed);
        s.avgJobMs = static_cast<f64>(totalNs) / static_cast<f64>(completed) / 1e6;
    } else {
        s.avgJobMs = 0.0;
    }

    return s;
}

}  // namespace Caffeine::Threading
