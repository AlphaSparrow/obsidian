#include "compute/quant/portfolio_allocator.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>

namespace obsidian {
namespace compute {
namespace quant {

std::vector<std::vector<double>> PortfolioAllocator::ComputeShrunkCovariance(
    const std::vector<std::vector<double>>& asset_returns,
    double shrinkage_intensity
) {
    if (asset_returns.empty()) return {};
    const size_t num_assets = asset_returns.size();
    const size_t num_obs = asset_returns[0].size();
    if (num_obs < 2) return {};

    // 1. Calculate sample means
    std::vector<double> means(num_assets, 0.0);
    for (size_t i = 0; i < num_assets; ++i) {
        double sum = 0.0;
        for (double r : asset_returns[i]) sum += r;
        means[i] = sum / static_cast<double>(num_obs);
    }

    // 2. Sample covariance
    std::vector<std::vector<double>> sample_cov(num_assets, std::vector<double>(num_assets, 0.0));
    const double factor = 1.0 / static_cast<double>(num_obs - 1);

    for (size_t i = 0; i < num_assets; ++i) {
        for (size_t j = i; j < num_assets; ++j) {
            double cov_sum = 0.0;
            for (size_t t = 0; t < num_obs; ++t) {
                cov_sum += (asset_returns[i][t] - means[i]) * (asset_returns[j][t] - means[j]);
            }
            double val = cov_sum * factor;
            sample_cov[i][j] = val;
            sample_cov[j][i] = val;
        }
    }

    // 3. Shrinkage towards diagonal target F = diag(sample_cov)
    const double delta = std::clamp(shrinkage_intensity, 0.0, 1.0);
    std::vector<std::vector<double>> shrunk_cov(num_assets, std::vector<double>(num_assets, 0.0));

    for (size_t i = 0; i < num_assets; ++i) {
        for (size_t j = 0; j < num_assets; ++j) {
            if (i == j) {
                shrunk_cov[i][j] = sample_cov[i][j];
            } else {
                shrunk_cov[i][j] = (1.0 - delta) * sample_cov[i][j];
            }
        }
    }

    return shrunk_cov;
}

std::vector<double> PortfolioAllocator::MatVecMul(
    const std::vector<std::vector<double>>& M,
    const std::vector<double>& v
) {
    const size_t rows = M.size();
    if (rows == 0) return {};
    const size_t cols = M[0].size();
    std::vector<double> res(rows, 0.0);

    for (size_t i = 0; i < rows; ++i) {
        double acc = 0.0;
        for (size_t j = 0; j < cols && j < v.size(); ++j) {
            acc += M[i][j] * v[j];
        }
        res[i] = acc;
    }
    return res;
}

double PortfolioAllocator::QuadForm(
    const std::vector<double>& w,
    const std::vector<std::vector<double>>& Sigma
) {
    auto sigma_w = MatVecMul(Sigma, w);
    double variance = 0.0;
    for (size_t i = 0; i < w.size() && i < sigma_w.size(); ++i) {
        variance += w[i] * sigma_w[i];
    }
    return (variance > 0.0) ? variance : 0.0;
}

bool PortfolioAllocator::InvertMatrix(
    const std::vector<std::vector<double>>& A,
    std::vector<std::vector<double>>& A_inv,
    double ridge_eps
) {
    const size_t n = A.size();
    if (n == 0 || A[0].size() != n) return false;

    // Augmented matrix [A + ridge*I | I]
    std::vector<std::vector<double>> aug(n, std::vector<double>(2 * n, 0.0));
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < n; ++j) {
            aug[i][j] = A[i][j];
        }
        aug[i][i] += ridge_eps; // Tikhonov regularization
        aug[i][n + i] = 1.0;
    }

