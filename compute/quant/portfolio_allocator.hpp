#pragma once

#include "storage/common/status.hpp"
#include <vector>
#include <span>
#include <string>
#include <cstddef>

namespace obsidian {
namespace compute {
namespace quant {

enum class AllocationMethod : uint8_t {
    EqualWeight = 0,
    InverseVolatility = 1,
    MinimumVariance = 2,
    MaxSharpe = 3,
    RiskParity = 4
};

struct AllocationResult {
    std::vector<double> weights;
    double expected_return{0.0};
    double expected_volatility{0.0};
    double portfolio_variance{0.0};
    double sharpe_ratio{0.0};
    double diversification_ratio{0.0};
};

class PortfolioAllocator {
public:
    // Computes sample covariance matrix with Ledoit-Wolf shrinkage towards diagonal target
    [[nodiscard]] static std::vector<std::vector<double>> ComputeShrunkCovariance(
        const std::vector<std::vector<double>>& asset_returns,
        double shrinkage_intensity = 0.20
    );

    // Equal-weighted allocation (1/N benchmark)
    [[nodiscard]] static AllocationResult EqualWeight(
        size_t num_assets,
        const std::vector<double>& expected_returns,
        const std::vector<std::vector<double>>& cov_matrix
    );

    // Inverse volatility weighting
    [[nodiscard]] static AllocationResult InverseVolatility(
        const std::vector<double>& expected_returns,
        const std::vector<std::vector<double>>& cov_matrix
    );

    // Global Minimum Variance (Long-only)
    [[nodiscard]] static AllocationResult MinimumVariance(
        const std::vector<double>& expected_returns,
        const std::vector<std::vector<double>>& cov_matrix,
        bool long_only = true
    );

    // Tangency / Maximum Sharpe Portfolio
    [[nodiscard]] static AllocationResult MaxSharpe(
        const std::vector<double>& expected_returns,
        const std::vector<std::vector<double>>& cov_matrix,
        double risk_free_rate = 0.0,
        bool long_only = true
    );

    // Equal Risk Contribution (Risk Parity via Cyclical Coordinate Descent)
    [[nodiscard]] static AllocationResult RiskParity(
        const std::vector<double>& expected_returns,
        const std::vector<std::vector<double>>& cov_matrix,
        size_t max_iterations = 200,
        double tolerance = 1e-6
    );

    // Invert symmetric positive definite matrix using Gauss-Jordan with Tikhonov ridge
    [[nodiscard]] static bool InvertMatrix(
        const std::vector<std::vector<double>>& A,
        std::vector<std::vector<double>>& A_inv,
        double ridge_eps = 1e-6
    );

    // Matrix-vector multiplication
    [[nodiscard]] static std::vector<double> MatVecMul(
        const std::vector<std::vector<double>>& M,
        const std::vector<double>& v
    );

    // Quadratic form: w^T * Sigma * w
    [[nodiscard]] static double QuadForm(
        const std::vector<double>& w,
        const std::vector<std::vector<double>>& Sigma
    );
};

} // namespace quant
} // namespace compute
} // namespace obsidian
