#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <algorithm>

namespace obsidian {
namespace storage {
namespace schema {

// Supported physical and logical data types for columnar storage
enum class DataType : uint8_t {
    Boolean = 0,
    Int8 = 1,
    Int16 = 2,
    Int32 = 3,
    Int64 = 4,
    Float = 5,
    Double = 6,
    String = 7,
    TimestampNs = 8
};

inline const char* DataTypeToString(DataType type) {
    switch (type) {
        case DataType::Boolean: return "Boolean";
        case DataType::Int8: return "Int8";
        case DataType::Int16: return "Int16";
        case DataType::Int32: return "Int32";
        case DataType::Int64: return "Int64";
        case DataType::Float: return "Float";
        case DataType::Double: return "Double";
        case DataType::String: return "String";
        case DataType::TimestampNs: return "TimestampNs";
        default: return "Unknown";
    }
}

struct Field {
    std::string name;
    DataType type;
    bool nullable{true};

    bool operator==(const Field& other) const {
        return name == other.name && type == other.type && nullable == other.nullable;
    }
};

class Schema {
public:
    Schema() = default;
    explicit Schema(std::vector<Field> fields) : fields_(std::move(fields)) {}

    [[nodiscard]] const std::vector<Field>& fields() const noexcept {
        return fields_;
    }

    [[nodiscard]] size_t num_fields() const noexcept {
        return fields_.size();
    }

    [[nodiscard]] const Field& field(size_t index) const {
        if (index >= fields_.size()) {
            throw std::out_of_range("Field index out of range: " + std::to_string(index));
        }
        return fields_[index];
    }

    [[nodiscard]] int GetFieldIndex(const std::string& name) const {
        for (size_t i = 0; i < fields_.size(); ++i) {
            if (fields_[i].name == name) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    [[nodiscard]] bool HasField(const std::string& name) const noexcept {
        return GetFieldIndex(name) >= 0;
    }

    bool operator==(const Schema& other) const {
        return fields_ == other.fields_;
    }

private:
    std::vector<Field> fields_;
};

// Domain entity structs for high-frequency quantitative market data

struct QuoteL1 {
    int64_t timestamp_ns;
    int32_t symbol_id;
    double bid_price;
    double bid_size;
    double ask_price;
    double ask_size;
    int64_t sequence_num;

    static Schema GetSchema() {
        return Schema({
            {"timestamp_ns", DataType::TimestampNs, false},
            {"symbol_id", DataType::Int32, false},
            {"bid_price", DataType::Double, false},
            {"bid_size", DataType::Double, false},
            {"ask_price", DataType::Double, false},
            {"ask_size", DataType::Double, false},
            {"sequence_num", DataType::Int64, false}
        });
    }
};

enum class Side : int8_t {
    Bid = 0,
    Ask = 1
};

struct OrderBookLevel {
    int16_t depth_level;
    Side side;
    double price;
    double quantity;
    int32_t order_count;
};

struct OrderBookL2Snapshot {
    int64_t timestamp_ns;
    int32_t symbol_id;
    std::vector<OrderBookLevel> bids;
    std::vector<OrderBookLevel> asks;

    static Schema GetFlattenedSchema() {
        return Schema({
            {"timestamp_ns", DataType::TimestampNs, false},
            {"symbol_id", DataType::Int32, false},
            {"depth_level", DataType::Int16, false},
            {"side", DataType::Int8, false},
            {"price", DataType::Double, false},
            {"quantity", DataType::Double, false},
            {"order_count", DataType::Int32, false}
        });
    }
};

enum class AggressorSide : int8_t {
    Unknown = 0,
    Buy = 1,
    Sell = 2
};

struct TradeTick {
    int64_t timestamp_ns;
    int32_t symbol_id;
    int64_t trade_id;
    double price;
    double quantity;
    AggressorSide aggressor_side;

    static Schema GetSchema() {
        return Schema({
            {"timestamp_ns", DataType::TimestampNs, false},
            {"symbol_id", DataType::Int32, false},
            {"trade_id", DataType::Int64, false},
            {"price", DataType::Double, false},
            {"quantity", DataType::Double, false},
            {"aggressor_side", DataType::Int8, false}
        });
    }
};

struct BarOHLCV {
    int64_t bar_timestamp_ns;
    int32_t symbol_id;
    int32_t resolution_ms;
    double open;
    double high;
    double low;
    double close;
    double volume;
    double vwap;
    int64_t tick_count;
    double buy_volume;
    double sell_volume;

    static Schema GetSchema() {
        return Schema({
            {"bar_timestamp_ns", DataType::TimestampNs, false},
            {"symbol_id", DataType::Int32, false},
            {"resolution_ms", DataType::Int32, false},
            {"open", DataType::Double, false},
            {"high", DataType::Double, false},
            {"low", DataType::Double, false},
            {"close", DataType::Double, false},
            {"volume", DataType::Double, false},
            {"vwap", DataType::Double, false},
            {"tick_count", DataType::Int64, false},
            {"buy_volume", DataType::Double, false},
            {"sell_volume", DataType::Double, false}
        });
    }
};

} // namespace schema
} // namespace storage
} // namespace obsidian
