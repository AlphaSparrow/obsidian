#pragma once

#include "engines/strategy/strategy_base.hpp"
#include "compute/simd/prospect_theory.hpp"
#include <deque>
#include <vector>
#include <string>

namespace obsidian {
namespace engines {
namespace strategy {

struct CPTBreakoutConfig {
    size_t lookback_period{20};
    double cpt_threshold{0.02}; // Minimum CPT lottery skew score to trigger entry
    double fixed_order_qty{1.0};
    double stop_loss_pct{0.03};  // 3% fixed stop loss
    double trailing_stop_pct{0.05}; // 5% trailing stop
};

class CPTBreakoutStrategy : public IStrategy {
public:
    explicit CPTBreakoutStrategy(
        std::string target_symbol,
        CPTBreakoutConfig config = {}
    );

    std::string GetName() const override { return "CPTBreakoutStrategy"; }
    void OnTick(const MarketTick& tick) override;
    void OnBar(const MarketBar& bar) override;
    void OnFill(const OrderFill& fill) override;

    std::vector<OrderRequest> DrainOrders() override;
    const std::unordered_map<std::string, Position>& GetPositions() const noexcept override;
    void Reset() override;

    // Strategy diagnostics
    [[nodiscard]] double GetLastCPTScore() const noexcept { return last_cpt_score_; }
    [[nodiscard]] double GetChannelHigh() const noexcept { return channel_high_; }
    [[nodiscard]] double GetChannelLow() const noexcept { return channel_low_; }

private:
    void GenerateBuyOrder(double price, uint64_t timestamp_ns);
    void GenerateSellOrder(double price, uint64_t timestamp_ns);

    std::string symbol_;
    CPTBreakoutConfig config_;
    obsidian::compute::simd::ProspectTheoryEngine cpt_engine_;

    std::deque<double> close_prices_;
    std::deque<double> returns_;
    std::vector<OrderRequest> pending_orders_;
    std::unordered_map<std::string, Position> positions_;

    uint64_t next_order_id_{1};
    double last_cpt_score_{0.0};
    double channel_high_{0.0};
    double channel_low_{0.0};
    double trailing_peak_price_{0.0};
};

} // namespace strategy
} // namespace engines
} // namespace obsidian
