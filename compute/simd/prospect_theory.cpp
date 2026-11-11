#include "compute/simd/prospect_theory.hpp"
#include <algorithm>
#include <numeric>
#include <limits>

namespace obsidian {
namespace compute {
namespace simd {

ProspectTheoryEngine::ProspectTheoryEngine(ProspectTheoryParams params)
    : params_(params) {}

double ProspectTheoryEngine::ProbabilityWeightGains(double p) const noexcept {
    if (p <= 0.0) return 0.0;
    if (p >= 1.0) return 1.0;
    double p_g = std::pow(p, params_.gamma_pos);
    double denom = std::pow(p_g + std::pow(1.0 - p, params_.gamma_pos), 1.0 / params_.gamma_pos);
    return p_g / denom;
}

double ProspectTheoryEngine::ProbabilityWeightLosses(double p) const noexcept {
    if (p <= 0.0) return 0.0;
    if (p >= 1.0) return 1.0;
    double p_g = std::pow(p, params_.gamma_neg);
    double denom = std::pow(p_g + std::pow(1.0 - p, params_.gamma_neg), 1.0 / params_.gamma_neg);
    return p_g / denom;
}

double ProspectTheoryEngine::ValueFunction(double x) const noexcept {
    if (x >= 0.0) {
        return std::pow(x, params_.alpha);
    } else {
        return -params_.lambda * std::pow(-x, params_.alpha);
    }
}

double ProspectTheoryEngine::CalculateWindowCPT(std::span<const double> returns_window, size_t min_periods) const {
    std::vector<double> valid_returns;
    valid_returns.reserve(returns_window.size());

    for (double r : returns_window) {
        if (!std::isnan(r) && !std::isinf(r)) {
            valid_returns.push_back(r);
        }
    }

    size_t n = valid_returns.size();
    if (n < min_periods) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    std::vector<double> gains;
    std::vector<double> losses;
    gains.reserve(n);
    losses.reserve(n);

    for (double r : valid_returns) {
        if (r >= 0.0) {
            gains.push_back(r);
        } else {
            losses.push_back(r);
        }
    }

    // Gains: sorted descending (highest/best gain at index 0)
    std::sort(gains.begin(), gains.end(), std::greater<double>());
    // Losses: sorted ascending (worst/most negative loss at index 0)
    std::sort(losses.begin(), losses.end());

    double cpt_value = 0.0;
    double n_double = static_cast<double>(n);

    // Process Gains
    for (size_t i = 0; i < gains.size(); ++i) {
        double p_current = static_cast<double>(i + 1) / n_double;
        double p_prev = static_cast<double>(i) / n_double;
        double decision_weight = ProbabilityWeightGains(p_current) - ProbabilityWeightGains(p_prev);
        cpt_value += decision_weight * std::pow(gains[i], params_.alpha);
    }

    // Process Losses
    for (size_t i = 0; i < losses.size(); ++i) {
        double p_current = static_cast<double>(i + 1) / n_double;
        double p_prev = static_cast<double>(i) / n_double;
        double decision_weight = ProbabilityWeightLosses(p_current) - ProbabilityWeightLosses(p_prev);
        cpt_value += decision_weight * (-params_.lambda * std::pow(-losses[i], params_.alpha));
    }

    return cpt_value;
}

double ProspectTheoryEngine::CalculateSampleSkewness(std::span<const double> window) {
    std::vector<double> valid;
    valid.reserve(window.size());
    for (double v : window) {
        if (!std::isnan(v) && !std::isinf(v)) {
            valid.push_back(v);
        }
    }

    size_t n = valid.size();
    if (n < 3) return std::numeric_limits<double>::quiet_NaN();

    double mean = std::accumulate(valid.begin(), valid.end(), 0.0) / static_cast<double>(n);

    double m2 = 0.0;
    double m3 = 0.0;
    for (double x : valid) {
        double diff = x - mean;
        m2 += diff * diff;
        m3 += diff * diff * diff;
    }

    m2 /= static_cast<double>(n);
    m3 /= static_cast<double>(n);

    double variance = m2;
    if (variance <= 1e-14) return 0.0;

    double g1 = m3 / std::pow(variance, 1.5);
    // Adjusted Fisher-Pearson skewness: G1 = sqrt(n*(n-1))/(n-2) * g1
    double n_d = static_cast<double>(n);
    double adj = std::sqrt(n_d * (n_d - 1.0)) / (n_d - 2.0);
    return adj * g1;
}

double ProspectTheoryEngine::CalculateSampleStd(std::span<const double> window) {
    std::vector<double> valid;
    valid.reserve(window.size());
    for (double v : window) {
        if (!std::isnan(v) && !std::isinf(v)) {
            valid.push_back(v);
        }
    }

    size_t n = valid.size();
    if (n < 2) return std::numeric_limits<double>::quiet_NaN();

    double mean = std::accumulate(valid.begin(), valid.end(), 0.0) / static_cast<double>(n);
    double sum_sq = 0.0;
    for (double x : valid) {
        double diff = x - mean;
        sum_sq += diff * diff;
    }

    return std::sqrt(sum_sq / static_cast<double>(n - 1));
}

double ProspectTheoryEngine::CalculateMaxReturn(std::span<const double> window) {
    double max_val = -std::numeric_limits<double>::infinity();
    bool found = false;
    for (double v : window) {
        if (!std::isnan(v) && !std::isinf(v)) {
            if (v > max_val) {
                max_val = v;
                found = true;
            }
        }
    }
    return found ? max_val : std::numeric_limits<double>::quiet_NaN();
}

RollingBehavioralFeatures ProspectTheoryEngine::CalculateRollingSeries(
    std::span<const double> daily_returns,
    size_t window_size,
    size_t min_periods) const {
    size_t total = daily_returns.size();
    RollingBehavioralFeatures features;
    features.pt_values.resize(total, std::numeric_limits<double>::quiet_NaN());
    features.roll_skew.resize(total, std::numeric_limits<double>::quiet_NaN());
    features.roll_vol.resize(total, std::numeric_limits<double>::quiet_NaN());
    features.roll_max.resize(total, std::numeric_limits<double>::quiet_NaN());
    features.composite_lottery_score.resize(total, std::numeric_limits<double>::quiet_NaN());
    features.is_lottery_seeking.resize(total, 0);

    if (total == 0) return features;

    for (size_t i = 0; i < total; ++i) {
        size_t start = (i + 1 > window_size) ? (i + 1 - window_size) : 0;
        size_t len = i - start + 1;

        std::span<const double> window(&daily_returns[start], len);
        features.pt_values[i] = CalculateWindowCPT(window, min_periods);
        features.roll_skew[i] = CalculateSampleSkewness(window);
        features.roll_vol[i] = CalculateSampleStd(window);
        features.roll_max[i] = CalculateMaxReturn(window);
    }

    return features;
}

} // namespace simd
} // namespace compute
} // namespace obsidian
