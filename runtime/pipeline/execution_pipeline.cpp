#include "runtime/pipeline/execution_pipeline.hpp"

namespace obsidian {
namespace runtime {
namespace pipeline {

ExecutionPipeline::ExecutionPipeline(size_t max_queue_capacity, BackpressurePolicy policy)
    : max_queue_capacity_(max_queue_capacity), policy_(policy) {}

storage::Status ExecutionPipeline::AddStage(std::shared_ptr<IStage> stage) {
    if (!stage) {
        return storage::Status::InvalidArgument("Cannot add null stage to execution pipeline");
    }
    std::unique_lock<std::mutex> lock(mutex_);
    stages_.push_back(std::move(stage));
    return storage::Status::Ok();
}

storage::Status ExecutionPipeline::Push(DataBatch batch) {
    std::unique_lock<std::mutex> lock(mutex_);

    if (queue_.size() >= max_queue_capacity_) {
        if (policy_ == BackpressurePolicy::RejectNew) {
            batches_dropped_.fetch_add(1, std::memory_order_relaxed);
            return storage::Status::Internal("Pipeline queue full: rejected new batch due to backpressure");
        } else if (policy_ == BackpressurePolicy::DropOldest) {
            queue_.pop_front();
            batches_dropped_.fetch_add(1, std::memory_order_relaxed);
        } else {
            // Block
            not_full_cv_.wait(lock, [this]() { return queue_.size() < max_queue_capacity_; });
        }
    }

    queue_.push_back(std::move(batch));
    batches_pushed_.fetch_add(1, std::memory_order_relaxed);
    not_empty_cv_.notify_one();

    return storage::Status::Ok();
}

storage::StatusOr<DataBatch> ExecutionPipeline::ProcessNext() {
    DataBatch current_batch;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return storage::Status::NotFound("No batches available in pipeline queue");
        }
        current_batch = std::move(queue_.front());
        queue_.pop_front();
        not_full_cv_.notify_one();
    }

    auto result_or = ExecuteSync(std::move(current_batch));
    if (result_or.ok()) {
        batches_completed_.fetch_add(1, std::memory_order_relaxed);
    }
    return result_or;
}

storage::StatusOr<DataBatch> ExecutionPipeline::ExecuteSync(DataBatch input) {
    std::vector<std::shared_ptr<IStage>> stages_snapshot;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        stages_snapshot = stages_;
    }

    DataBatch current = std::move(input);
    for (const auto& stage : stages_snapshot) {
        DataBatch next;
        next.batch_id = current.batch_id;
        next.timestamp_ns = current.timestamp_ns;
        next.custom_context = current.custom_context;

        auto st = stage->Process(current, next);
        if (!st.ok()) {
            return storage::Status::Internal("Pipeline stage '" + stage->GetStageName() + "' failed: " + st.message());
        }
        current = std::move(next);
    }

    return current;
}

void ExecutionPipeline::Clear() {
    std::unique_lock<std::mutex> lock(mutex_);
    queue_.clear();
    stages_.clear();
    not_full_cv_.notify_all();
}

size_t ExecutionPipeline::QueueDepth() const {
    std::unique_lock<std::mutex> lock(mutex_);
    return queue_.size();
}

size_t ExecutionPipeline::StageCount() const {
    std::unique_lock<std::mutex> lock(mutex_);
    return stages_.size();
}

PipelineMetrics ExecutionPipeline::GetMetrics() const {
    PipelineMetrics m;
    m.batches_pushed = batches_pushed_.load(std::memory_order_relaxed);
    m.batches_completed = batches_completed_.load(std::memory_order_relaxed);
    m.batches_dropped = batches_dropped_.load(std::memory_order_relaxed);

    std::unique_lock<std::mutex> lock(mutex_);
    m.queue_depth = queue_.size();
    m.stage_count = stages_.size();
    return m;
}

} // namespace pipeline
} // namespace runtime
} // namespace obsidian
