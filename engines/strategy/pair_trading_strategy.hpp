#pragma once

#include "engines/strategy/strategy_base.hpp"
#include <deque>
#include <vector>
#include <string>

namespace obsidian {
namespace engines {
namespace strategy {

struct PairTradingConfig {
    double hedge_ratio{1.0};           // Beta coefficient between leg A and leg B
    size_t lookback_window{30};        // Rolling window for mean and std dev calculation
    double entry_z_score{2.0};         // Z-score threshold to enter mean-reverting position
    double exit_z_score{0.2};          // Z-score threshold to exit / close position
    double stop_loss_z_score{3.5};     // Z-score divergence threshold to cut loss
    double leg_a_qty{1.0};             // Base order size for leg A
};

enum class PairPositionState : uint8_t {
    Flat = 0,
    LongSpread = 1,  // Long A, Short B
    ShortSpread = 2  // Short A, Long B
};

class PairTradingStrategy : public IStrategy {
public:
    PairTradingStrategy(
        std::string leg_a_symbol,
        std::string leg_b_symbol,
        PairTradingConfig config = {}
    );

    std::string GetName() const override { return "PairTradingStrategy"; }
    void OnTick(const MarketTick& tick) override;
    void OnBar(const MarketBar& bar) override;
    void OnFill(const OrderFill& fill) override;

    std::vector<OrderRequest> DrainOrders() override;
    const std::unordered_map<std::string, Position>& GetPositions() const noexcept override;
    void Reset() override;

    // Analytics accessors
    [[nodiscard]] double GetCurrentZScore() const noexcept { return current_z_score_; }
    [[nodiscard]] double GetCurrentSpread() const noexcept { return current_spread_; }
    [[nodiscard]] PairPositionState GetState() const noexcept { return state_; }

private:
    void EvaluatePair(uint64_t timestamp_ns);
    void OpenLongSpread(uint64_t timestamp_ns);
    void OpenShortSpread(uint64_t timestamp_ns);
    void CloseSpread(uint64_t timestamp_ns);

    std::string leg_a_;
    std::string leg_b_;
    PairTradingConfig config_;

    double last_price_a_{0.0};
    double last_price_b_{0.0};
    bool has_new_bar_a_{false};
    bool has_new_bar_b_{false};

    std::deque<double> spread_history_;
    double current_spread_{0.0};
    double current_z_score_{0.0};

    PairPositionState state_{PairPositionState::Flat};
    std::vector<OrderRequest> pending_orders_;
    std::unordered_map<std::string, Position> positions_;
    uint64_t next_order_id_{1000};
};

} // namespace strategy
} // namespace engines
} // namespace obsidian
