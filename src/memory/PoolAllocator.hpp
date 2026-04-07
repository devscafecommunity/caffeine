#pragma once

#include "Allocator.hpp"
#include "../core/Assertions.hpp"

namespace Caffeine {

class PoolAllocator final : public IAllocator {
public:
    PoolAllocator(void* buffer, usize poolSize, usize slotSize, usize alignment = 8)
        : m_poolStart(static_cast<u8*>(buffer))
        , m_poolSize(poolSize)
        , m_slotSize(slotSize)
        , m_alignment(alignment)
    {
        CF_ASSERT(alignment >= 8, "Alignment must be at least 8");
        CF_ASSERT((alignment & (alignment - 1)) == 0, "Alignment must be power of 2");
        
        m_maxSlots = poolSize / slotSize;
        initializeFreeList();
    }

    explicit PoolAllocator(usize poolSize, usize slotSize, usize alignment = 8)
        : PoolAllocator(new u8[poolSize], poolSize, slotSize, alignment)
    {
        m_ownsBuffer = true;
    }

    ~PoolAllocator() override {
        if (m_ownsBuffer) {
            delete[] m_poolStart;
        }
    }

    void* alloc(usize size, usize alignment = 8) override {
        CF_ASSERT(size <= m_slotSize, "Requested size exceeds slot size");
        
        if (!m_freeList) {
            return nullptr;
        }

        void* ptr = m_freeList;
        m_freeList = *reinterpret_cast<u8**>(ptr);
        ++m_usedCount;
        
        if (m_usedCount > m_peakSlots) m_peakSlots = m_usedCount;

        return ptr;
    }

    void free(void* ptr) override {
        if (!ptr) return;

        *reinterpret_cast<u8**>(ptr) = m_freeList;
        m_freeList = static_cast<u8*>(ptr);
        --m_usedCount;
    }

    void reset() override {
        initializeFreeList();
        m_usedCount = 0;
    }

    usize usedMemory() const override { return m_usedCount * m_slotSize; }
    usize totalSize() const override { return m_poolSize; }
    usize peakMemory() const override { return m_peakSlots * m_slotSize; }
    usize allocationCount() const override { return m_usedCount; }
    const char* name() const override { return "Pool"; }

    usize freeSlots() const { return m_maxSlots - m_usedCount; }
    usize slotSize() const { return m_slotSize; }
    usize maxSlots() const { return m_maxSlots; }

private:
    void initializeFreeList() {
        m_freeList = nullptr;
        for (usize i = 0; i < m_maxSlots; ++i) {
            u8* slot = m_poolStart + (i * m_slotSize);
            *reinterpret_cast<u8**>(slot) = m_freeList;
            m_freeList = slot;
        }
    }

    u8* m_freeList = nullptr;
    u8* m_poolStart = nullptr;
    usize m_poolSize = 0;
    usize m_slotSize = 0;
    usize m_alignment = 8;
    usize m_maxSlots = 0;
    usize m_usedCount = 0;
    usize m_peakSlots = 0;
    bool m_ownsBuffer = false;
};

} // namespace Caffeine