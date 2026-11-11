#include "compute/quant/volatility_model.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>

namespace obsidian {
namespace compute {
namespace quant {

VolatilityModel::VolatilityModel(GarchParams params, double ewma_lambda)
    : params_(params),
      ewma_lambda_(std::clamp(ewma_lambda, 0.50, 0.999)),
      current_variance_(params.UnconditionalVariance()),
      current_ewma_variance_(params.UnconditionalVariance()),
      observations_(0) {
    if (current_variance_ <= 0.0) {
        current_variance_ = 1e-4; // Default baseline ~1% daily vol
        current_ewma_variance_ = 1e-4;
    }
}

void VolatilityModel::Update(double daily_return) {
    const double r2 = daily_return * daily_return;

    if (observations_ == 0) {
        current_variance_ = (params_.omega > 0.0) ? params_.omega + (params_.alpha + params_.beta) * r2 : r2;
        current_ewma_variance_ = r2;
    } else {
        // GARCH(1,1): sigma_t^2 = omega + alpha * r_{t-1}^2 + beta * sigma_{t-1}^2
        current_variance_ = params_.omega + (params_.alpha * r2) + (params_.beta * current_variance_);

        // EWMA (RiskMetrics): sigma_ewma^2 = lambda * sigma_{t-1}^2 + (1 - lambda) * r_t^2
        current_ewma_variance_ = (ewma_lambda_ * current_ewma_variance_) + ((1.0 - ewma_lambda_) * r2);
    }

    // Numerical safeguard against variance collapse or divergence
    if (current_variance_ < 1e-12) {
        current_variance_ = 1e-12;
    }
    if (current_ewma_variance_ < 1e-12) {
        current_ewma_variance_ = 1e-12;
    }

    ++observations_;
}

double VolatilityModel::ForecastVariance(size_t steps_ahead) const noexcept {
    if (steps_ahead == 0) {
        return current_variance_;
    }

    const double persistence = params_.alpha + params_.beta;
    const double v_long_run = params_.UnconditionalVariance();

    if (!params_.IsStationary() || v_long_run <= 0.0) {
        // Fallback for non-stationary parameters: flat persistence
        return current_variance_;
    }

    // sigma_{t+k}^2 = V_L + (alpha + beta)^(k-1) * (sigma_{t+1}^2 - V_L)
    if (steps_ahead == 1) {
        return current_variance_;
    }

    const double decay = std::pow(persistence, static_cast<double>(steps_ahead - 1));
    return v_long_run + decay * (current_variance_ - v_long_run);
}

double VolatilityModel::GetAnnualizedVolatility(double trading_days_per_year) const noexcept {
    return std::sqrt(current_variance_ * trading_days_per_year);
}

double VolatilityModel::GetEwmaVolatility() const noexcept {
    return std::sqrt(current_ewma_variance_);
}

double VolatilityModel::GetGarchVolatility() const noexcept {
    return std::sqrt(current_variance_);
}

VolatilityRegime VolatilityModel::GetCurrentRegime() const noexcept {
    const double v_lr = params_.UnconditionalVariance();
    double benchmark_sd = (v_lr > 0.0) ? std::sqrt(v_lr) : 0.02; // Default 2% daily vol
    double current_sd = std::sqrt(current_variance_);

    double ratio = current_sd / benchmark_sd;

    if (ratio < 0.75) {
        return VolatilityRegime::Low;
    } else if (ratio < 1.35) {
        return VolatilityRegime::Normal;
    } else if (ratio < 2.00) {
        return VolatilityRegime::Elevated;
    } else {
        return VolatilityRegime::Extreme;
    }
}

storage::Status VolatilityModel::FitSeries(std::span<const double> historical_returns) {
    if (historical_returns.empty()) {
        return storage::Status::InvalidArgument("historical_returns cannot be empty");
    }

    // 1. Calculate sample mean and variance
    double sum = 0.0;
    for (double r : historical_returns) {
        sum += r;
    }
    double mean = sum / static_cast<double>(historical_returns.size());

    double var_sum = 0.0;
    for (double r : historical_returns) {
        double diff = r - mean;
        var_sum += diff * diff;
    }
    double sample_var = (historical_returns.size() > 1) 
        ? var_sum / static_cast<double>(historical_returns.size() - 1)
        : 1e-4;

    if (sample_var < 1e-8) {
        sample_var = 1e-8;
    }

    // 2. Calibrate omega from sample variance: omega = sample_var * (1 - alpha - beta)
    const double persistence = params_.alpha + params_.beta;
    if (persistence < 1.0) {
        params_.omega = sample_var * (1.0 - persistence);
    }

    // 3. Re-run series through filter
    Reset();
    current_variance_ = sample_var;
    current_ewma_variance_ = sample_var;

    for (double r : historical_returns) {
        Update(r);
    }

    return storage::Status::Ok();
}

void VolatilityModel::Reset() {
    current_variance_ = params_.UnconditionalVariance();
    if (current_variance_ <= 0.0) {
        current_variance_ = 1e-4;
    }
    current_ewma_variance_ = current_variance_;
    observations_ = 0;
}

} // namespace quant
} // namespace compute
} // namespace obsidian

