#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <span>

namespace obsidian {
namespace runtime {
namespace event_bus {

enum class EventType : uint32_t {
    Unknown = 0,
    MarketTick = 1,
    OrderBookUpdate = 2,
    TradeExecution = 3,
    SignalGenerated = 4,
    OrderSubmitted = 5,
    OrderFilled = 6,
    OrderCancelled = 7,
    RiskViolation = 8,
    BarClosed = 9,
    SystemAlert = 10,
    Custom = 100
};

inline const char* EventTypeToString(EventType type) {
    switch (type) {
        case EventType::MarketTick: return "MarketTick";
        case EventType::OrderBookUpdate: return "OrderBookUpdate";
        case EventType::TradeExecution: return "TradeExecution";
        case EventType::SignalGenerated: return "SignalGenerated";
        case EventType::OrderSubmitted: return "OrderSubmitted";
        case EventType::OrderFilled: return "OrderFilled";
        case EventType::OrderCancelled: return "OrderCancelled";
        case EventType::RiskViolation: return "RiskViolation";
        case EventType::BarClosed: return "BarClosed";
        case EventType::SystemAlert: return "SystemAlert";
        case EventType::Custom: return "Custom";
        default: return "Unknown";
    }
}

enum class EventPriority : uint8_t {
    Low = 0,
    Normal = 1,
    High = 2,
    Critical = 3
};

struct Event {
    int64_t timestamp_ns{0};
    uint32_t topic_id{0};
    EventType type{EventType::Unknown};
    EventPriority priority{EventPriority::Normal};
    uint64_t sequence_id{0};
    std::vector<uint8_t> payload;

    template <typename T>
    static Event Create(uint32_t topic_id, EventType type, const T& data, EventPriority priority = EventPriority::Normal, int64_t ts_ns = 0) {
        Event evt;
        evt.timestamp_ns = ts_ns;
        evt.topic_id = topic_id;
        evt.type = type;
        evt.priority = priority;
        evt.payload.resize(sizeof(T));
        std::memcpy(evt.payload.data(), &data, sizeof(T));
        return evt;
    }

    template <typename T>
    const T* As() const {
        if (payload.size() < sizeof(T)) return nullptr;
        return reinterpret_cast<const T*>(payload.data());
    }
};

} // namespace event_bus
} // namespace runtime
} // namespace obsidian
