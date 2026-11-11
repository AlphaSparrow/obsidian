#include "crypto/feed_monitor.hpp"
#include <algorithm>
#include <cmath>

namespace obsidian {
namespace crypto {

FeedMonitor::FeedMonitor(FeedMonitorConfig config)
    : config_(config) {}

void FeedMonitor::ProcessPacket(const FeedPacket& packet) {
    StreamState& state = streams_[packet.symbol];
    ++state.stats.packets_received;

    // 1. One-way network latency measurement
    double latency_us = 0.0;
    if (packet.received_ts_ns >= packet.exchange_ts_ns) {
        latency_us = static_cast<double>(packet.received_ts_ns - packet.exchange_ts_ns) / 1000.0;
    }
    state.stats.min_latency_us = std::min(state.stats.min_latency_us, latency_us);
    state.stats.max_latency_us = std::max(state.stats.max_latency_us, latency_us);

    // Cumulative rolling average
    state.stats.avg_latency_us = (state.stats.avg_latency_us * static_cast<double>(state.stats.packets_received - 1) + latency_us) 
                                / static_cast<double>(state.stats.packets_received);

    state.recent_latencies_us.push_back(latency_us);
    if (state.recent_latencies_us.size() > config_.latency_window_size) {
        state.recent_latencies_us.pop_front();
    }

    if (!state.has_first_packet) {
        state.last_sequence_id = packet.sequence_id;
        state.last_received_ts_ns = packet.received_ts_ns;
        state.last_exchange_ts_ns = packet.exchange_ts_ns;
        state.has_first_packet = true;
        return;
    }

    // 2. Jitter calculation: |Delta t_curr - Delta t_prev|
    double arrival_interval_us = static_cast<double>(packet.received_ts_ns - state.last_received_ts_ns) / 1000.0;
    if (state.last_interval_us > 0.0) {
        double jitter_us = std::abs(arrival_interval_us - state.last_interval_us);
        state.stats.mean_jitter_us = (state.stats.mean_jitter_us * 0.95) + (jitter_us * 0.05);

        if (jitter_us > config_.jitter_alert_threshold_us) {
            ++state.stats.jitter_spike_events;
            FeedAnomalyEvent event{};
            event.type = FeedAnomalyType::ClockJitterSpike;
            event.symbol = packet.symbol;
            event.sequence_actual = packet.sequence_id;
            event.latency_us = latency_us;
            event.jitter_us = jitter_us;
            event.timestamp_ns = packet.received_ts_ns;
            event.description = "Inter-arrival jitter spike: " + std::to_string(jitter_us) + " us";
            pending_anomalies_.push_back(std::move(event));
        }
    }
    state.last_interval_us = arrival_interval_us;

    // 3. Sequence integrity check
    if (packet.sequence_id > state.last_sequence_id + 1) {
        uint64_t missed = packet.sequence_id - (state.last_sequence_id + 1);
        state.stats.sequence_gaps += missed;

        FeedAnomalyEvent event{};
        event.type = FeedAnomalyType::SequenceGap;
        event.symbol = packet.symbol;
        event.sequence_expected = state.last_sequence_id + 1;
        event.sequence_actual = packet.sequence_id;
        event.latency_us = latency_us;
        event.timestamp_ns = packet.received_ts_ns;
        event.description = "Missed " + std::to_string(missed) + " packets in stream";
        pending_anomalies_.push_back(std::move(event));
    } else if (packet.sequence_id <= state.last_sequence_id) {
        ++state.stats.out_of_order_packets;

        FeedAnomalyEvent event{};
        event.type = FeedAnomalyType::OutOfOrderPacket;
        event.symbol = packet.symbol;
        event.sequence_expected = state.last_sequence_id + 1;
        event.sequence_actual = packet.sequence_id;
        event.latency_us = latency_us;
        event.timestamp_ns = packet.received_ts_ns;
        event.description = "Out-of-order packet arrived";
        pending_anomalies_.push_back(std::move(event));
    }

    // 4. Crossed book anomaly check (Bid >= Ask)
    if (packet.bid_price > 0.0 && packet.ask_price > 0.0 && packet.bid_price >= packet.ask_price) {
        ++state.stats.crossed_book_events;

        FeedAnomalyEvent event{};
        event.type = FeedAnomalyType::CrossedBook;
        event.symbol = packet.symbol;
        event.sequence_actual = packet.sequence_id;
        event.latency_us = latency_us;
        event.timestamp_ns = packet.received_ts_ns;
        event.description = "Crossed market condition: Bid " + std::to_string(packet.bid_price) + 
                            " >= Ask " + std::to_string(packet.ask_price);
        pending_anomalies_.push_back(std::move(event));
    }

    state.last_sequence_id = packet.sequence_id;
    state.last_received_ts_ns = packet.received_ts_ns;
    state.last_exchange_ts_ns = packet.exchange_ts_ns;
}

void FeedMonitor::CheckHeartbeat(uint64_t current_time_ns) {
    for (auto& [sym, state] : streams_) {
        if (!state.has_first_packet) continue;

        if (current_time_ns > state.last_received_ts_ns) {
            uint64_t elapsed_ns = current_time_ns - state.last_received_ts_ns;
            if (elapsed_ns > config_.stale_timeout_ns) {
                ++state.stats.stale_feed_events;

                FeedAnomalyEvent event{};
                event.type = FeedAnomalyType::StaleFeed;
                event.symbol = sym;
                event.sequence_actual = state.last_sequence_id;
                event.latency_us = static_cast<double>(elapsed_ns) / 1000.0;
                event.timestamp_ns = current_time_ns;
                event.description = "Watchdog alert: Feed silent for " + std::to_string(elapsed_ns / 1'000'000) + " ms";
                pending_anomalies_.push_back(std::move(event));
            }
        }
    }
}

std::vector<FeedAnomalyEvent> FeedMonitor::DrainAnomalies() {
    std::vector<FeedAnomalyEvent> drained = std::move(pending_anomalies_);
    pending_anomalies_.clear();
    return drained;
}

FeedMonitorStats FeedMonitor::GetStats(const std::string& symbol) const {
    auto it = streams_.find(symbol);
    if (it == streams_.end()) {
        return FeedMonitorStats{};
    }

    FeedMonitorStats res = it->second.stats;

    // Calculate p99 latency
    if (!it->second.recent_latencies_us.empty()) {
        std::vector<double> latencies(it->second.recent_latencies_us.begin(), it->second.recent_latencies_us.end());
        std::sort(latencies.begin(), latencies.end());
        size_t p99_idx = static_cast<size_t>(std::floor(0.99 * static_cast<double>(latencies.size() - 1)));
        res.p99_latency_us = latencies[p99_idx];
    }

    return res;
}

void FeedMonitor::Reset() {
    streams_.clear();
    pending_anomalies_.clear();
}

} // namespace crypto
} // namespace obsidian

