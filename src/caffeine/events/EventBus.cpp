#include "events/EventBus.hpp"

#include <algorithm>

namespace Caffeine::Events {

void EventBus::unsubscribe(ListenerHandle handle) {
    for (auto& [typeId, records] : m_listeners) {
        auto it = std::find_if(records.begin(), records.end(),
            [handle](const Detail::ListenerRecord& r) { return r.handle == handle; });
        if (it != records.end()) {
            records.erase(it);
            return;
        }
    }
}

void EventBus::dispatch() {
    std::vector<std::unique_ptr<Detail::IEventWrapper>> batch;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        batch.swap(m_queue);
    }

    for (const auto& wrapper : batch) {
        auto it = m_listeners.find(wrapper->typeId);
        if (it != m_listeners.end()) {
            wrapper->dispatchTo(it->second);
        }
    }
}

usize EventBus::pendingCount() const {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    return m_queue.size();
}

void EventBus::clear() {
    m_listeners.clear();
    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_queue.clear();
}

} // namespace Caffeine::Events
