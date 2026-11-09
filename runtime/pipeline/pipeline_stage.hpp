#pragma once

#include "storage/common/status.hpp"
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace obsidian {
namespace runtime {
namespace pipeline {

struct DataBatch {
    uint64_t batch_id{0};
    int64_t timestamp_ns{0};
    std::vector<uint8_t> payload;
    std::shared_ptr<void> custom_context;
};

class IStage {
public:
    virtual ~IStage() = default;
    [[nodiscard]] virtual const std::string& GetStageName() const = 0;
    virtual storage::Status Process(const DataBatch& input, DataBatch& output) = 0;
};

} // namespace pipeline
} // namespace runtime
} // namespace obsidian
