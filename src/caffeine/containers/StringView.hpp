#pragma once

#include "../core/Types.hpp"

namespace Caffeine {

class StringView {
public:
    StringView() = default;
    StringView(const char* str, usize len) : m_data(str), m_length(len) {}
    StringView(const char* str) : m_data(str), m_length(str ? calculateLength(str) : 0) {}

    const char* data() const { return m_data; }
    usize length() const { return m_length; }
    usize size() const { return m_length; }
    bool empty() const { return m_length == 0; }

    const char& operator[](usize idx) const { return m_data[idx]; }

    int compare(StringView other) const {
        usize minLen = (m_length < other.m_length) ? m_length : other.m_length;
        for (usize i = 0; i < minLen; ++i) {
            if (m_data[i] != other.m_data[i]) {
                return (m_data[i] < other.m_data[i]) ? -1 : 1;
            }
        }
        return (m_length == other.m_length) ? 0 : (m_length < other.m_length) ? -1 : 1;
    }

    bool operator==(StringView other) const { return compare(other) == 0; }
    bool operator!=(StringView other) const { return compare(other) != 0; }
    bool operator<(StringView other) const { return compare(other) < 0; }

private:
    static usize calculateLength(const char* str) {
        usize len = 0;
        while (str[len]) ++len;
        return len;
    }

    const char* m_data = nullptr;
    usize m_length = 0;
};

} // namespace Caffeine