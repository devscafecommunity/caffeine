#include "core/io/CafWriter.hpp"
#include "core/io/Crc32.hpp"

#include <cstdio>
#include <cstring>

namespace Caffeine::IO {

CafWriter::WriteResult CafWriter::write(
    const char* path,
    AssetType   type,
    u16         flags,
    const void* metadata,
    u64         metadataSize,
    const void* payload,
    u64         payloadSize)
{
    WriteResult result{};

    std::FILE* file = std::fopen(path, "wb");
    if (!file) return result;

    const u32 payloadCrc = crc32(payload, payloadSize);

    CafHeader header{};
    header.magic        = CafHeader::kMagic;
    header.versionMajor = CafHeader::kVersionMajor;
    header.versionMinor = CafHeader::kVersionMinor;
    header.type         = type;
    header.flags        = flags;
    header.crc32        = payloadCrc;
    header.metadataSize = metadataSize;
    header.dataSize     = payloadSize;

    if (std::fwrite(&header, sizeof(CafHeader), 1, file) != 1) {
        std::fclose(file);
        return result;
    }

    if (metadataSize > 0 && metadata != nullptr) {
        if (std::fwrite(metadata, 1, metadataSize, file) != metadataSize) {
            std::fclose(file);
            return result;
        }
    }

    if (payloadSize > 0 && payload != nullptr) {
        if (std::fwrite(payload, 1, payloadSize, file) != payloadSize) {
            std::fclose(file);
            return result;
        }
    }

    u32 wholeCrc = crc32(&header, sizeof(CafHeader));
    if (metadataSize > 0 && metadata != nullptr)
        wholeCrc = crc32Continue(wholeCrc, metadata, metadataSize);
    if (payloadSize > 0 && payload != nullptr)
        wholeCrc = crc32Continue(wholeCrc, payload, payloadSize);

    if (std::fwrite(&wholeCrc, sizeof(u32), 1, file) != 1) {
        std::fclose(file);
        return result;
    }

    result.bytesWritten = header.totalFileSize();
    result.success      = true;
    std::fclose(file);
    return result;
}

} // namespace Caffeine::IO
