#pragma once

#include "storage/common/status.hpp"
#include <vector>
#include <span>
#include <cstddef>
#include <cmath>

namespace obsidian {
namespace compute {
namespace quant {

enum class VolatilityRegime : uint8_t {
    Low = 0,
    Normal = 1,
    Elevated = 2,
    Extreme = 3
};

inline const char* VolatilityRegimeToString(VolatilityRegime regime) noexcept {
    switch (regime) {
        case VolatilityRegime::Low: return "Low";
        case VolatilityRegime::Normal: return "Normal";
        case VolatilityRegime::Elevated: return "Elevated";
        case VolatilityRegime::Extreme: return "Extreme";
        default: return "Unknown";
    }
}

struct GarchParams {
    double omega{1e-6};  // Baseline variance weight (> 0)
    double alpha{0.08};  // Innovation / shock coefficient
    double beta{0.90};   // Persistence coefficient
    // Invariant: alpha + beta < 1.0 (covariance stationary)

    [[nodiscard]] bool IsStationary() const noexcept {
        return omega > 0.0 && alpha >= 0.0 && beta >= 0.0 && (alpha + beta < 1.0);
    }

    [[nodiscard]] double UnconditionalVariance() const noexcept {
        return IsStationary() ? (omega / (1.0 - alpha - beta)) : 0.0;
    }
};

class VolatilityModel {
public:
    explicit VolatilityModel(GarchParams params = {}, double ewma_lambda = 0.94);

    // Updates volatility state with the latest period return
    void Update(double daily_return);

    // Predicts forward variance k-steps ahead using GARCH(1,1) mean reversion
    [[nodiscard]] double ForecastVariance(size_t steps_ahead = 1) const noexcept;

    // Returns annualized standard deviation (assuming 365 days for crypto)
    [[nodiscard]] double GetAnnualizedVolatility(double trading_days_per_year = 365.0) const noexcept;

    // Current EWMA volatility estimate
    [[nodiscard]] double GetEwmaVolatility() const noexcept;

    // Current GARCH(1,1) instantaneous volatility estimate
    [[nodiscard]] double GetGarchVolatility() const noexcept;

    // Classifies the prevailing volatility regime
    [[nodiscard]] VolatilityRegime GetCurrentRegime() const noexcept;

    // Calibrate model parameters on a historical return series
    storage::Status FitSeries(std::span<const double> historical_returns);

    // Resets internal volatility state
    void Reset();

private:
    GarchParams params_;
    double ewma_lambda_{0.94};

    double current_variance_{0.0};
    double current_ewma_variance_{0.0};
    size_t observations_{0};
};

} // namespace quant
} // namespace compute
} // namespace obsidian

