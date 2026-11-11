#include "compute/quant/volatility_model.hpp"
#include "compute/quant/risk_engine.hpp"
#include "compute/quant/portfolio_allocator.hpp"
#include "engines/strategy/cpt_breakout_strategy.hpp"
#include "engines/strategy/pair_trading_strategy.hpp"
#include "crypto/feed_monitor.hpp"

#include <iostream>
#include <cassert>
#include <vector>
#include <cmath>

using namespace obsidian::compute::quant;
using namespace obsidian::engines::strategy;
using namespace obsidian::crypto;

void TestVolatilityModelGarchAndRegime() {
    std::cout << "[TEST] Running TestVolatilityModelGarchAndRegime...\n";

    GarchParams params{};
    params.omega = 1e-5;
    params.alpha = 0.09;
    params.beta = 0.89; // Persistence = 0.98 < 1.0
    assert(params.IsStationary());
    assert(params.UnconditionalVariance() > 0.0);

    VolatilityModel model(params);

    // Simulate 100 periods of zero and moderate shocks
    for (size_t i = 0; i < 50; ++i) {
        model.Update(0.01);
    }
    double var_1 = model.GetGarchVolatility();
    assert(var_1 > 0.0);

    // Introduce large shock (market crash)
    model.Update(-0.08);
    double var_after_shock = model.GetGarchVolatility();
    assert(var_after_shock > var_1);

    // Forward forecast should decay towards long run variance
    double f1 = model.ForecastVariance(1);
    double f10 = model.ForecastVariance(10);
    double f100 = model.ForecastVariance(100);
    assert(f1 > 0.0 && f10 > 0.0 && f100 > 0.0);

    // Regime classification
    VolatilityRegime regime = model.GetCurrentRegime();
    assert(regime == VolatilityRegime::Elevated || regime == VolatilityRegime::Extreme || regime == VolatilityRegime::Normal);

    // FitSeries test
    std::vector<double> history = {0.01, -0.02, 0.015, -0.005, 0.03, -0.025, 0.008};
    auto status = model.FitSeries(history);
    assert(status.IsOk());

    std::cout << "  -> PASSED\n";
}

void TestRiskEngineVaRAndCVaR() {
    std::cout << "[TEST] Running TestRiskEngineVaRAndCVaR...\n";

    // Standard normal quantiles: N(0.05) ~ -1.645, N(0.01) ~ -2.326
    double q95 = RiskEngine::NormalQuantile(0.05);
    double q99 = RiskEngine::NormalQuantile(0.01);
    assert(std::abs(q95 - (-1.64485)) < 0.001);
    assert(std::abs(q99 - (-2.32635)) < 0.001);

    // Synthesize returns with known characteristics
    std::vector<double> returns(1000, 0.001);
    // Add negative tail losses
    for (size_t i = 0; i < 50; ++i) {
        returns[i] = -0.05 - (0.001 * static_cast<double>(i));
    }

    auto report = RiskEngine::Analyze(returns, 0.02, 365.0);
    assert(report.sample_std_dev > 0.0);
    assert(report.parametric_var_95 > 0.0);
    assert(report.cornish_fisher_var_95 > 0.0);
    assert(report.historical_var_95 > 0.0);
    assert(report.cvar_95 >= report.historical_var_95); // Expected Shortfall >= VaR
    assert(report.max_drawdown >= 0.0 && report.max_drawdown <= 1.0);

    std::cout << "  -> PASSED\n";
}

