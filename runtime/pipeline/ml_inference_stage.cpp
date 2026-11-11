#include "runtime/pipeline/ml_inference_stage.hpp"

namespace obsidian {
namespace runtime {
namespace pipeline {

DecisionTree::DecisionTree(std::vector<DecisionTreeNode> nodes)
    : nodes_(std::move(nodes)) {}

double DecisionTree::PredictProbability(std::span<const double> features) const noexcept {
    if (nodes_.empty()) return 0.0;

    int32_t curr_idx = 0;
    while (curr_idx >= 0 && curr_idx < static_cast<int32_t>(nodes_.size())) {
        const auto& node = nodes_[curr_idx];
        if (node.feature_index == -1) {
            // Leaf node
            return node.leaf_probability;
        }

        size_t f_idx = static_cast<size_t>(node.feature_index);
        double val = (f_idx < features.size()) ? features[f_idx] : 0.0;

        if (val <= node.threshold) {
            curr_idx = node.left_child;
        } else {
            curr_idx = node.right_child;
        }
    }

    return 0.0;
}

RandomForestModel::RandomForestModel(std::vector<DecisionTree> trees)
    : trees_(std::move(trees)) {}

double RandomForestModel::PredictProbability(std::span<const double> features) const noexcept {
    if (trees_.empty()) return 0.0;

    double sum_prob = 0.0;
    for (const auto& tree : trees_) {
        sum_prob += tree.PredictProbability(features);
    }
    return sum_prob / static_cast<double>(trees_.size());
}

uint8_t RandomForestModel::PredictClass(std::span<const double> features, double threshold) const noexcept {
    return PredictProbability(features) >= threshold ? 1 : 0;
}

RandomForestModel RandomForestModel::CreateTrainedModel() {
    std::vector<DecisionTree> ensemble;

    // Feature indices:
    // 0: pt_value, 1: roll_skew, 2: roll_max, 3: roll_vol, 4: turnover

    // Tree 1: Primary split on CPT pt_value and roll_max
    {
        std::vector<DecisionTreeNode> nodes = {
            /* 0 */ {0, 0.05, 1, 2, 0.0},
            /* 1 */ {2, 0.08, 3, 4, 0.0},
            /* 2 */ {2, 0.15, 5, 6, 0.0},
            /* 3 */ {-1, 0.0, -1, -1, 0.10}, // Leaf: low pt, low max
            /* 4 */ {-1, 0.0, -1, -1, 0.45}, // Leaf: low pt, moderate max
            /* 5 */ {-1, 0.0, -1, -1, 0.65}, // Leaf: high pt, moderate max
            /* 6 */ {-1, 0.0, -1, -1, 0.95}  // Leaf: high pt, high max (speculative)
        };
        ensemble.emplace_back(std::move(nodes));
    }

    // Tree 2: Primary split on roll_skew and pt_value
    {
        std::vector<DecisionTreeNode> nodes = {
            /* 0 */ {1, 0.75, 1, 2, 0.0},
            /* 1 */ {0, 0.02, 3, 4, 0.0},
            /* 2 */ {4, 0.04, 5, 6, 0.0},
            /* 3 */ {-1, 0.0, -1, -1, 0.05},
            /* 4 */ {-1, 0.0, -1, -1, 0.50},
            /* 5 */ {-1, 0.0, -1, -1, 0.70},
            /* 6 */ {-1, 0.0, -1, -1, 0.90}
        };
        ensemble.emplace_back(std::move(nodes));
    }

    // Tree 3: Primary split on turnover and roll_max
    {
        std::vector<DecisionTreeNode> nodes = {
            /* 0 */ {4, 0.03, 1, 2, 0.0},
            /* 1 */ {2, 0.10, 3, 4, 0.0},
            /* 2 */ {0, 0.06, 5, 6, 0.0},
            /* 3 */ {-1, 0.0, -1, -1, 0.12},
            /* 4 */ {-1, 0.0, -1, -1, 0.55},
            /* 5 */ {-1, 0.0, -1, -1, 0.75},
            /* 6 */ {-1, 0.0, -1, -1, 0.92}
        };
        ensemble.emplace_back(std::move(nodes));
    }

    return RandomForestModel(std::move(ensemble));
}

MLInferenceStage::MLInferenceStage(RandomForestModel model, double alert_threshold)
    : model_(std::move(model)), alert_threshold_(alert_threshold) {}

const std::string& MLInferenceStage::GetStageName() const {
    static std::string name = "MLInferenceStage";
    return name;
}

storage::Status MLInferenceStage::Process(const DataBatch& input, DataBatch& output) {
    output = input;

    // Check if payload contains 5 float/double features (e.g. 5 * 8 bytes = 40 bytes)
    if (input.payload.size() >= 5 * sizeof(double)) {
        const auto* features_ptr = reinterpret_cast<const double*>(input.payload.data());
        std::span<const double> features(features_ptr, 5);

        double prob = model_.PredictProbability(features);
        total_predictions_.fetch_add(1, std::memory_order_relaxed);

        if (prob >= alert_threshold_) {
            speculative_alerts_.fetch_add(1, std::memory_order_relaxed);
        }

        // Append probability to output payload
        size_t cur_sz = output.payload.size();
        output.payload.resize(cur_sz + sizeof(double));
        std::memcpy(output.payload.data() + cur_sz, &prob, sizeof(double));
    }

    return storage::Status::Ok();
}

MLInferenceMetrics MLInferenceStage::GetMetrics() const {
    MLInferenceMetrics m;
    m.total_predictions = total_predictions_.load(std::memory_order_relaxed);
    m.speculative_alerts = speculative_alerts_.load(std::memory_order_relaxed);
    return m;
}

} // namespace pipeline
} // namespace runtime
} // namespace obsidian
