// ============================================================================
// @file    JobSystem.hpp
// @brief   Job System with work-stealing thread pool (RF2.2-RF2.6)
// @note    Part of threading/ module — namespace Caffeine::Threading
// ============================================================================
#pragma once

#include "../core/Types.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace Caffeine::Threading {

using namespace Caffeine;

// ============================================================================
// JobPriority
// ============================================================================

enum class JobPriority : u8 {
    Critical   = 0,
    Normal     = 1,
    Background = 2
};

constexpr u32 PRIORITY_COUNT = 3;

// ============================================================================
// IJob / JobWithData<T>
// ============================================================================

struct IJob {
    virtual ~IJob() = default;
    virtual void execute() = 0;
    virtual JobPriority priority() const { return JobPriority::Normal; }
};

template<typename DataT>
struct JobWithData : IJob {
    DataT                       data;
    std::function<void(DataT&)> func;
    JobPriority                 prio = JobPriority::Normal;

    void execute() override { func(data); }
    JobPriority priority() const override { return prio; }
};

// ============================================================================
// JobHandle — index + version to avoid ABA problem
// ============================================================================

class JobHandle {
public:
    JobHandle() = default;
    explicit JobHandle(u32 index, u32 version, std::atomic<u32>* completionFlag);

    void wait() const;
    bool isComplete() const;
    explicit operator bool() const { return isComplete(); }

private:
    u32                 m_index   = ~u32(0);
    u32                 m_version = ~u32(0);
    std::atomic<u32>*   m_completionFlag = nullptr;
};

// ============================================================================
// JobBarrier — wait() blocks until N jobs release()
// ============================================================================

class JobBarrier {
public:
    explicit JobBarrier(u32 targetCount = 0);

    void add();
    void release();
    void wait();

private:
    std::atomic<u32>        m_count{0};
    std::mutex              m_mutex;
    std::condition_variable m_cv;
};

// ============================================================================
// JobSystem
// ============================================================================

class JobSystem {
public:
    explicit JobSystem(u32 workerCount = 0);
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    JobHandle schedule(std::unique_ptr<IJob> job,
                       JobBarrier*           barrier = nullptr,
                       JobPriority           prio    = JobPriority::Normal);

    template<typename DataT, typename FuncT>
    JobHandle scheduleData(DataT       data,
                           FuncT&&     func,
                           JobBarrier* barrier = nullptr,
                           JobPriority prio    = JobPriority::Normal) {
        auto job  = std::make_unique<JobWithData<DataT>>();
        job->data = std::move(data);
        job->func = std::forward<FuncT>(func);
        job->prio = prio;
        return schedule(std::move(job), barrier, prio);
    }

    template<typename FuncT>
    JobHandle scheduleParallelFor(u32         count,
                                  FuncT&&     func,
                                  JobBarrier* barrier = nullptr,
                                  JobPriority prio    = JobPriority::Normal);

    void waitAll();
    u32  workerCount() const { return m_workerCount; }

    struct Stats {
        u32 activeWorkers;
        u32 pendingJobs;
        u64 completedJobsTotal;
        f64 avgJobMs;
    };
    Stats stats() const;

private:
    struct JobEntry {
        std::unique_ptr<IJob> job;
        JobBarrier*           barrier = nullptr;
        u32                   slotIndex = 0;
        u32                   slotVersion = 0;
    };

    struct WorkStealDeque {
        std::mutex              mutex;
        std::deque<JobEntry>    jobs;

        void push(JobEntry&& entry);
        bool pop(JobEntry& out);
        bool steal(JobEntry& out);
        bool empty() const;
        u32  size() const;
    };

    struct CompletionSlot {
        std::atomic<u32> flag{0};
        std::atomic<u32> version{0};
    };

    void workerMain(u32 workerIndex);
    bool tryExecuteOne(u32 workerIndex);
    void pushToQueue(JobEntry&& entry, JobPriority prio, u32 workerHint);
    std::pair<u32, u32> allocateSlot();

    u32                             m_workerCount = 0;
    std::vector<std::thread>        m_workers;
    std::atomic<bool>               m_running{false};
    std::atomic<u32>                m_activeWorkers{0};

    // Per-worker deques (one set of 3 priority queues per worker)
    // Layout: m_localQueues[workerIndex * PRIORITY_COUNT + priorityLevel]
    std::vector<std::unique_ptr<WorkStealDeque>> m_localQueues;

    // Global overflow queues (one per priority level)
    WorkStealDeque                  m_globalQueues[PRIORITY_COUNT];

    std::mutex                      m_wakeMutex;
    std::condition_variable         m_wakeCV;

    // Completion tracking
    static constexpr u32            MAX_SLOTS = 4096;
    CompletionSlot                  m_slots[MAX_SLOTS];
    std::atomic<u32>                m_nextSlot{0};

    // Stats
    std::atomic<u64>                m_completedTotal{0};
    std::atomic<u64>                m_totalJobTimeNs{0};

    // Pending job count for waitAll
    std::atomic<u32>                m_pendingJobs{0};
    std::mutex                      m_waitAllMutex;
    std::condition_variable         m_waitAllCV;
};

// ============================================================================
// scheduleParallelFor — template implementation
// ============================================================================

template<typename FuncT>
JobHandle JobSystem::scheduleParallelFor(u32         count,
                                         FuncT&&     func,
                                         JobBarrier* barrier,
                                         JobPriority prio) {
    if (count == 0) {
        auto [idx, ver] = allocateSlot();
        m_slots[idx].flag.store(1, std::memory_order_release);
        return JobHandle(idx, ver, &m_slots[idx].flag);
    }

    u32 chunkCount = m_workerCount > 0 ? m_workerCount : 1;
    if (chunkCount > count) chunkCount = count;
    u32 chunkSize = count / chunkCount;
    u32 remainder = count % chunkCount;

    auto [idx, ver] = allocateSlot();

    struct ParallelChunk {
        u32 begin;
        u32 end;
    };

    std::atomic<u32>* chunkCounter = new std::atomic<u32>(chunkCount);
    auto* slotFlag = &m_slots[idx].flag;

    if (barrier) {
        barrier->add();  // one add per parallelFor
        // We already have chunkCount sub-jobs; barrier tracks the parallelFor as 1 unit
    }

    u32 offset = 0;
    for (u32 c = 0; c < chunkCount; ++c) {
        u32 begin = offset;
        u32 end   = begin + chunkSize + (c < remainder ? 1 : 0);
        offset = end;

        // Capture func by value for each chunk
        auto chunkFunc = func;  // copy the callable
        auto chunkJob = std::make_unique<JobWithData<ParallelChunk>>();
        chunkJob->data = ParallelChunk{begin, end};
        chunkJob->prio = prio;
        chunkJob->func = [chunkFunc, chunkCounter, slotFlag, barrier](ParallelChunk& chunk) mutable {
            for (u32 i = chunk.begin; i < chunk.end; ++i) {
                chunkFunc(i);
            }
            u32 prev = chunkCounter->fetch_sub(1, std::memory_order_acq_rel);
            if (prev == 1) {
                // Last chunk done
                slotFlag->store(1, std::memory_order_release);
                if (barrier) barrier->release();
                delete chunkCounter;
            }
        };

        // Schedule chunk but don't attach to barrier directly (handled above)
        schedule(std::move(chunkJob), nullptr, prio);
    }

    return JobHandle(idx, ver, slotFlag);
}

}  // namespace Caffeine::Threading
