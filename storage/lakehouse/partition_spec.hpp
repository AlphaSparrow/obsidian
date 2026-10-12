#pragma once

#include "lakehouse_structures.hpp"
#include "storage/common/status.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <functional>

namespace obsidian {
namespace storage {
namespace lakehouse {

enum class PartitionTransform : uint8_t {
    Identity = 0,
    Bucket = 1,
    Year = 2,
    Month = 3,
    Day = 4,
    Hour = 5
};

struct PartitionField {
    std::string source_name;
    std::string target_name;
    PartitionTransform transform;
    int32_t transform_param{0}; // e.g., bucket count for Bucket transform

    bool operator==(const PartitionField& other) const {
        return source_name == other.source_name &&
               target_name == other.target_name &&
               transform == other.transform &&
               transform_param == other.transform_param;
    }
};

class PartitionSpec {
public:
    PartitionSpec() : spec_id_(0) {}
    PartitionSpec(int32_t spec_id, std::vector<PartitionField> fields)
        : spec_id_(spec_id), fields_(std::move(fields)) {}

    [[nodiscard]] int32_t spec_id() const noexcept {
        return spec_id_;
    }

    [[nodiscard]] const std::vector<PartitionField>& fields() const noexcept {
        return fields_;
    }

    [[nodiscard]] bool empty() const noexcept {
        return fields_.empty();
    }

    // Evaluates partition values for a row's raw string/numeric values
    [[nodiscard]] std::vector<PartitionValue> Evaluate(
        const std::unordered_map<std::string, std::string>& row_values) const {
        std::vector<PartitionValue> result;
        result.reserve(fields_.size());

        for (const auto& field : fields_) {
            auto it = row_values.find(field.source_name);
            if (it == row_values.end()) {
                result.push_back({field.target_name, "null"});
                continue;
            }

            const std::string& raw_val = it->second;
            std::string partition_val;

            switch (field.transform) {
                case PartitionTransform::Identity:
                    partition_val = raw_val;
                    break;
                case PartitionTransform::Bucket: {
                    if (field.transform_param <= 0) {
                        partition_val = "0";
                    } else {
                        size_t hash_val = std::hash<std::string>{}(raw_val);
                        int32_t bucket = static_cast<int32_t>(hash_val % static_cast<size_t>(field.transform_param));
                        partition_val = std::to_string(bucket);
                    }
                    break;
                }
                case PartitionTransform::Year:
                case PartitionTransform::Month:
                case PartitionTransform::Day:
                case PartitionTransform::Hour: {
                    try {
                        int64_t ts_ns = std::stoll(raw_val);
                        time_t ts_sec = static_cast<time_t>(ts_ns / 1000000000LL);
                        std::tm tm_buf{};
#if defined(_WIN32)
                        gmtime_s(&tm_buf, &ts_sec);
#else
                        gmtime_r(&ts_sec, &tm_buf);
#endif
                        char buf[32];
                        if (field.transform == PartitionTransform::Year) {
                            std::strftime(buf, sizeof(buf), "%Y", &tm_buf);
                        } else if (field.transform == PartitionTransform::Month) {
                            std::strftime(buf, sizeof(buf), "%Y-%m", &tm_buf);
                        } else if (field.transform == PartitionTransform::Day) {
                            std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_buf);
                        } else {
                            std::strftime(buf, sizeof(buf), "%Y-%m-%d-%H", &tm_buf);
                        }
                        partition_val = std::string(buf);
                    } catch (...) {
                        partition_val = "invalid_timestamp";
                    }
                    break;
                }
            }

            result.push_back({field.target_name, partition_val});
        }

        return result;
    }

    // Builds the directory path component e.g. "exchange=NASDAQ/date=2026-10-12/bucket=3"
    [[nodiscard]] std::string FormatPartitionPath(const std::vector<PartitionValue>& partition_values) const {
        std::string path;
        for (size_t i = 0; i < partition_values.size(); ++i) {
            if (i > 0) path += "/";
            path += partition_values[i].name + "=" + partition_values[i].value;
        }
        return path;
    }

private:
    int32_t spec_id_;
    std::vector<PartitionField> fields_;
};

// Predicate pushdown filter for column-level min/max data skipping
class ColumnPredicate {
public:
    enum class Op {
        Equal,
        LessThan,
        LessThanOrEqual,
        GreaterThan,
        GreaterThanOrEqual,
        InRange
    };

    ColumnPredicate(std::string column, Op op, std::string lower, std::string upper = "")
        : column_(std::move(column)), op_(op), lower_(std::move(lower)), upper_(std::move(upper)) {}

    static ColumnPredicate GreaterThanOrEqual(std::string column, std::string value) {
        return ColumnPredicate(std::move(column), Op::GreaterThanOrEqual, std::move(value));
    }

    static ColumnPredicate LessThanOrEqual(std::string column, std::string value) {
        return ColumnPredicate(std::move(column), Op::LessThanOrEqual, std::move(value));
    }

    static ColumnPredicate InRange(std::string column, std::string min_val, std::string max_val) {
        return ColumnPredicate(std::move(column), Op::InRange, std::move(min_val), std::move(max_val));
    }

    [[nodiscard]] const std::string& column() const noexcept { return column_; }

    // Evaluates whether a file CAN BE SKIPPED based on its recorded min/max statistics
    [[nodiscard]] bool CanSkipFile(const DataFile& file) const {
        auto min_it = file.min_values.find(column_);
        auto max_it = file.max_values.find(column_);
        if (min_it == file.min_values.end() || max_it == file.max_values.end()) {
            return false; // No statistics available, must scan
        }

        const std::string& file_min = min_it->second;
        const std::string& file_max = max_it->second;

        // Try numeric comparison if both values are integral
        try {
            int64_t f_min = std::stoll(file_min);
            int64_t f_max = std::stoll(file_max);
            
            if (op_ == Op::GreaterThanOrEqual) {
                int64_t target = std::stoll(lower_);
                // If entire file's max is less than the lower bound, skip
                return f_max < target;
            } else if (op_ == Op::LessThanOrEqual) {
                int64_t target = std::stoll(lower_);
                // If entire file's min is greater than upper bound, skip
                return f_min > target;
            } else if (op_ == Op::InRange) {
                int64_t low = std::stoll(lower_);
                int64_t high = std::stoll(upper_);
                // If file is completely outside the range [low, high]
                return f_max < low || f_min > high;
            }
        } catch (...) {
            // Fallback to lexicographic string comparison
            if (op_ == Op::GreaterThanOrEqual) {
                return file_max < lower_;
            } else if (op_ == Op::LessThanOrEqual) {
                return file_min > lower_;
            } else if (op_ == Op::InRange) {
                return file_max < lower_ || file_min > upper_;
            }
        }

        return false;
    }

private:
    std::string column_;
    Op op_;
    std::string lower_;
    std::string upper_;
};

} // namespace lakehouse
} // namespace storage
} // namespace obsidian
