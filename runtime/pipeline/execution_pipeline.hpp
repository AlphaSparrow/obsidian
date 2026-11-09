#pragma once

#include "runtime/pipeline/pipeline_stage.hpp"
#include "storage/common/status.hpp"

#include <vector>
#include <deque>
#include <memory>
#include <string>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace obsidian {
namespace runtime {
namespace pipeline {

enum class BackpressurePolicy : uint8_t {
    Block = 0,
    DropOldest = 1,
    RejectNew = 2
};

struct PipelineMetrics {
    uint64_t batches_pushed{0};
    uint64_t batches_completed{0};
    uint64_t batches_dropped{0};
    size_t queue_depth{0};
    size_t stage_count{0};
};

class ExecutionPipeline {
public:
    explicit ExecutionPipeline(size_t max_queue_capacity = 16384, BackpressurePolicy policy = BackpressurePolicy::Block);
    ~ExecutionPipeline() = default;

    ExecutionPipeline(const ExecutionPipeline&) = delete;
    ExecutionPipeline& operator=(const ExecutionPipeline&) = delete;

    // Appends an execution stage to the pipeline
    storage::Status AddStage(std::shared_ptr<IStage> stage);

    // Enqueues a batch for pipeline processing
    storage::Status Push(DataBatch batch);

    // Pops and processes the next batch through all pipeline stages
    storage::StatusOr<DataBatch> ProcessNext();

    // Directly transforms a batch synchronously through all stages
    storage::StatusOr<DataBatch> ExecuteSync(DataBatch input);

    // Empties the queue and clears stages
    void Clear();

    [[nodiscard]] size_t QueueDepth() const;
    [[nodiscard]] size_t StageCount() const;
    [[nodiscard]] PipelineMetrics GetMetrics() const;

private:
    size_t max_queue_capacity_{16384};
    BackpressurePolicy policy_{BackpressurePolicy::Block};

    std::vector<std::shared_ptr<IStage>> stages_;

    mutable std::mutex mutex_;
    std::condition_variable not_full_cv_;
    std::condition_variable not_empty_cv_;
    std::deque<DataBatch> queue_;

    // Telemetry
    std::atomic<uint64_t> batches_pushed_{0};
    std::atomic<uint64_t> batches_completed_{0};
    std::atomic<uint64_t> batches_dropped_{0};
};

} // namespace pipeline
} // namespace runtime
} // namespace obsidian
