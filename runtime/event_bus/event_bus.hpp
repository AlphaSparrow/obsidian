#pragma once

#include "runtime/event_bus/event.hpp"
#include "storage/common/status.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
#include <unordered_map>
#include <deque>
#include <mutex>
#include <shared_mutex>
#include <atomic>

namespace obsidian {
namespace runtime {
namespace event_bus {

using EventHandler = std::function<void(const Event&)>;
using SubscriptionId = uint64_t;

struct EventBusMetrics {
    uint64_t published_sync_count{0};
    uint64_t published_async_count{0};
    uint64_t dispatched_count{0};
    uint64_t dropped_count{0};
    size_t queue_depth{0};
    size_t subscriber_count{0};
};

class EventBus {
public:
    explicit EventBus(size_t max_queue_capacity = 65536);
    ~EventBus() = default;

    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    // Subscribe to a specific topic ID
    SubscriptionId Subscribe(
        uint32_t topic_id,
        EventHandler handler,
        EventPriority min_priority = EventPriority::Low);

    // Subscribe to all events of a specific EventType across all topics
    SubscriptionId SubscribeType(
        EventType type,
        EventHandler handler,
        EventPriority min_priority = EventPriority::Low);

    // Unsubscribe by subscription handle
    bool Unsubscribe(SubscriptionId id);

    // Synchronously dispatch an event directly to all matching subscribers
    storage::Status PublishSync(const Event& event);

    // Enqueue an event for asynchronous worker dispatch
    storage::Status PublishAsync(Event event);

    // Drains and processes up to max_events from the async queue (0 = drain all)
    size_t ProcessPendingEvents(size_t max_events = 0);

    // Clears all pending events and subscriptions
    void Reset();

    // Returns a snapshot of event bus telemetry
    [[nodiscard]] EventBusMetrics GetMetrics() const;

private:
    struct Subscriber {
        SubscriptionId id{0};
        uint32_t topic_id{0};
        EventType type_filter{EventType::Unknown};
        bool is_type_subscriber{false};
        EventPriority min_priority{EventPriority::Low};
        EventHandler handler;
    };

    size_t max_queue_capacity_{65536};
    std::atomic<uint64_t> next_sub_id_{1};
    std::atomic<uint64_t> sequence_counter_{1};

    mutable std::shared_mutex sub_mutex_;
    std::unordered_map<SubscriptionId, Subscriber> subscribers_;
    std::unordered_map<uint32_t, std::vector<SubscriptionId>> topic_index_;
    std::unordered_map<uint32_t, std::vector<SubscriptionId>> type_index_;

    mutable std::mutex queue_mutex_;
    std::deque<Event> async_queue_;

    // Telemetry counters
    std::atomic<uint64_t> published_sync_{0};
    std::atomic<uint64_t> published_async_{0};
    std::atomic<uint64_t> dispatched_{0};
    std::atomic<uint64_t> dropped_{0};

    void DispatchToSubscriber(const Subscriber& sub, const Event& event);
};

} // namespace event_bus
} // namespace runtime
} // namespace obsidian
