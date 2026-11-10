#include "runtime/event_bus/event_bus.hpp"
#include <algorithm>

namespace obsidian {
namespace runtime {
namespace event_bus {

EventBus::EventBus(size_t max_queue_capacity)
    : max_queue_capacity_(max_queue_capacity) {}

SubscriptionId EventBus::Subscribe(
    uint32_t topic_id,
    EventHandler handler,
    EventPriority min_priority) {
    if (!handler) return 0;

    SubscriptionId id = next_sub_id_.fetch_add(1, std::memory_order_relaxed);

    Subscriber sub;
    sub.id = id;
    sub.topic_id = topic_id;
    sub.is_type_subscriber = false;
    sub.min_priority = min_priority;
    sub.handler = std::move(handler);

    std::unique_lock<std::shared_mutex> lock(sub_mutex_);
    subscribers_[id] = sub;
    topic_index_[topic_id].push_back(id);

    return id;
}

SubscriptionId EventBus::SubscribeType(
    EventType type,
    EventHandler handler,
    EventPriority min_priority) {
    if (!handler) return 0;

    SubscriptionId id = next_sub_id_.fetch_add(1, std::memory_order_relaxed);

    Subscriber sub;
    sub.id = id;
    sub.type_filter = type;
    sub.is_type_subscriber = true;
    sub.min_priority = min_priority;
    sub.handler = std::move(handler);

    std::unique_lock<std::shared_mutex> lock(sub_mutex_);
    subscribers_[id] = sub;
    type_index_[static_cast<uint32_t>(type)].push_back(id);

    return id;
}

bool EventBus::Unsubscribe(SubscriptionId id) {
    std::unique_lock<std::shared_mutex> lock(sub_mutex_);
    auto it = subscribers_.find(id);
    if (it == subscribers_.end()) {
        return false;
    }

    const auto& sub = it->second;
    if (sub.is_type_subscriber) {
        auto type_it = type_index_.find(static_cast<uint32_t>(sub.type_filter));
        if (type_it != type_index_.end()) {
            auto& vec = type_it->second;
            vec.erase(std::remove(vec.begin(), vec.end(), id), vec.end());
            if (vec.empty()) type_index_.erase(type_it);
        }
    } else {
        auto topic_it = topic_index_.find(sub.topic_id);
        if (topic_it != topic_index_.end()) {
            auto& vec = topic_it->second;
            vec.erase(std::remove(vec.begin(), vec.end(), id), vec.end());
            if (vec.empty()) topic_index_.erase(topic_it);
        }
    }

    subscribers_.erase(it);
    return true;
}

storage::Status EventBus::PublishSync(const Event& event) {
    Event dispatched_event = event;
    if (dispatched_event.sequence_id == 0) {
        dispatched_event.sequence_id = sequence_counter_.fetch_add(1, std::memory_order_relaxed);
    }

    published_sync_.fetch_add(1, std::memory_order_relaxed);

    std::shared_lock<std::shared_mutex> lock(sub_mutex_);

    // Dispatch to topic subscribers
    auto topic_it = topic_index_.find(dispatched_event.topic_id);
    if (topic_it != topic_index_.end()) {
        for (SubscriptionId sid : topic_it->second) {
            auto sub_it = subscribers_.find(sid);
            if (sub_it != subscribers_.end()) {
                DispatchToSubscriber(sub_it->second, dispatched_event);
            }
        }
    }

    // Dispatch to type subscribers
    auto type_it = type_index_.find(static_cast<uint32_t>(dispatched_event.type));
    if (type_it != type_index_.end()) {
        for (SubscriptionId sid : type_it->second) {
            auto sub_it = subscribers_.find(sid);
            if (sub_it != subscribers_.end()) {
                DispatchToSubscriber(sub_it->second, dispatched_event);
            }
        }
    }

    return storage::Status::Ok();
}

storage::Status EventBus::PublishAsync(Event event) {
    if (event.sequence_id == 0) {
        event.sequence_id = sequence_counter_.fetch_add(1, std::memory_order_relaxed);
    }

    std::unique_lock<std::mutex> lock(queue_mutex_);
    if (async_queue_.size() >= max_queue_capacity_) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return storage::Status::Internal("Event bus async queue capacity exceeded: dropping event");
    }

    async_queue_.push_back(std::move(event));
    published_async_.fetch_add(1, std::memory_order_relaxed);
    return storage::Status::Ok();
}

size_t EventBus::ProcessPendingEvents(size_t max_events) {
    std::vector<Event> batch;
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        if (async_queue_.empty()) {
            return 0;
        }

        size_t count_to_drain = (max_events == 0) ? async_queue_.size() : std::min(max_events, async_queue_.size());
        batch.reserve(count_to_drain);
        for (size_t i = 0; i < count_to_drain; ++i) {
            batch.push_back(std::move(async_queue_.front()));
            async_queue_.pop_front();
        }
    }

    for (const auto& evt : batch) {
        PublishSync(evt);
    }

    return batch.size();
}

void EventBus::Reset() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        async_queue_.clear();
    }
    {
        std::unique_lock<std::shared_mutex> lock(sub_mutex_);
        subscribers_.clear();
        topic_index_.clear();
        type_index_.clear();
    }
    published_sync_.store(0);
    published_async_.store(0);
    dispatched_.store(0);
    dropped_.store(0);
}

EventBusMetrics EventBus::GetMetrics() const {
    EventBusMetrics m;
    m.published_sync_count = published_sync_.load(std::memory_order_relaxed);
    m.published_async_count = published_async_.load(std::memory_order_relaxed);
    m.dispatched_count = dispatched_.load(std::memory_order_relaxed);
    m.dropped_count = dropped_.load(std::memory_order_relaxed);

    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        m.queue_depth = async_queue_.size();
    }
    {
        std::shared_lock<std::shared_mutex> lock(sub_mutex_);
        m.subscriber_count = subscribers_.size();
    }

    return m;
}

void EventBus::DispatchToSubscriber(const Subscriber& sub, const Event& event) {
    if (static_cast<uint8_t>(event.priority) < static_cast<uint8_t>(sub.min_priority)) {
        return;
    }

    try {
        sub.handler(event);
        dispatched_.fetch_add(1, std::memory_order_relaxed);
    } catch (...) {
        // Defensive isolation: subscriber exception should never break the event bus
    }
}

} // namespace event_bus
} // namespace runtime
} // namespace obsidian
