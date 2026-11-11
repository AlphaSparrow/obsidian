#pragma once

#include "storage/common/status.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <deque>
#include <cstdint>
#include <cstddef>

namespace obsidian {
namespace crypto {

enum class FeedAnomalyType : uint8_t {
    SequenceGap = 0,
    OutOfOrderPacket = 1,
    StaleFeed = 2,
    CrossedBook = 3,
    ClockJitterSpike = 4
};

inline const char* FeedAnomalyTypeToString(FeedAnomalyType type) noexcept {
    switch (type) {
        case FeedAnomalyType::SequenceGap: return "SequenceGap";
        case FeedAnomalyType::OutOfOrderPacket: return "OutOfOrderPacket";
        case FeedAnomalyType::StaleFeed: return "StaleFeed";
        case FeedAnomalyType::CrossedBook: return "CrossedBook";
        case FeedAnomalyType::ClockJitterSpike: return "ClockJitterSpike";
        default: return "Unknown";
    }
}

struct FeedPacket {
    uint64_t sequence_id{0};
    uint64_t exchange_ts_ns{0};
    uint64_t received_ts_ns{0};
    std::string symbol;
    double bid_price{0.0};
    double ask_price{0.0};
    double bid_qty{0.0};
    double ask_qty{0.0};
    double last_price{0.0};
};

struct FeedAnomalyEvent {
    FeedAnomalyType type{FeedAnomalyType::SequenceGap};
    std::string symbol;
    uint64_t sequence_expected{0};
    uint64_t sequence_actual{0};
    double latency_us{0.0};
    double jitter_us{0.0};
    uint64_t timestamp_ns{0};
    std::string description;
};

struct FeedMonitorStats {
    uint64_t packets_received{0};
    uint64_t sequence_gaps{0};
    uint64_t out_of_order_packets{0};
    uint64_t crossed_book_events{0};
    uint64_t stale_feed_events{0};
    uint64_t jitter_spike_events{0};

    double min_latency_us{1e9};
    double max_latency_us{0.0};
    double avg_latency_us{0.0};
    double p99_latency_us{0.0};
    double mean_jitter_us{0.0};
};

struct FeedMonitorConfig {
    uint64_t stale_timeout_ns{500'000'000};       // 500 ms heartbeat watchdog
    double jitter_alert_threshold_us{50'000.0};  // 50 ms jitter spike threshold
    size_t latency_window_size{1000};            // Sliding sample window for percentiles
};

class FeedMonitor {
public:
    explicit FeedMonitor(FeedMonitorConfig config = {});

    // Ingests and inspects an inbound websocket tick / orderbook packet
    void ProcessPacket(const FeedPacket& packet);

    // Watchdog check for stale / dead feeds
    void CheckHeartbeat(uint64_t current_time_ns);

    // Drains detected anomalies and alert events
    [[nodiscard]] std::vector<FeedAnomalyEvent> DrainAnomalies();

    // Query per-symbol aggregate metrics
    [[nodiscard]] FeedMonitorStats GetStats(const std::string& symbol) const;

    // Resets tracker state
    void Reset();

private:
    struct StreamState {
        uint64_t last_sequence_id{0};
        uint64_t last_received_ts_ns{0};
        uint64_t last_exchange_ts_ns{0};
        double last_interval_us{0.0};
        bool has_first_packet{false};

        FeedMonitorStats stats;
        std::deque<double> recent_latencies_us;
    };

    FeedMonitorConfig config_;
    std::unordered_map<std::string, StreamState> streams_;
    std::vector<FeedAnomalyEvent> pending_anomalies_;
};

} // namespace crypto
} // namespace obsidian