    // Gauss-Jordan elimination with partial pivoting
    for (size_t col = 0; col < n; ++col) {
        size_t pivot_row = col;
        double max_val = std::abs(aug[col][col]);
        for (size_t r = col + 1; r < n; ++r) {
            double v = std::abs(aug[r][col]);
            if (v > max_val) {
                max_val = v;
                pivot_row = r;
            }
        }

        if (max_val < 1e-14) {
            return false; // Singular matrix
        }

        if (pivot_row != col) {
            std::swap(aug[pivot_row], aug[col]);
        }

        double pivot = aug[col][col];
        for (size_t j = 0; j < 2 * n; ++j) {
            aug[col][j] /= pivot;
        }

        for (size_t r = 0; r < n; ++r) {
            if (r != col) {
                double factor = aug[r][col];
                for (size_t j = col; j < 2 * n; ++j) {
                    aug[r][j] -= factor * aug[col][j];
                }
            }
        }
    }

    // Extract inverse
    A_inv.assign(n, std::vector<double>(n, 0.0));
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < n; ++j) {
            A_inv[i][j] = aug[i][n + j];
        }
    }
    return true;
}

static AllocationResult BuildMetrics(
    std::vector<double> weights,
    const std::vector<double>& expected_returns,
    const std::vector<std::vector<double>>& cov_matrix
) {
    AllocationResult res;
    res.weights = std::move(weights);
    res.portfolio_variance = PortfolioAllocator::QuadForm(res.weights, cov_matrix);
    res.expected_volatility = std::sqrt(res.portfolio_variance);

    res.expected_return = 0.0;
    double weighted_asset_vol_sum = 0.0;
    for (size_t i = 0; i < res.weights.size(); ++i) {
        if (i < expected_returns.size()) {
            res.expected_return += res.weights[i] * expected_returns[i];
        }
        if (i < cov_matrix.size()) {
            double asset_vol = std::sqrt(std::max(0.0, cov_matrix[i][i]));
            weighted_asset_vol_sum += res.weights[i] * asset_vol;
        }
    }

    if (res.expected_volatility > 1e-12) {
        res.sharpe_ratio = res.expected_return / res.expected_volatility;
        res.diversification_ratio = weighted_asset_vol_sum / res.expected_volatility;
    }

    return res;
}

AllocationResult PortfolioAllocator::EqualWeight(
    size_t num_assets,
    const std::vector<double>& expected_returns,
    const std::vector<std::vector<double>>& cov_matrix
) {
    if (num_assets == 0) return {};
    double w = 1.0 / static_cast<double>(num_assets);
    std::vector<double> weights(num_assets, w);
    return BuildMetrics(std::move(weights), expected_returns, cov_matrix);
}

AllocationResult PortfolioAllocator::InverseVolatility(
    const std::vector<double>& expected_returns,
    const std::vector<std::vector<double>>& cov_matrix
) {
    const size_t n = cov_matrix.size();
    if (n == 0) return {};

    std::vector<double> inv_vols(n, 0.0);
    double sum_inv_vol = 0.0;

    for (size_t i = 0; i < n; ++i) {
        double var = cov_matrix[i][i];
        double vol = (var > 1e-12) ? std::sqrt(var) : 1e-6;
        inv_vols[i] = 1.0 / vol;
        sum_inv_vol += inv_vols[i];
    }

    std::vector<double> weights(n, 1.0 / static_cast<double>(n));
    if (sum_inv_vol > 0.0) {
        for (size_t i = 0; i < n; ++i) {
            weights[i] = inv_vols[i] / sum_inv_vol;
        }
    }

    return BuildMetrics(std::move(weights), expected_returns, cov_matrix);
}

