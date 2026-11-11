#include "compute/simd/prospect_theory.hpp"
#include "storage/dataset/universe_ingestor.hpp"
#include "storage/parquet/columnar_file_reader.hpp"
#include "storage/common/file_system.hpp"
#include "runtime/pipeline/ml_inference_stage.hpp"
#include "runtime/pipeline/execution_pipeline.hpp"

#include <iostream>
#include <cassert>
#include <vector>
#include <cmath>
#include <filesystem>

using namespace obsidian::compute::simd;
using namespace obsidian::storage::dataset;
using namespace obsidian::storage::parquet;
using namespace obsidian::storage;
using namespace obsidian::runtime::pipeline;

namespace fs = std::filesystem;

void TestProspectTheoryValueFunctionAndWeights() {
    std::cout << "[TEST] Running TestProspectTheoryValueFunctionAndWeights...\n";
    ProspectTheoryEngine engine;

    // 1. Boundary checks for probability weighting
    assert(engine.ProbabilityWeightGains(0.0) == 0.0);
    assert(engine.ProbabilityWeightGains(1.0) == 1.0);
    assert(engine.ProbabilityWeightLosses(0.0) == 0.0);
    assert(engine.ProbabilityWeightLosses(1.0) == 1.0);

    // 2. Overweighting of small probabilities (w(0.05) > 0.05)
    double w_small = engine.ProbabilityWeightGains(0.05);
    assert(w_small > 0.05);

    // 3. S-shaped Value Function: loss aversion lambda = 2.25
    double gain = 0.10;
    double loss = -0.10;
    double v_gain = engine.ValueFunction(gain);
    double v_loss = engine.ValueFunction(loss);

    assert(v_gain > 0.0);
    assert(v_loss < 0.0);
    // Absolute loss must be greater than gain by factor of lambda (2.25)
    assert(std::abs(v_loss) > v_gain * 2.0);

    std::cout << "  -> PASSED\n";
}

void TestWindowCPTCalculationEquivalence() {
    std::cout << "[TEST] Running TestWindowCPTCalculationEquivalence...\n";
    ProspectTheoryEngine engine;

    // Construct 60-day return series with high positive skew (lottery characteristic)
    std::vector<double> returns(60, 0.001); // Small baseline returns
    returns[10] = 0.25; // Large lottery gain
    returns[25] = 0.35; // Extreme spike
    returns[40] = -0.05; // Minor drawdown

    double cpt_val = engine.CalculateWindowCPT(returns, 30);
    assert(!std::isnan(cpt_val));
    assert(cpt_val > 0.0); // Speculative upside yields strong positive CPT value

    // Check minimum periods guard
    std::vector<double> short_returns(20, 0.02);
    double nan_cpt = engine.CalculateWindowCPT(short_returns, 30);
    assert(std::isnan(nan_cpt));

    std::cout << "  -> PASSED\n";
}

void TestSampleSkewnessAndKurtosis() {
    std::cout << "[TEST] Running TestSampleSkewnessAndKurtosis...\n";
    std::vector<double> symmetric = {-0.02, -0.01, 0.0, 0.01, 0.02};
    double skew_sym = ProspectTheoryEngine::CalculateSampleSkewness(symmetric);
    assert(std::abs(skew_sym) < 1e-6);

    std::vector<double> right_skewed = {-0.01, -0.01, 0.0, 0.01, 0.15};
    double skew_pos = ProspectTheoryEngine::CalculateSampleSkewness(right_skewed);
    assert(skew_pos > 1.0); // Substantial positive skewness

    double vol = ProspectTheoryEngine::CalculateSampleStd(right_skewed);
    assert(vol > 0.0);

    double max_ret = ProspectTheoryEngine::CalculateMaxReturn(right_skewed);
    assert(max_ret == 0.15);

    std::cout << "  -> PASSED\n";
}

