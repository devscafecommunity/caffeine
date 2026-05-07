#include "core/io/BlobLoader.hpp"
#include "core/io/Crc32.hpp"
#include "core/Assertions.hpp"

#include <cstdio>
#include <cstring>

namespace Caffeine::IO {

BlobLoader::LoadResult BlobLoader::load(const char* path, IAllocator* allocator) {
    CF_ASSERT(path != nullptr,      "BlobLoader::load: path is null");
    CF_ASSERT(allocator != nullptr, "BlobLoader::load: allocator is null");

    LoadResult result{};

    std::FILE* file = std::fopen(path, "rb");
    if (!file) return result;

    std::fseek(file, 0, SEEK_END);
    const u64 fileSize = static_cast<u64>(std::ftell(file));
    std::rewind(file);

    constexpr u64 kMinFileSize =
        CafHeader::kHeaderSize + CafHeader::kFooterSize;

    if (fileSize < kMinFileSize) {
        std::fclose(file);
        return result;
    }

    void* buf = allocator->alloc(fileSize, 16);
    if (!buf) {
        std::fclose(file);
        return result;
    }

    if (std::fread(buf, 1, fileSize, file) != fileSize) {
        std::fclose(file);
        return result;
    }
    std::fclose(file);

    const auto* header = static_cast<const CafHeader*>(buf);
    const auto* bytes  = static_cast<const u8*>(buf);

    if (header->magic != CafHeader::kMagic)
        return result;

    if (header->versionMajor != CafHeader::kVersionMajor)
        return result;

    if (fileSize != header->totalFileSize())
        return result;

    const u8* payloadPtr =
        bytes + CafHeader::kHeaderSize + header->metadataSize;

    if (!verifyCRC32(payloadPtr, header->dataSize, header->crc32))
        return result;

    const u32 footerCrc  = *reinterpret_cast<const u32*>(bytes + fileSize - CafHeader::kFooterSize);
    const u32 computedCrc = Caffeine::IO::crc32(buf, fileSize - CafHeader::kFooterSize);
    if (footerCrc != computedCrc)
        return result;

    result.header   = header;
    result.metadata = bytes + CafHeader::kHeaderSize;
    result.payload  = payloadPtr;
    result.valid    = true;
    return result;
}

bool BlobLoader::validateHeader(const char* path) {
    CF_ASSERT(path != nullptr, "BlobLoader::validateHeader: path is null");

    std::FILE* file = std::fopen(path, "rb");
    if (!file) return false;

    CafHeader header{};
    const bool ok = (std::fread(&header, sizeof(CafHeader), 1, file) == 1);
    std::fclose(file);

    return ok
        && header.magic        == CafHeader::kMagic
        && header.versionMajor == CafHeader::kVersionMajor;
}

bool BlobLoader::verifyCRC32(const void* data, u64 size, u32 expected) {
    return Caffeine::IO::crc32(data, size) == expected;
}

} // namespace Caffeine::IO
