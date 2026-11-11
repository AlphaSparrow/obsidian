#include "compute/quant/risk_engine.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>

namespace obsidian {
namespace compute {
namespace quant {

// Accurate Rational Approximation for Inverse Normal CDF (Acklam algorithm)
double RiskEngine::NormalQuantile(double p) noexcept {
    if (p <= 0.0) return -1e9;
    if (p >= 1.0) return 1e9;

    // Coefficients in rational approximations
    static const double a1 = -3.969683028665376e+01;
    static const double a2 =  2.209460984245205e+02;
    static const double a3 = -2.759285104469687e+02;
    static const double a4 =  1.383577518672690e+02;
    static const double a5 = -3.066479806614716e+01;
    static const double a6 =  2.506628277459239e+00;

    static const double b1 = -5.447609879822406e+01;
    static const double b2 =  1.615858368580409e+02;
    static const double b3 = -1.556989798598866e+02;
    static const double b4 =  6.680131188771972e+01;
    static const double b5 = -1.328068155288572e+01;

    static const double c1 = -7.784894002430293e-03;
    static const double c2 = -3.223964580411365e-01;
    static const double c3 = -2.400758277161838e+00;
    static const double c4 = -2.549732539343734e+00;
    static const double c5 =  4.374664141464968e+00;
    static const double c6 =  2.938163982698783e+00;

    static const double d1 =  7.784695709041462e-03;
    static const double d2 =  3.224671290700398e-01;
    static const double d3 =  2.445134137142996e+00;
    static const double d4 =  3.754408661907416e+00;

    static const double p_low  = 0.02425;
    static const double p_high = 1.0 - p_low;

    double q = 0.0;
    if (p < p_low) {
        // Rational approximation for lower region
        q = std::sqrt(-2.0 * std::log(p));
        return (((((c1 * q + c2) * q + c3) * q + c4) * q + c5) * q + c6) /
               ((((d1 * q + d2) * q + d3) * q + d4) * q + 1.0);
    } else if (p <= p_high) {
        // Rational approximation for central region
        q = p - 0.5;
        double r = q * q;
        return (((((a1 * r + a2) * r + a3) * r + a4) * r + a5) * r + a6) * q /
               (((((b1 * r + b2) * r + b3) * r + b4) * r + b5) * r + 1.0);
    } else {
        // Rational approximation for upper region
        q = std::sqrt(-2.0 * std::log(1.0 - p));
        return -(((((c1 * q + c2) * q + c3) * q + c4) * q + c5) * q + c6) /
                ((((d1 * q + d2) * q + d3) * q + d4) * q + 1.0);
    }
}

double RiskEngine::ParametricVaR(double mean, double std_dev, double confidence) noexcept {
    double alpha = 1.0 - confidence;
    double z = NormalQuantile(alpha);
    // VaR expressed as positive loss threshold: -(mu + z * sigma)
    return -(mean + z * std_dev);
}

double RiskEngine::CornishFisherVaR(
    double mean,
    double std_dev,
    double skewness,
    double excess_kurtosis,
    double confidence
) noexcept {
    double alpha = 1.0 - confidence;
    double z = NormalQuantile(alpha);

    // Cornish-Fisher expansion quantile
    double z_cf = z + 
        (1.0 / 6.0) * (z * z - 1.0) * skewness +
        (1.0 / 24.0) * (z * z * z - 3.0 * z) * excess_kurtosis -
        (1.0 / 36.0) * (2.0 * z * z * z - 5.0 * z) * (skewness * skewness);

    return -(mean + z_cf * std_dev);
}

double RiskEngine::HistoricalVaR(std::span<const double> returns, double confidence) {
    if (returns.empty()) return 0.0;

    std::vector<double> sorted_returns(returns.begin(), returns.end());
    std::sort(sorted_returns.begin(), sorted_returns.end());

    double alpha = 1.0 - confidence;
    size_t idx = static_cast<size_t>(std::floor(alpha * static_cast<double>(sorted_returns.size())));
    if (idx >= sorted_returns.size()) {
        idx = sorted_returns.size() - 1;
    }

    return -sorted_returns[idx];
}

double RiskEngine::ExpectedShortfall(std::span<const double> returns, double confidence) {
    if (returns.empty()) return 0.0;

    std::vector<double> sorted_returns(returns.begin(), returns.end());
    std::sort(sorted_returns.begin(), sorted_returns.end());

    double alpha = 1.0 - confidence;
    size_t cutoff_count = static_cast<size_t>(std::floor(alpha * static_cast<double>(sorted_returns.size())));
    if (cutoff_count == 0) cutoff_count = 1;

    double tail_sum = 0.0;
    for (size_t i = 0; i < cutoff_count; ++i) {
        tail_sum += sorted_returns[i];
    }

    return -(tail_sum / static_cast<double>(cutoff_count));
}

std::pair<double, size_t> RiskEngine::CalculateMaxDrawdown(std::span<const double> returns) noexcept {
    if (returns.empty()) return {0.0, 0};

    double peak = 1.0;
    double equity = 1.0;
    double max_dd = 0.0;
    size_t max_duration = 0;
    size_t current_duration = 0;

    for (double r : returns) {
        equity *= (1.0 + r);
        if (equity > peak) {
            peak = equity;
            current_duration = 0;
        } else {
            ++current_duration;
            double dd = (peak - equity) / peak;
            if (dd > max_dd) {
                max_dd = dd;
            }
            if (current_duration > max_duration) {
                max_duration = current_duration;
            }
        }
    }

    return {max_dd, max_duration};
}

RiskMetricsReport RiskEngine::Analyze(
    std::span<const double> returns,
    double risk_free_rate,
    double periods_per_year
) {
    RiskMetricsReport report{};
    if (returns.empty()) {
        return report;
    }

    const size_t n = returns.size();
    const double n_d = static_cast<double>(n);

    // 1. Mean
    double sum = 0.0;
    for (double r : returns) sum += r;
    report.sample_mean = sum / n_d;

    // 2. Variance & Standard Deviation
    double var_sum = 0.0;
    for (double r : returns) {
        double d = r - report.sample_mean;
        var_sum += d * d;
    }
    double variance = (n > 1) ? (var_sum / (n_d - 1.0)) : 0.0;
    report.sample_std_dev = std::sqrt(variance);

    // 3. Higher Moments (Skewness & Kurtosis)
    if (report.sample_std_dev > 1e-12 && n > 2) {
        double m3 = 0.0;
        double m4 = 0.0;
        for (double r : returns) {
            double d = (r - report.sample_mean) / report.sample_std_dev;
            m3 += d * d * d;
            m4 += d * d * d * d;
        }
        report.skewness = (n_d / ((n_d - 1.0) * (n_d - 2.0))) * m3;
        if (n > 3) {
            double c1 = (n_d * (n_d + 1.0)) / ((n_d - 1.0) * (n_d - 2.0) * (n_d - 3.0));
            double c2 = (3.0 * (n_d - 1.0) * (n_d - 1.0)) / ((n_d - 2.0) * (n_d - 3.0));
            report.excess_kurtosis = (c1 * m4) - c2;
        }
    }

    // 4. VaR metrics (95% and 99%)
    report.parametric_var_95 = ParametricVaR(report.sample_mean, report.sample_std_dev, 0.95);
    report.parametric_var_99 = ParametricVaR(report.sample_mean, report.sample_std_dev, 0.99);

    report.cornish_fisher_var_95 = CornishFisherVaR(
        report.sample_mean, report.sample_std_dev, report.skewness, report.excess_kurtosis, 0.95
    );
    report.cornish_fisher_var_99 = CornishFisherVaR(
        report.sample_mean, report.sample_std_dev, report.skewness, report.excess_kurtosis, 0.99
    );

    report.historical_var_95 = HistoricalVaR(returns, 0.95);
    report.historical_var_99 = HistoricalVaR(returns, 0.99);

    // 5. Expected Shortfall (CVaR)
    report.cvar_95 = ExpectedShortfall(returns, 0.95);
    report.cvar_99 = ExpectedShortfall(returns, 0.99);

    // 6. Drawdown
    auto [mdd, duration] = CalculateMaxDrawdown(returns);
    report.max_drawdown = mdd;
    report.max_drawdown_duration = duration;

    // 7. Annualized Ratios
    report.annualized_return = report.sample_mean * periods_per_year;
    report.annualized_volatility = report.sample_std_dev * std::sqrt(periods_per_year);

    double rf_period = risk_free_rate / periods_per_year;
    double excess_return = report.annualized_return - risk_free_rate;

    if (report.annualized_volatility > 1e-12) {
        report.sharpe_ratio = excess_return / report.annualized_volatility;
    }

    // Downside deviation for Sortino
    double down_sum = 0.0;
    for (double r : returns) {
        if (r < rf_period) {
            double diff = r - rf_period;
            down_sum += diff * diff;
        }
    }
    double down_dev = std::sqrt(down_sum / n_d) * std::sqrt(periods_per_year);
    if (down_dev > 1e-12) {
        report.sortino_ratio = excess_return / down_dev;
    }

    // Calmar ratio
    if (report.max_drawdown > 1e-12) {
        report.calmar_ratio = report.annualized_return / report.max_drawdown;
    }

    return report;
}

} // namespace quant
} // namespace compute
} // namespace obsidian

