#pragma once

#include "core/Types.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace Caffeine::Events {

using ListenerHandle = u32;

namespace Detail {

template<typename T>
u32 eventTypeId() {
    static char s_sentinel = 0;
    return static_cast<u32>(reinterpret_cast<uintptr_t>(&s_sentinel));
}

struct ListenerRecord {
    ListenerHandle                   handle;
    std::function<void(const void*)> callback;
};

struct IEventWrapper {
    u32 typeId;
    explicit IEventWrapper(u32 id) : typeId(id) {}
    virtual ~IEventWrapper() = default;
    virtual void dispatchTo(const std::vector<ListenerRecord>& listeners) const = 0;
};

template<typename T>
struct EventWrapper final : IEventWrapper {
    T event;
    EventWrapper(u32 id, T e) : IEventWrapper(id), event(std::move(e)) {}
    void dispatchTo(const std::vector<ListenerRecord>& listeners) const override {
        for (const auto& record : listeners) {
            record.callback(static_cast<const void*>(&event));
        }
    }
};

} // namespace Detail

class EventBus {
public:
    EventBus() = default;
    ~EventBus() = default;

    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    template<typename T>
    ListenerHandle subscribe(std::function<void(const T&)> callback) {
        const u32 typeId = Detail::eventTypeId<T>();
        const ListenerHandle handle = m_nextHandle++;

        Detail::ListenerRecord record{
            handle,
            [cb = std::move(callback)](const void* data) {
                cb(*static_cast<const T*>(data));
            }
        };

        m_listeners[typeId].push_back(std::move(record));
        return handle;
    }

    void unsubscribe(ListenerHandle handle);

    template<typename T>
    void publish(const T& event) {
        const u32 typeId = Detail::eventTypeId<T>();
        auto it = m_listeners.find(typeId);
        if (it == m_listeners.end()) return;

        std::vector<Detail::ListenerRecord> snapshot = it->second;
        for (const auto& record : snapshot) {
            record.callback(static_cast<const void*>(&event));
        }
    }

    template<typename T>
    void publishDeferred(T event) {
        const u32 typeId = Detail::eventTypeId<T>();
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_queue.push_back(
            std::make_unique<Detail::EventWrapper<T>>(typeId, std::move(event)));
    }

    void dispatch();

    usize pendingCount() const;

    void clear();

private:
    std::unordered_map<u32, std::vector<Detail::ListenerRecord>> m_listeners;

    std::vector<std::unique_ptr<Detail::IEventWrapper>> m_queue;
    mutable std::mutex m_queueMutex;

    ListenerHandle m_nextHandle{1};
};

} // namespace Caffeine::Events
