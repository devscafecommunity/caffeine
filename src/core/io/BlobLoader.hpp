#pragma once

#include "core/Types.hpp"
#include "core/io/CafTypes.hpp"
#include "memory/Allocator.hpp"

namespace Caffeine::IO {

class BlobLoader {
public:
    struct LoadResult {
        const CafHeader* header   = nullptr;
        const void*      metadata = nullptr;
        const void*      payload  = nullptr;
        bool             valid    = false;
    };

    static LoadResult load(const char* path, IAllocator* allocator);
    static bool       validateHeader(const char* path);

private:
    static bool verifyCRC32(const void* data, u64 size, u32 expected);
};

} // namespace Caffeine::IO
