#pragma once

#include "core/Types.hpp"
#include <cstring>
#include <string>
#include <vector>

namespace Caffeine::Editor::SceneSerializerIO {

inline void appendU32(std::vector<u8>& buf, u32 v) {
    const u8* p = reinterpret_cast<const u8*>(&v);
    buf.insert(buf.end(), p, p + 4);
}

inline void appendI32(std::vector<u8>& buf, i32 v) {
    appendU32(buf, static_cast<u32>(v));
}

inline void appendF32(std::vector<u8>& buf, f32 v) {
    const u8* p = reinterpret_cast<const u8*>(&v);
    buf.insert(buf.end(), p, p + 4);
}

inline void appendU8(std::vector<u8>& buf, u8 v) {
    buf.push_back(v);
}

inline void appendBytes(std::vector<u8>& buf, const void* data, usize size) {
    if (!data || size == 0) return;
    const u8* p = static_cast<const u8*>(data);
    buf.insert(buf.end(), p, p + size);
}

inline void appendString(std::vector<u8>& buf, const std::string& s) {
    const u32 len = static_cast<u32>(s.size());
    appendU32(buf, len);
    appendBytes(buf, s.data(), s.size());
}

template<typename T>
inline void appendPOD(std::vector<u8>& buf, const T& value) {
    appendBytes(buf, &value, sizeof(T));
}

inline bool readU32(const u8*& cursor, const u8* end, u32& out) {
    if (cursor + 4 > end) return false;
    memcpy(&out, cursor, 4);
    cursor += 4;
    return true;
}

inline bool readI32(const u8*& cursor, const u8* end, i32& out) {
    u32 tmp = 0;
    if (!readU32(cursor, end, tmp)) return false;
    out = static_cast<i32>(tmp);
    return true;
}

inline bool readF32(const u8*& cursor, const u8* end, f32& out) {
    if (cursor + 4 > end) return false;
    memcpy(&out, cursor, 4);
    cursor += 4;
    return true;
}

inline bool readU8(const u8*& cursor, const u8* end, u8& out) {
    if (cursor >= end) return false;
    out = *cursor++;
    return true;
}

inline bool readBytes(const u8*& cursor, const u8* end, void* dst, usize size) {
    if (cursor + size > end) return false;
    if (size > 0) memcpy(dst, cursor, size);
    cursor += size;
    return true;
}

inline bool readString(const u8*& cursor, const u8* end, std::string& out) {
    u32 len = 0;
    if (!readU32(cursor, end, len)) return false;
    if (cursor + len > end) return false;
    out.assign(reinterpret_cast<const char*>(cursor), len);
    cursor += len;
    return true;
}

template<typename T>
inline bool readPOD(const u8*& cursor, const u8* end, T& out) {
    return readBytes(cursor, end, &out, sizeof(T));
}

}  // namespace Caffeine::Editor::SceneSerializerIO
