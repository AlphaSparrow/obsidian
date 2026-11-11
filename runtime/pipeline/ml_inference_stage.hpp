#pragma once

#include "runtime/pipeline/pipeline_stage.hpp"
#include "storage/common/status.hpp"

#include <vector>
#include <string>
#include <memory>
#include <atomic>
#include <span>

namespace obsidian {
namespace runtime {
namespace pipeline {

struct DecisionTreeNode {
    int32_t feature_index{-1}; // -1 if leaf
    double threshold{0.0};
    int32_t left_child{-1};
    int32_t right_child{-1};
    double leaf_probability{0.0}; // Probability of class 1 (lottery seeking)
};

class DecisionTree {
public:
    DecisionTree() = default;
    explicit DecisionTree(std::vector<DecisionTreeNode> nodes);

    [[nodiscard]] double PredictProbability(std::span<const double> features) const noexcept;
    [[nodiscard]] size_t NodeCount() const noexcept { return nodes_.size(); }

private:
    std::vector<DecisionTreeNode> nodes_;
};

class RandomForestModel {
public:
    RandomForestModel() = default;
    explicit RandomForestModel(std::vector<DecisionTree> trees);

    // Predicts probability of speculative regime (Class 1) across the ensemble
    [[nodiscard]] double PredictProbability(std::span<const double> features) const noexcept;

    // Binary classification (1 if probability >= threshold, else 0)
    [[nodiscard]] uint8_t PredictClass(std::span<const double> features, double threshold = 0.5) const noexcept;

    [[nodiscard]] size_t TreeCount() const noexcept { return trees_.size(); }

    // Generates a trained behavioral heuristic model matching the Random Forest hyperparameters
    static RandomForestModel CreateTrainedModel();

private:
    std::vector<DecisionTree> trees_;
};

struct MLInferenceMetrics {
    uint64_t total_predictions{0};
    uint64_t speculative_alerts{0};
};

class MLInferenceStage : public IStage {
public:
    explicit MLInferenceStage(
        RandomForestModel model = RandomForestModel::CreateTrainedModel(),
        double alert_threshold = 0.70);

    [[nodiscard]] const std::string& GetStageName() const override;
    storage::Status Process(const DataBatch& input, DataBatch& output) override;

    [[nodiscard]] MLInferenceMetrics GetMetrics() const;

private:
    RandomForestModel model_;
    double alert_threshold_{0.70};

    std::atomic<uint64_t> total_predictions_{0};
    std::atomic<uint64_t> speculative_alerts_{0};
};

} // namespace pipeline
} // namespace runtime
} // namespace obsidian
