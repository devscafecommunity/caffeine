#pragma once

#include "core/Types.hpp"
#include "core/io/CafTypes.hpp"

namespace Caffeine::IO {

class CafWriter {
public:
    struct WriteResult {
        bool    success   = false;
        u64     bytesWritten = 0;
    };

    static WriteResult write(
        const char*    path,
        AssetType      type,
        u16            flags,
        const void*    metadata,
        u64            metadataSize,
        const void*    payload,
        u64            payloadSize);
};

} // namespace Caffeine::IO
