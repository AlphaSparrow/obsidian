#pragma once

#include "storage/common/status.hpp"
#include <vector>
#include <span>
#include <cstddef>
#include <cmath>

namespace obsidian {
namespace compute {
namespace simd {

struct ProspectTheoryParams {
    double alpha{0.88};     // Diminishing sensitivity
    double lambda{2.25};    // Loss aversion
    double gamma_pos{0.61}; // Probability weighting for gains
    double gamma_neg{0.69}; // Probability weighting for losses
};

struct RollingBehavioralFeatures {
    std::vector<double> pt_values;
    std::vector<double> roll_skew;
    std::vector<double> roll_vol;
    std::vector<double> roll_max;
    std::vector<double> composite_lottery_score;
    std::vector<uint8_t> is_lottery_seeking; // 1 if in top 25% cross-sectional percentile, 0 otherwise
};

class ProspectTheoryEngine {
public:
    explicit ProspectTheoryEngine(ProspectTheoryParams params = {});

    // Calculates CPT probability weight for gains w+(p)
    [[nodiscard]] double ProbabilityWeightGains(double p) const noexcept;

    // Calculates CPT probability weight for losses w-(p)
    [[nodiscard]] double ProbabilityWeightLosses(double p) const noexcept;

    // Evaluates the S-shaped CPT value function v(x)
    [[nodiscard]] double ValueFunction(double x) const noexcept;

    // Calculates the Cumulative Prospect Theory (CPT) metric for a single window of returns
    // Returns NaN if valid return count is below min_periods
    [[nodiscard]] double CalculateWindowCPT(std::span<const double> returns_window, size_t min_periods = 30) const;

    // Calculates 60-day rolling behavioral features (CPT, skewness, volatility, max return) across a return series
    [[nodiscard]] RollingBehavioralFeatures CalculateRollingSeries(
        std::span<const double> daily_returns,
        size_t window_size = 60,
        size_t min_periods = 30) const;

    // Calculates adjusted Fisher-Pearson sample skewness matching pandas df.skew()
    [[nodiscard]] static double CalculateSampleSkewness(std::span<const double> window);

    // Calculates sample standard deviation matching pandas df.std()
    [[nodiscard]] static double CalculateSampleStd(std::span<const double> window);

    // Calculates maximum return in the window
    [[nodiscard]] static double CalculateMaxReturn(std::span<const double> window);

private:
    ProspectTheoryParams params_;
};

} // namespace simd
} // namespace compute
} // namespace obsidian