void TestPortfolioAllocatorMarkowitzAndRiskParity() {
    std::cout << "[TEST] Running TestPortfolioAllocatorMarkowitzAndRiskParity...\n";

    // 3 Assets with historical returns
    const size_t num_assets = 3;
    const size_t num_obs = 50;
    std::vector<std::vector<double>> returns(num_assets, std::vector<double>(num_obs, 0.001));

    // Asset 0: low vol, Asset 1: med vol, Asset 2: high vol
    for (size_t t = 0; t < num_obs; ++t) {
        returns[0][t] = (t % 2 == 0) ? 0.01 : -0.008;
        returns[1][t] = (t % 2 == 0) ? 0.02 : -0.018;
        returns[2][t] = (t % 2 == 0) ? 0.04 : -0.035;
    }

    auto cov = PortfolioAllocator::ComputeShrunkCovariance(returns, 0.15);
    assert(cov.size() == num_assets);
    assert(cov[0][0] > 0.0 && cov[1][1] > 0.0 && cov[2][2] > 0.0);

    std::vector<double> mu = {0.05, 0.10, 0.18};

    // 1. Equal Weight
    auto ew = PortfolioAllocator::EqualWeight(num_assets, mu, cov);
    assert(ew.weights.size() == 3);
    assert(std::abs(ew.weights[0] - (1.0 / 3.0)) < 1e-4);

    // 2. Inverse Volatility
    auto iv = PortfolioAllocator::InverseVolatility(mu, cov);
    assert(iv.weights[0] > iv.weights[2]); // Lowest vol gets highest weight

    // 3. Minimum Variance
    auto mv = PortfolioAllocator::MinimumVariance(mu, cov, true);
    double sum_mv = 0.0;
    for (double w : mv.weights) {
        assert(w >= -1e-6); // Long only
        sum_mv += w;
    }
    assert(std::abs(sum_mv - 1.0) < 1e-4);

    // 4. Risk Parity (Equal Risk Contribution)
    auto rp = PortfolioAllocator::RiskParity(mu, cov);
    double sum_rp = 0.0;
    for (double w : rp.weights) {
        assert(w >= 0.0);
        sum_rp += w;
    }
    assert(std::abs(sum_rp - 1.0) < 1e-4);
    assert(rp.expected_volatility > 0.0);

    std::cout << "  -> PASSED\n";
}

void TestCPTBreakoutStrategyExecution() {
    std::cout << "[TEST] Running TestCPTBreakoutStrategyExecution...\n";

    CPTBreakoutConfig config{};
    config.lookback_period = 10;
    config.cpt_threshold = -1.0; // Allow entry for testing
    config.fixed_order_qty = 2.0;
    config.stop_loss_pct = 0.05;

    CPTBreakoutStrategy strategy("BTC/USDT", config);

    // Feed initial 10 bars in range [100, 105]
    uint64_t ts = 1000000000;
    for (int i = 0; i < 10; ++i) {
        MarketBar bar{"BTC/USDT", ts += 60000000000, 100.0, 105.0, 99.0, 102.0 + (i * 0.1), 50.0};
        strategy.OnBar(bar);
    }

    // 11th Bar: Massive upside breakout to 120.0
    MarketBar breakout_bar{"BTC/USDT", ts += 60000000000, 103.0, 122.0, 102.5, 120.0, 500.0};
    strategy.OnBar(breakout_bar);

    auto orders = strategy.DrainOrders();
    assert(!orders.empty());
    assert(orders[0].side == OrderSide::Buy);
    assert(orders[0].quantity == 2.0);

    // Simulate fill
    OrderFill fill{orders[0].order_id, "BTC/USDT", OrderSide::Buy, 120.0, 2.0, 0.1, ts};
    strategy.OnFill(fill);

    const auto& pos = strategy.GetPositions().at("BTC/USDT");
    assert(pos.quantity == 2.0);
    assert(pos.avg_entry_price == 120.0);

    // Stop loss trigger: Price drops to 110.0 (below 120 * 0.95 = 114)
    MarketBar crash_bar{"BTC/USDT", ts += 60000000000, 115.0, 115.0, 108.0, 110.0, 200.0};
    strategy.OnBar(crash_bar);

    auto exit_orders = strategy.DrainOrders();
    assert(!exit_orders.empty());
    assert(exit_orders[0].side == OrderSide::Sell);
    assert(exit_orders[0].quantity == 2.0);

    std::cout << "  -> PASSED\n";
}

void TestPairTradingStrategyMeanReversion() {
    std::cout << "[TEST] Running TestPairTradingStrategyMeanReversion...\n";

    PairTradingConfig config{};
    config.hedge_ratio = 1.0;
    config.lookback_window = 15;
    config.entry_z_score = 1.5;
    config.exit_z_score = 0.2;
    config.leg_a_qty = 1.0;

    PairTradingStrategy strategy("BTC", "ETH", config);

    uint64_t ts = 1000000000;
    // Feed 15 balanced bars with spread close to zero (e.g. BTC=100, ETH=100)
    for (int i = 0; i < 15; ++i) {
        MarketBar bar_a{"BTC", ts, 100.0, 101.0, 99.0, 100.0, 10.0};
        MarketBar bar_b{"ETH", ts, 100.0, 101.0, 99.0, 100.0, 10.0};
        strategy.OnBar(bar_a);
        strategy.OnBar(bar_b);
        ts += 60000000000;
    }

    // Divergence: BTC drops to 80, ETH remains at 100 (Spread becomes negative, z-score << -1.5)
    for (int i = 0; i < 3; ++i) {
        MarketBar div_a{"BTC", ts, 85.0, 85.0, 78.0, 80.0, 20.0};
        MarketBar div_b{"ETH", ts, 100.0, 101.0, 99.0, 100.0, 10.0};
        strategy.OnBar(div_a);
        strategy.OnBar(div_b);
        ts += 60000000000;
    }

    assert(strategy.GetCurrentZScore() < -1.0);
    assert(strategy.GetState() == PairPositionState::LongSpread);

    auto orders = strategy.DrainOrders();
    assert(orders.size() >= 2); // 1 buy for BTC, 1 sell for ETH

    std::cout << "  -> PASSED\n";
}

