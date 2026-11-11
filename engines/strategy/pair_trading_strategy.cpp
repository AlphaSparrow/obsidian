#include "engines/strategy/pair_trading_strategy.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace obsidian {
namespace engines {
namespace strategy {

PairTradingStrategy::PairTradingStrategy(
    std::string leg_a_symbol,
    std::string leg_b_symbol,
    PairTradingConfig config
) : leg_a_(std::move(leg_a_symbol)),
    leg_b_(std::move(leg_b_symbol)),
    config_(config) {
    positions_[leg_a_] = Position{leg_a_, 0.0, 0.0, 0.0, 0.0};
    positions_[leg_b_] = Position{leg_b_, 0.0, 0.0, 0.0, 0.0};
}

void PairTradingStrategy::OnTick(const MarketTick& tick) {
    if (tick.symbol == leg_a_) {
        last_price_a_ = tick.last_price;
        positions_[leg_a_].UpdateMarketPrice(tick.last_price);
    } else if (tick.symbol == leg_b_) {
        last_price_b_ = tick.last_price;
        positions_[leg_b_].UpdateMarketPrice(tick.last_price);
    }
}

void PairTradingStrategy::OnBar(const MarketBar& bar) {
    if (bar.symbol == leg_a_) {
        last_price_a_ = bar.close;
        positions_[leg_a_].UpdateMarketPrice(bar.close);
        has_new_bar_a_ = true;
    } else if (bar.symbol == leg_b_) {
        last_price_b_ = bar.close;
        positions_[leg_b_].UpdateMarketPrice(bar.close);
        has_new_bar_b_ = true;
    }

    if (has_new_bar_a_ && has_new_bar_b_) {
        EvaluatePair(bar.timestamp_ns);
        has_new_bar_a_ = false;
        has_new_bar_b_ = false;
    }
}

void PairTradingStrategy::EvaluatePair(uint64_t timestamp_ns) {
    if (last_price_a_ <= 0.0 || last_price_b_ <= 0.0) return;

    // Log-price spread: S_t = ln(P_a) - beta * ln(P_b)
    current_spread_ = std::log(last_price_a_) - (config_.hedge_ratio * std::log(last_price_b_));
    spread_history_.push_back(current_spread_);

    if (spread_history_.size() > config_.lookback_window) {
        spread_history_.pop_front();
    }

    if (spread_history_.size() < config_.lookback_window) {
        return; // Building initial lookback window
    }

    // Compute rolling mean and std dev of spread
    double sum = 0.0;
    for (double s : spread_history_) sum += s;
    double mean = sum / static_cast<double>(spread_history_.size());

    double var_sum = 0.0;
    for (double s : spread_history_) {
        double d = s - mean;
        var_sum += d * d;
    }
    double std_dev = std::sqrt(var_sum / static_cast<double>(spread_history_.size() - 1));

    if (std_dev < 1e-8) {
        current_z_score_ = 0.0;
        return;
    }

    current_z_score_ = (current_spread_ - mean) / std_dev;

    // State machine for pair trade execution
    switch (state_) {
        case PairPositionState::Flat:
            if (current_z_score_ <= -config_.entry_z_score) {
                OpenLongSpread(timestamp_ns);
                state_ = PairPositionState::LongSpread;
            } else if (current_z_score_ >= config_.entry_z_score) {
                OpenShortSpread(timestamp_ns);
                state_ = PairPositionState::ShortSpread;
            }
            break;

        case PairPositionState::LongSpread:
            // Exit on mean reversion (z >= -exit_z_score) or stop loss divergence
            if (current_z_score_ >= -config_.exit_z_score || current_z_score_ <= -config_.stop_loss_z_score) {
                CloseSpread(timestamp_ns);
                state_ = PairPositionState::Flat;
            }
            break;

        case PairPositionState::ShortSpread:
            // Exit on mean reversion (z <= exit_z_score) or stop loss divergence
            if (current_z_score_ <= config_.exit_z_score || current_z_score_ >= config_.stop_loss_z_score) {
                CloseSpread(timestamp_ns);
                state_ = PairPositionState::Flat;
            }
            break;
    }
}

void PairTradingStrategy::OpenLongSpread(uint64_t timestamp_ns) {
    // Buy Leg A, Sell Leg B
    double qty_b = config_.leg_a_qty * config_.hedge_ratio * (last_price_a_ / last_price_b_);

    OrderRequest order_a{next_order_id_++, leg_a_, OrderSide::Buy, OrderType::Market, config_.leg_a_qty, last_price_a_, timestamp_ns};
    OrderRequest order_b{next_order_id_++, leg_b_, OrderSide::Sell, OrderType::Market, qty_b, last_price_b_, timestamp_ns};

    pending_orders_.push_back(order_a);
    pending_orders_.push_back(order_b);
}

void PairTradingStrategy::OpenShortSpread(uint64_t timestamp_ns) {
    // Sell Leg A, Buy Leg B
    double qty_b = config_.leg_a_qty * config_.hedge_ratio * (last_price_a_ / last_price_b_);

    OrderRequest order_a{next_order_id_++, leg_a_, OrderSide::Sell, OrderType::Market, config_.leg_a_qty, last_price_a_, timestamp_ns};
    OrderRequest order_b{next_order_id_++, leg_b_, OrderSide::Buy, OrderType::Market, qty_b, last_price_b_, timestamp_ns};

    pending_orders_.push_back(order_a);
    pending_orders_.push_back(order_b);
}

void PairTradingStrategy::CloseSpread(uint64_t timestamp_ns) {
    Position& pos_a = positions_[leg_a_];
    Position& pos_b = positions_[leg_b_];

    if (std::abs(pos_a.quantity) > 1e-9) {
        OrderSide side = (pos_a.quantity > 0.0) ? OrderSide::Sell : OrderSide::Buy;
        pending_orders_.push_back(OrderRequest{
            next_order_id_++, leg_a_, side, OrderType::Market, std::abs(pos_a.quantity), last_price_a_, timestamp_ns
        });
    }

    if (std::abs(pos_b.quantity) > 1e-9) {
        OrderSide side = (pos_b.quantity > 0.0) ? OrderSide::Sell : OrderSide::Buy;
        pending_orders_.push_back(OrderRequest{
            next_order_id_++, leg_b_, side, OrderType::Market, std::abs(pos_b.quantity), last_price_b_, timestamp_ns
        });
    }
}

void PairTradingStrategy::OnFill(const OrderFill& fill) {
    auto it = positions_.find(fill.symbol);
    if (it == positions_.end()) return;

    Position& pos = it->second;
    double fill_signed_qty = (fill.side == OrderSide::Buy) ? fill.filled_qty : -fill.filled_qty;

    if (std::abs(pos.quantity) < 1e-9) {
        pos.quantity = fill_signed_qty;
        pos.avg_entry_price = fill.fill_price;
    } else if ((pos.quantity > 0 && fill_signed_qty > 0) || (pos.quantity < 0 && fill_signed_qty < 0)) {
        double new_qty = pos.quantity + fill_signed_qty;
        pos.avg_entry_price = ((pos.quantity * pos.avg_entry_price) + (fill_signed_qty * fill.fill_price)) / new_qty;
        pos.quantity = new_qty;
    } else {
        // Reducing or closing position
        double closed_qty = std::min(std::abs(pos.quantity), std::abs(fill_signed_qty));
        double trade_pnl = (pos.quantity > 0)
            ? closed_qty * (fill.fill_price - pos.avg_entry_price)
            : closed_qty * (pos.avg_entry_price - fill.fill_price);

        pos.realized_pnl += trade_pnl - fill.fee;
        pos.quantity += fill_signed_qty;
        if (std::abs(pos.quantity) < 1e-9) {
            pos.quantity = 0.0;
            pos.avg_entry_price = 0.0;
        }
    }
}

std::vector<OrderRequest> PairTradingStrategy::DrainOrders() {
    std::vector<OrderRequest> drained = std::move(pending_orders_);
    pending_orders_.clear();
    return drained;
}

const std::unordered_map<std::string, Position>& PairTradingStrategy::GetPositions() const noexcept {
    return positions_;
}

void PairTradingStrategy::Reset() {
    spread_history_.clear();
    pending_orders_.clear();
    positions_[leg_a_] = Position{leg_a_, 0.0, 0.0, 0.0, 0.0};
    positions_[leg_b_] = Position{leg_b_, 0.0, 0.0, 0.0, 0.0};
    state_ = PairPositionState::Flat;
    current_spread_ = 0.0;
    current_z_score_ = 0.0;
    last_price_a_ = 0.0;
    last_price_b_ = 0.0;
    has_new_bar_a_ = false;
    has_new_bar_b_ = false;
}

} // namespace strategy
} // namespace engines
} // namespace obsidian
