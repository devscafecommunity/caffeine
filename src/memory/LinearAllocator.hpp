#pragma once

#include "Allocator.hpp"
#include "../core/Assertions.hpp"

namespace Caffeine {

class LinearAllocator final : public IAllocator {
public:
    explicit LinearAllocator(usize size)
        : m_start(new u8[size])
        , m_end(m_start + size)
        , m_cursor(m_start)
        , m_ownsBuffer(true)
    {
    }

    LinearAllocator(void* buffer, usize size)
        : m_start(static_cast<u8*>(buffer))
        , m_end(m_start + size)
        , m_cursor(m_start)
        , m_ownsBuffer(false)
    {
    }

    ~LinearAllocator() override {
        if (m_ownsBuffer && m_start) {
            delete[] m_start;
        }
    }

    void* alloc(usize size, usize alignment = 8) override {
        usize padding = calculatePadding(m_cursor, alignment);
        usize totalSize = size + padding;

        if (m_cursor + totalSize > m_end) {
            return nullptr;
        }

        void* ptr = m_cursor + padding;
        m_cursor += totalSize;

        usize used = usedMemory();
        if (used > m_peak) m_peak = used;
        ++m_allocCount;

        return ptr;
    }

    void free(void* ptr) override {
    }

    void reset() override {
        m_cursor = m_start;
        m_allocCount = 0;
    }

    usize usedMemory() const override { return m_cursor - m_start; }
    usize totalSize() const override { return m_end - m_start; }
    usize peakMemory() const override { return m_peak; }
    usize allocationCount() const override { return m_allocCount; }
    const char* name() const override { return "Linear"; }

    usize availableMemory() const { return m_end - m_cursor; }
    void* currentPointer() const { return m_cursor; }

private:
    u8* m_start;
    u8* m_end;
    u8* m_cursor;
    usize m_peak = 0;
    usize m_allocCount = 0;
    bool m_ownsBuffer = false;
};

} // namespace Caffeine