void TestCryptoFeedMonitorJitterAndGaps() {
    std::cout << "[TEST] Running TestCryptoFeedMonitorJitterAndGaps...\n";

    FeedMonitorConfig config{};
    config.stale_timeout_ns = 500'000'000; // 500 ms
    FeedMonitor monitor(config);

    uint64_t base_ex_ts = 1'000'000'000'000;
    uint64_t base_rx_ts = base_ex_ts + 2'000'000; // 2 ms latency

    // 1. Ingest normal packets 1, 2, 3
    for (uint64_t seq = 1; seq <= 3; ++seq) {
        FeedPacket p{};
        p.sequence_id = seq;
        p.exchange_ts_ns = base_ex_ts + (seq * 10'000'000);
        p.received_ts_ns = base_rx_ts + (seq * 10'000'000);
        p.symbol = "BTC-USDT";
        p.bid_price = 50000.0;
        p.ask_price = 50001.0;
        p.bid_qty = 1.5;
        p.ask_qty = 2.0;
        p.last_price = 50000.5;
        monitor.ProcessPacket(p);
    }

    auto initial_anomalies = monitor.DrainAnomalies();
    assert(initial_anomalies.empty());

    // 2. Packet gap: jump from sequence 3 to 7 (missed 4, 5, 6)
    FeedPacket gap_p{};
    gap_p.sequence_id = 7;
    gap_p.exchange_ts_ns = base_ex_ts + (7 * 10'000'000);
    gap_p.received_ts_ns = base_rx_ts + (7 * 10'000'000);
    gap_p.symbol = "BTC-USDT";
    gap_p.bid_price = 50000.0;
    gap_p.ask_price = 50001.0;
    monitor.ProcessPacket(gap_p);

    auto gap_anomalies = monitor.DrainAnomalies();
    assert(!gap_anomalies.empty());
    assert(gap_anomalies[0].type == FeedAnomalyType::SequenceGap);

    // 3. Crossed book anomaly: Bid (50005) >= Ask (50000)
    FeedPacket crossed_p{};
    crossed_p.sequence_id = 8;
    crossed_p.exchange_ts_ns = base_ex_ts + (8 * 10'000'000);
    crossed_p.received_ts_ns = base_rx_ts + (8 * 10'000'000);
    crossed_p.symbol = "BTC-USDT";
    crossed_p.bid_price = 50005.0;
    crossed_p.ask_price = 50000.0;
    monitor.ProcessPacket(crossed_p);

    auto crossed_anomalies = monitor.DrainAnomalies();
    assert(!crossed_anomalies.empty());
    assert(crossed_anomalies[0].type == FeedAnomalyType::CrossedBook);

    // 4. Watchdog stale feed check: Advance time by 1 second (1000 ms > 500 ms)
    monitor.CheckHeartbeat(crossed_p.received_ts_ns + 1'000'000'000);
    auto stale_anomalies = monitor.DrainAnomalies();
    assert(!stale_anomalies.empty());
    assert(stale_anomalies[0].type == FeedAnomalyType::StaleFeed);

    auto stats = monitor.GetStats("BTC-USDT");
    assert(stats.packets_received == 5);
    assert(stats.sequence_gaps == 3);
    assert(stats.crossed_book_events == 1);
    assert(stats.stale_feed_events == 1);

    std::cout << "  -> PASSED\n";
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "  Obsidian Mega Engine & Quantitative Model Tests\n";
    std::cout << "========================================================\n";

    TestVolatilityModelGarchAndRegime();
    TestRiskEngineVaRAndCVaR();
    TestPortfolioAllocatorMarkowitzAndRiskParity();
    TestCPTBreakoutStrategyExecution();
    TestPairTradingStrategyMeanReversion();
    TestCryptoFeedMonitorJitterAndGaps();

    std::cout << "========================================================\n";
    std::cout << "  ALL TESTS PASSED SUCCESSFULLY (6/6 SUITES)\n";
    std::cout << "========================================================\n";
    return 0;
}

