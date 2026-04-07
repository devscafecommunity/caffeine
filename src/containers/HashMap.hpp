#pragma once

#include "../core/Types.hpp"
#include "Vector.hpp"

namespace Caffeine {

template<typename Key, typename Value>
class HashMap {
public:
    using Pair = struct { Key key; Value value; };

    HashMap() = default;
    explicit HashMap(usize capacity);

    Value* get(const Key& key) {
        for (auto& pair : m_data) {
            if (pair.key == key) return &pair.value;
        }
        return nullptr;
    }

    const Value* get(const Key& key) const {
        for (const auto& pair : m_data) {
            if (pair.key == key) return &pair.value;
        }
        return nullptr;
    }

    void set(const Key& key, const Value& value) {
        for (auto& pair : m_data) {
            if (pair.key == key) {
                pair.value = value;
                return;
            }
        }
        m_data.pushBack({key, value});
    }

    bool contains(const Key& key) const {
        for (const auto& pair : m_data) {
            if (pair.key == key) return true;
        }
        return false;
    }

    void remove(const Key& key) {
        for (usize i = 0; i < m_data.size(); ++i) {
            if (m_data[i].key == key) {
                m_data[i] = m_data.back();
                m_data.popBack();
                return;
            }
        }
    }

    void clear() { m_data.clear(); }
    usize size() const { return m_data.size(); }
    bool empty() const { return m_data.empty(); }

private:
    Vector<Pair> m_data;
};

template<typename Key, typename Value>
HashMap<Key, Value>::HashMap(usize capacity) {
    m_data.reserve(capacity);
}

} // namespace Caffeine