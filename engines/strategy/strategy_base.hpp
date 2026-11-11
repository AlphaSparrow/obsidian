#pragma once

#include "storage/common/status.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <memory>

namespace obsidian {
namespace engines {
namespace strategy {

enum class OrderSide : uint8_t {
    Buy = 0,
    Sell = 1
};

enum class OrderType : uint8_t {
    Market = 0,
    Limit = 1,
    StopLoss = 2
};

struct MarketTick {
    std::string symbol;
    uint64_t timestamp_ns{0};
    double bid_price{0.0};
    double ask_price{0.0};
    double last_price{0.0};
    double volume{0.0};
};

struct MarketBar {
    std::string symbol;
    uint64_t timestamp_ns{0};
    double open{0.0};
    double high{0.0};
    double low{0.0};
    double close{0.0};
    double volume{0.0};
};

struct OrderRequest {
    uint64_t order_id{0};
    std::string symbol;
    OrderSide side{OrderSide::Buy};
    OrderType type{OrderType::Market};
    double quantity{0.0};
    double price{0.0};
    uint64_t timestamp_ns{0};
};

struct OrderFill {
    uint64_t order_id{0};
    std::string symbol;
    OrderSide side{OrderSide::Buy};
    double fill_price{0.0};
    double filled_qty{0.0};
    double fee{0.0};
    uint64_t timestamp_ns{0};
};

struct Position {
    std::string symbol;
    double quantity{0.0};
    double avg_entry_price{0.0};
    double realized_pnl{0.0};
    double unrealized_pnl{0.0};

    void UpdateMarketPrice(double current_price) noexcept {
        unrealized_pnl = quantity * (current_price - avg_entry_price);
    }
};

class IStrategy {
public:
    virtual ~IStrategy() = default;

    virtual std::string GetName() const = 0;
    virtual void OnTick(const MarketTick& tick) = 0;
    virtual void OnBar(const MarketBar& bar) = 0;
    virtual void OnFill(const OrderFill& fill) = 0;

    // Retrieves pending orders produced by latest evaluation
    virtual std::vector<OrderRequest> DrainOrders() = 0;

    // Current position state
    virtual const std::unordered_map<std::string, Position>& GetPositions() const noexcept = 0;

    // Resets internal strategy state
    virtual void Reset() = 0;
};

} // namespace strategy
} // namespace engines
} // namespace obsidian

