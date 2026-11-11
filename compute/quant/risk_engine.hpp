#pragma once

#include "storage/common/status.hpp"
#include <vector>
#include <span>
#include <cstddef>
#include <cstdint>

namespace obsidian {
namespace compute {
namespace quant {

struct RiskMetricsReport {
    double sample_mean{0.0};
    double sample_std_dev{0.0};
    double skewness{0.0};
    double excess_kurtosis{0.0};

    // Value at Risk (Loss as a positive fraction, e.g. 0.05 = 5% loss)
    double parametric_var_95{0.0};
    double parametric_var_99{0.0};
    double cornish_fisher_var_95{0.0};
    double cornish_fisher_var_99{0.0};
    double historical_var_95{0.0};
    double historical_var_99{0.0};

    // Expected Shortfall (CVaR)
    double cvar_95{0.0};
    double cvar_99{0.0};

    // Drawdown
    double max_drawdown{0.0};
    size_t max_drawdown_duration{0};

    // Ratios (Annualized)
    double annualized_return{0.0};
    double annualized_volatility{0.0};
    double sharpe_ratio{0.0};
    double sortino_ratio{0.0};
    double calmar_ratio{0.0};
};

class RiskEngine {
public:
    // Calculates inverse normal CDF (probit function) for quantile p
    [[nodiscard]] static double NormalQuantile(double p) noexcept;

    // Evaluates full multi-method risk profile across return series
    [[nodiscard]] static RiskMetricsReport Analyze(
        std::span<const double> returns,
        double risk_free_rate = 0.0,
        double periods_per_year = 365.0
    );

    // Parametric Gaussian VaR
    [[nodiscard]] static double ParametricVaR(
        double mean,
        double std_dev,
        double confidence = 0.95
    ) noexcept;

    // Cornish-Fisher expansion VaR accounting for skewness and excess kurtosis
    [[nodiscard]] static double CornishFisherVaR(
        double mean,
        double std_dev,
        double skewness,
        double excess_kurtosis,
        double confidence = 0.95
    ) noexcept;

    // Historical empirical VaR
    [[nodiscard]] static double HistoricalVaR(
        std::span<const double> returns,
        double confidence = 0.95
    );

    // Expected Shortfall (Conditional VaR)
    [[nodiscard]] static double ExpectedShortfall(
        std::span<const double> returns,
        double confidence = 0.95
    );

    // Maximum drawdown from peak to trough
    [[nodiscard]] static std::pair<double, size_t> CalculateMaxDrawdown(
        std::span<const double> returns
    ) noexcept;
};

} // namespace quant
} // namespace compute
} // namespace obsidian

