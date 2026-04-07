#pragma once

#include "../core/Types.hpp"
#include "../core/Assertions.hpp"
#include "../memory/Allocator.hpp"
#include <utility>

namespace Caffeine {

template<typename T>
class Vector {
public:
    Vector() = default;
    
    explicit Vector(IAllocator* allocator)
        : m_allocator(allocator)
    {
    }

    Vector(usize capacity, IAllocator* allocator)
        : m_allocator(allocator)
    {
        reserve(capacity);
    }

    ~Vector() {
        if (m_data && m_ownsData) {
            if (m_allocator) {
                m_allocator->free(m_data);
            } else {
                ::operator delete(m_data);
            }
        }
    }

    Vector(const Vector& other) : m_allocator(other.m_allocator) {
        reserve(other.m_capacity);
        for (usize i = 0; i < other.m_size; ++i) {
            new (&m_data[i]) T(other.m_data[i]);
        }
        m_size = other.m_size;
    }

    Vector& operator=(const Vector& other) {
        if (this != &other) {
            clear();
            reserve(other.m_capacity);
            for (usize i = 0; i < other.m_size; ++i) {
                new (&m_data[i]) T(other.m_data[i]);
            }
            m_size = other.m_size;
        }
        return *this;
    }

    Vector(Vector&& other) noexcept
        : m_data(other.m_data)
        , m_size(other.m_size)
        , m_capacity(other.m_capacity)
        , m_allocator(other.m_allocator)
        , m_ownsData(other.m_ownsData)
    {
        other.m_data = nullptr;
        other.m_size = 0;
        other.m_capacity = 0;
        other.m_ownsData = false;
    }

    Vector& operator=(Vector&& other) noexcept {
        if (this != &other) {
            if (m_ownsData && m_data) {
                if (m_allocator) {
                    m_allocator->free(m_data);
                } else {
                    ::operator delete(m_data);
                }
            }
            m_data = other.m_data;
            m_size = other.m_size;
            m_capacity = other.m_capacity;
            m_allocator = other.m_allocator;
            m_ownsData = other.m_ownsData;
            
            other.m_data = nullptr;
            other.m_size = 0;
            other.m_capacity = 0;
            other.m_ownsData = false;
        }
        return *this;
    }

    T* data() { return m_data; }
    const T* data() const { return m_data; }

    usize size() const { return m_size; }
    usize capacity() const { return m_capacity; }
    bool empty() const { return m_size == 0; }

    T& operator[](usize idx) {
        CF_ASSERT(idx < m_size, "Index out of bounds");
        return m_data[idx];
    }

    const T& operator[](usize idx) const {
        CF_ASSERT(idx < m_size, "Index out of bounds");
        return m_data[idx];
    }

    T& at(usize idx) {
        CF_ASSERT(idx < m_size, "Index out of bounds");
        return m_data[idx];
    }

    const T& at(usize idx) const {
        CF_ASSERT(idx < m_size, "Index out of bounds");
        return m_data[idx];
    }

    T& front() {
        CF_ASSERT(m_size > 0, "Vector is empty");
        return m_data[0];
    }

    const T& front() const {
        CF_ASSERT(m_size > 0, "Vector is empty");
        return m_data[0];
    }

    T& back() {
        CF_ASSERT(m_size > 0, "Vector is empty");
        return m_data[m_size - 1];
    }

    const T& back() const {
        CF_ASSERT(m_size > 0, "Vector is empty");
        return m_data[m_size - 1];
    }

    void reserve(usize newCapacity) {
        if (newCapacity > m_capacity) {
            T* newData;
            if (m_allocator) {
                newData = static_cast<T*>(m_allocator->alloc(newCapacity * sizeof(T), alignof(T)));
            } else {
                newData = static_cast<T*>(::operator new(newCapacity * sizeof(T)));
            }
            CF_ASSERT_NOT_NULL(newData);
            
            for (usize i = 0; i < m_size; ++i) {
                new (&newData[i]) T(std::move(m_data[i]));
                m_data[i].~T();
            }
            
            if (m_ownsData && m_data) {
                if (m_allocator) {
                    m_allocator->free(m_data);
                } else {
                    ::operator delete(m_data);
                }
            }
            
            m_data = newData;
            m_capacity = newCapacity;
            m_ownsData = true;
        }
    }

    void resize(usize newSize) {
        if (newSize > m_capacity) {
            reserve(newSize);
        }
        if (newSize > m_size) {
            for (usize i = m_size; i < newSize; ++i) {
                new (&m_data[i]) T();
            }
        } else if (newSize < m_size) {
            for (usize i = newSize; i < m_size; ++i) {
                m_data[i].~T();
            }
        }
        m_size = newSize;
    }

    void clear() {
        for (usize i = 0; i < m_size; ++i) {
            m_data[i].~T();
        }
        m_size = 0;
    }

    void pushBack(const T& value) {
        if (m_size >= m_capacity) {
            usize newCapacity = (m_capacity == 0) ? 8 : m_capacity * 2;
            reserve(newCapacity);
        }
        new (&m_data[m_size]) T(value);
        ++m_size;
    }

    void pushBack(T&& value) {
        if (m_size >= m_capacity) {
            usize newCapacity = (m_capacity == 0) ? 8 : m_capacity * 2;
            reserve(newCapacity);
        }
        new (&m_data[m_size]) T(std::move(value));
        ++m_size;
    }

    template<typename... Args>
    T& emplaceBack(Args&&... args) {
        if (m_size >= m_capacity) {
            usize newCapacity = (m_capacity == 0) ? 8 : m_capacity * 2;
            reserve(newCapacity);
        }
        new (&m_data[m_size]) T(std::forward<Args>(args)...);
        return m_data[m_size++];
    }

    void popBack() {
        CF_ASSERT(m_size > 0, "Vector is empty");
        --m_size;
        m_data[m_size].~T();
    }

    T* begin() { return m_data; }
    T* end() { return m_data + m_size; }
    const T* begin() const { return m_data; }
    const T* end() const { return m_data + m_size; }

private:
    T* m_data = nullptr;
    usize m_size = 0;
    usize m_capacity = 0;
    IAllocator* m_allocator = nullptr;
    bool m_ownsData = false;
};

} // namespace Caffeine