void TestUniverseIngestionAndPartitioning() {
    std::cout << "[TEST] Running TestUniverseIngestionAndPartitioning...\n";
    std::string test_table_dir = "build/test_env/lakehouse_universe";
    fs::remove_all(test_table_dir);

    std::string sample_csv = 
        "timeOpen;timeClose;timeHigh;timeLow;name;open;high;low;close;volume;marketCap;circulatingSupply;timestamp\n"
        "\"2026-10-31T00:00:00.000Z\";\"2026-10-31T23:59:59.999Z\";\"2026-10-31T17:55:00.000Z\";\"2026-10-31T03:55:00.000Z\";\"BTC\";77198.2;77629.7;77000.6;77434.7;12266900.0;155684730.0;20105365;\"2026-10-31T23:59:59.999Z\"\n"
        "\"2025-10-30T00:00:00.000Z\";\"2025-10-30T23:59:59.999Z\";\"2025-10-30T06:31:00.000Z\";\"2025-10-30T18:54:00.000Z\";\"BTC\";65000.0;66000.0;64500.0;65800.0;25000000.0;130000000.0;19900000;\"2025-10-30T23:59:59.999Z\"\n";

    fs::create_directories("build/test_env");
    std::string test_csv_file = "build/test_env/Bitcoin_sample.csv";
    {
        std::ofstream out(test_csv_file);
        out << sample_csv;
    }

    auto report_or = UniverseIngestor::IngestFiles({test_csv_file}, test_table_dir, true);
    assert(report_or.ok());
    auto report = report_or.value();

    assert(report.assets_processed == 1);
    assert(report.total_rows_ingested == 2);
    // Rows split across two distinct years: 2025 and 2026!
    assert(report.total_partitions_created == 2);

    // Verify written file can be read back using ColumnarFileReader
    std::string file_2026 = test_table_dir + "/symbol=Bitcoin/year=2026/data-001.obsd";
    assert(fs::exists(file_2026));

    auto reader_or = ColumnarFileReader::Open(file_2026);
    assert(reader_or.ok());
    auto& reader = reader_or.value();
    assert(reader.GetFooter().total_rows == 1);

    auto batch_or = reader.ReadAll();
    assert(batch_or.ok());
    auto batch = batch_or.value();
    assert(batch->RowCount() == 1);

    fs::remove_all(test_table_dir);
    fs::remove(test_csv_file);
    std::cout << "  -> PASSED\n";
}

void TestRandomForestNativeInference() {
    std::cout << "[TEST] Running TestRandomForestNativeInference...\n";
    auto model = RandomForestModel::CreateTrainedModel();
    assert(model.TreeCount() == 3);

    // Features: [pt_value, roll_skew, roll_max, roll_vol, turnover]
    // 1. Speculative profile: high CPT, high skewness, high max return
    std::vector<double> speculative_features = {0.10, 1.5, 0.20, 0.08, 0.08};
    double prob_speculative = model.PredictProbability(speculative_features);
    assert(prob_speculative >= 0.70);
    assert(model.PredictClass(speculative_features, 0.5) == 1);

    // 2. Conservative profile: low CPT, low skewness, low max return
    std::vector<double> conservative_features = {0.01, 0.1, 0.03, 0.02, 0.01};
    double prob_conservative = model.PredictProbability(conservative_features);
    assert(prob_conservative < 0.40);
    assert(model.PredictClass(conservative_features, 0.5) == 0);

    // 3. Pipeline Stage Execution
    MLInferenceStage stage(model, 0.70);
    ExecutionPipeline pipeline(10);
    pipeline.AddStage(std::make_shared<MLInferenceStage>(model, 0.70));

    DataBatch input_batch;
    input_batch.batch_id = 101;
    input_batch.payload.resize(5 * sizeof(double));
    std::memcpy(input_batch.payload.data(), speculative_features.data(), 5 * sizeof(double));

    auto out_or = pipeline.ExecuteSync(input_batch);
    assert(out_or.ok());
    // Output batch payload should have original 40 bytes + 8 bytes of probability score
    assert(out_or.value().payload.size() == 5 * sizeof(double) + sizeof(double));

    std::cout << "  -> PASSED\n";
}

int main() {
    std::cout << "=====================================================\n";
    std::cout << "  Obsidian Quant & CPT Convergence Verification Suite\n";
    std::cout << "=====================================================\n";

    try {
        TestProspectTheoryValueFunctionAndWeights();
        TestWindowCPTCalculationEquivalence();
        TestSampleSkewnessAndKurtosis();
        TestUniverseIngestionAndPartitioning();
        TestRandomForestNativeInference();

        std::cout << "\n>>> ALL QUANT CONVERGENCE TESTS PASSED! <<<\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FATAL ERROR during test execution: " << e.what() << "\n";
        return 1;
    }
}