AllocationResult PortfolioAllocator::MinimumVariance(
    const std::vector<double>& expected_returns,
    const std::vector<std::vector<double>>& cov_matrix,
    bool long_only
) {
    const size_t n = cov_matrix.size();
    if (n == 0) return {};

    std::vector<std::vector<double>> inv_cov;
    if (!InvertMatrix(cov_matrix, inv_cov)) {
        return EqualWeight(n, expected_returns, cov_matrix);
    }

    // w = inv_cov * 1 / (1^T * inv_cov * 1)
    std::vector<double> ones(n, 1.0);
    auto raw_w = MatVecMul(inv_cov, ones);

    if (long_only) {
        for (double& w : raw_w) {
            if (w < 0.0) w = 0.0;
        }
    }

    double sum = 0.0;
    for (double w : raw_w) sum += w;

    std::vector<double> weights(n, 1.0 / static_cast<double>(n));
    if (sum > 1e-12) {
        for (size_t i = 0; i < n; ++i) {
            weights[i] = raw_w[i] / sum;
        }
    }

    return BuildMetrics(std::move(weights), expected_returns, cov_matrix);
}

AllocationResult PortfolioAllocator::MaxSharpe(
    const std::vector<double>& expected_returns,
    const std::vector<std::vector<double>>& cov_matrix,
    double risk_free_rate,
    bool long_only
) {
    const size_t n = cov_matrix.size();
    if (n == 0) return {};

    std::vector<std::vector<double>> inv_cov;
    if (!InvertMatrix(cov_matrix, inv_cov)) {
        return EqualWeight(n, expected_returns, cov_matrix);
    }

    // Excess returns vector
    std::vector<double> excess_returns(n, 0.0);
    for (size_t i = 0; i < n; ++i) {
        excess_returns[i] = (i < expected_returns.size()) ? (expected_returns[i] - risk_free_rate) : 0.0;
    }

    auto raw_w = MatVecMul(inv_cov, excess_returns);

    if (long_only) {
        for (double& w : raw_w) {
            if (w < 0.0) w = 0.0;
        }
    }

    double sum = 0.0;
    for (double w : raw_w) sum += w;

    std::vector<double> weights(n, 1.0 / static_cast<double>(n));
    if (sum > 1e-12) {
        for (size_t i = 0; i < n; ++i) {
            weights[i] = raw_w[i] / sum;
        }
    }

    return BuildMetrics(std::move(weights), expected_returns, cov_matrix);
}

AllocationResult PortfolioAllocator::RiskParity(
    const std::vector<double>& expected_returns,
    const std::vector<std::vector<double>>& cov_matrix,
    size_t max_iterations,
    double tolerance
) {
    const size_t n = cov_matrix.size();
    if (n == 0) return {};

    // Initial weights from inverse volatility
    auto init_alloc = InverseVolatility(expected_returns, cov_matrix);
    std::vector<double> w = init_alloc.weights;

    // Cyclical coordinate descent for Equal Risk Contribution
    // Target risk contribution per asset is sigma_p^2 / n
    for (size_t iter = 0; iter < max_iterations; ++iter) {
        double max_diff = 0.0;
        auto sigma_w = MatVecMul(cov_matrix, w);
        double port_var = QuadForm(w, cov_matrix);
        double target_rc = port_var / static_cast<double>(n);

        for (size_t i = 0; i < n; ++i) {
            double a = cov_matrix[i][i];
            double b = sigma_w[i] - (cov_matrix[i][i] * w[i]);
            // Solve a * w_i^2 + b * w_i - target_rc = 0
            double discriminant = (b * b) + (4.0 * a * target_rc);
            double new_w_i = (discriminant >= 0.0 && a > 1e-12)
                ? (-b + std::sqrt(discriminant)) / (2.0 * a)
                : w[i];

            if (new_w_i < 0.0) new_w_i = 1e-6;

            max_diff = std::max(max_diff, std::abs(new_w_i - w[i]));
            w[i] = new_w_i;
            sigma_w = MatVecMul(cov_matrix, w);
        }

        // Renormalize
        double sum = 0.0;
        for (double val : w) sum += val;
        if (sum > 0.0) {
            for (double& val : w) val /= sum;
        }

        if (max_diff < tolerance) {
            break;
        }
    }

    return BuildMetrics(std::move(w), expected_returns, cov_matrix);
}

} // namespace quant
} // namespace compute
} // namespace obsidian

