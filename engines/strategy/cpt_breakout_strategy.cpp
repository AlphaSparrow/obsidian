#include "engines/strategy/cpt_breakout_strategy.hpp"
#include <algorithm>
#include <cmath>

namespace obsidian {
namespace engines {
namespace strategy {

CPTBreakoutStrategy::CPTBreakoutStrategy(
    std::string target_symbol,
    CPTBreakoutConfig config
) : symbol_(std::move(target_symbol)),
    config_(config) {
    positions_[symbol_] = Position{symbol_, 0.0, 0.0, 0.0, 0.0};
}

void CPTBreakoutStrategy::OnTick(const MarketTick& tick) {
    if (tick.symbol != symbol_) return;
    auto it = positions_.find(symbol_);
    if (it != positions_.end() && std::abs(it->second.quantity) > 1e-9) {
        it->second.UpdateMarketPrice(tick.last_price);
    }
}

void CPTBreakoutStrategy::OnBar(const MarketBar& bar) {
    if (bar.symbol != symbol_) return;

    if (!close_prices_.empty()) {
        double prev_close = close_prices_.back();
        if (prev_close > 0.0) {
            double ret = (bar.close - prev_close) / prev_close;
            returns_.push_back(ret);
            if (returns_.size() > config_.lookback_period * 2) {
                returns_.pop_front();
            }
        }
    }

    close_prices_.push_back(bar.close);
    if (close_prices_.size() > config_.lookback_period + 1) {
        close_prices_.pop_front();
    }

    if (close_prices_.size() < config_.lookback_period) {
        return; // Insufficient history for channel formation
    }

    // 1. Calculate Donchian Channel on preceding (lookback_period - 1) bars
    channel_high_ = -1e9;
    channel_low_ = 1e9;
    size_t count = close_prices_.size() - 1; // Exclude current bar to avoid lookahead bias
    for (size_t i = 0; i < count; ++i) {
        channel_high_ = std::max(channel_high_, close_prices_[i]);
        channel_low_ = std::min(channel_low_, close_prices_[i]);
    }

    // 2. Calculate CPT lottery score on returns
    if (returns_.size() >= 10) {
        std::vector<double> ret_vec(returns_.begin(), returns_.end());
        last_cpt_score_ = cpt_engine_.CalculateWindowCPT(ret_vec, ret_vec.size());
    } else {
        last_cpt_score_ = 0.0;
    }

    Position& pos = positions_[symbol_];
    pos.UpdateMarketPrice(bar.close);

    // 3. Execution logic
    if (pos.quantity <= 1e-9) {
        // Flat: Check for upside breakout with behavioral speculative momentum
        if (bar.close > channel_high_ && last_cpt_score_ >= config_.cpt_threshold) {
            GenerateBuyOrder(bar.close, bar.timestamp_ns);
            trailing_peak_price_ = bar.close;
        }
    } else {
        // Long: Check stop conditions
        trailing_peak_price_ = std::max(trailing_peak_price_, bar.close);

        bool stop_loss_hit = bar.close <= pos.avg_entry_price * (1.0 - config_.stop_loss_pct);
        bool trailing_stop_hit = bar.close <= trailing_peak_price_ * (1.0 - config_.trailing_stop_pct);
        bool channel_exit_hit = bar.close < channel_low_;

        if (stop_loss_hit || trailing_stop_hit || channel_exit_hit) {
            GenerateSellOrder(bar.close, bar.timestamp_ns);
        }
    }
}

void CPTBreakoutStrategy::GenerateBuyOrder(double price, uint64_t timestamp_ns) {
    OrderRequest order{};
    order.order_id = next_order_id_++;
    order.symbol = symbol_;
    order.side = OrderSide::Buy;
    order.type = OrderType::Market;
    order.quantity = config_.fixed_order_qty;
    order.price = price;
    order.timestamp_ns = timestamp_ns;
    pending_orders_.push_back(order);
}

void CPTBreakoutStrategy::GenerateSellOrder(double price, uint64_t timestamp_ns) {
    Position& pos = positions_[symbol_];
    if (pos.quantity <= 0.0) return;

    OrderRequest order{};
    order.order_id = next_order_id_++;
    order.symbol = symbol_;
    order.side = OrderSide::Sell;
    order.type = OrderType::Market;
    order.quantity = pos.quantity;
    order.price = price;
    order.timestamp_ns = timestamp_ns;
    pending_orders_.push_back(order);
}

void CPTBreakoutStrategy::OnFill(const OrderFill& fill) {
    if (fill.symbol != symbol_) return;

    Position& pos = positions_[symbol_];
    if (fill.side == OrderSide::Buy) {
        double new_qty = pos.quantity + fill.filled_qty;
        if (new_qty > 0.0) {
            pos.avg_entry_price = ((pos.quantity * pos.avg_entry_price) + (fill.filled_qty * fill.fill_price)) / new_qty;
        }
        pos.quantity = new_qty;
    } else if (fill.side == OrderSide::Sell) {
        double closing_qty = std::min(pos.quantity, fill.filled_qty);
        pos.realized_pnl += closing_qty * (fill.fill_price - pos.avg_entry_price) - fill.fee;
        pos.quantity -= fill.filled_qty;
        if (pos.quantity <= 1e-9) {
            pos.quantity = 0.0;
            pos.avg_entry_price = 0.0;
            trailing_peak_price_ = 0.0;
        }
    }
}

std::vector<OrderRequest> CPTBreakoutStrategy::DrainOrders() {
    std::vector<OrderRequest> drained = std::move(pending_orders_);
    pending_orders_.clear();
    return drained;
}

const std::unordered_map<std::string, Position>& CPTBreakoutStrategy::GetPositions() const noexcept {
    return positions_;
}

void CPTBreakoutStrategy::Reset() {
    close_prices_.clear();
    returns_.clear();
    pending_orders_.clear();
    positions_[symbol_] = Position{symbol_, 0.0, 0.0, 0.0, 0.0};
    last_cpt_score_ = 0.0;
    channel_high_ = 0.0;
    channel_low_ = 0.0;
    trailing_peak_price_ = 0.0;
}

} // namespace strategy
} // namespace engines
} // namespace obsidian
