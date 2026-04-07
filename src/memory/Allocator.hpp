#pragma once

#include "../core/Types.hpp"

namespace Caffeine {

class IAllocator {
public:
    virtual ~IAllocator() = default;

    virtual void* alloc(usize size, usize alignment = 8) = 0;
    virtual void free(void* ptr) = 0;
    virtual void reset() = 0;

    virtual usize usedMemory() const = 0;
    virtual usize totalSize() const = 0;
    virtual usize peakMemory() const = 0;
    virtual usize allocationCount() const = 0;
    virtual const char* name() const = 0;
};

inline usize calculatePadding(void* ptr, usize alignment) {
    usize ptrAddr = reinterpret_cast<usize>(ptr);
    usize misalignment = ptrAddr & (alignment - 1);
    if (misalignment == 0) return 0;
    return alignment - misalignment;
}

inline usize calculateAlignedSize(usize size, usize alignment) {
    usize padding = (alignment - (size & (alignment - 1))) & (alignment - 1);
    return size + padding;
}

} // namespace Caffeine