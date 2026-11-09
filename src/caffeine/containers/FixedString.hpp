#pragma once

#include "../core/Types.hpp"

namespace Caffeine {

template<usize N>
class FixedString {
public:
    FixedString() = default;

    FixedString(const char* str) {
        if (str) {
            usize len = 0;
            while (str[len] && len < N - 1) {
                m_data[len] = str[len];
                ++len;
            }
            m_data[len] = '\0';
            m_length = len;
        }
    }

    const char* cStr() const { return m_data; }
    char* data() { return m_data; }
    const char* data() const { return m_data; }

    usize length() const { return m_length; }
    usize size() const { return m_length; }
    usize capacity() const { return N; }
    bool empty() const { return m_length == 0; }
    bool full() const { return m_length >= N - 1; }

    void clear() {
        m_data[0] = '\0';
        m_length = 0;
    }

    void append(const char* str) {
        if (!str) return;
        while (*str && m_length < N - 1) {
            m_data[m_length++] = *str++;
        }
        m_data[m_length] = '\0';
    }

    void append(char c) {
        if (m_length < N - 1) {
            m_data[m_length++] = c;
            m_data[m_length] = '\0';
        }
    }

    bool operator==(const FixedString& other) const {
        if (m_length != other.m_length) return false;
        for (usize i = 0; i < m_length; ++i) {
            if (m_data[i] != other.m_data[i]) return false;
        }
        return true;
    }

    bool operator!=(const FixedString& other) const {
        return !(*this == other);
    }

    char& operator[](usize idx) { return m_data[idx]; }
    const char& operator[](usize idx) const { return m_data[idx]; }

private:
    char m_data[N] = {};
    usize m_length = 0;
};

} // namespace Caffeine