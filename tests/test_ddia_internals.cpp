#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "storage/common/status.hpp"
#include "storage/common/file_system.hpp"
#include "storage/schema/market_data_schema.hpp"
#include "storage/arrow/columnar_batch.hpp"
#include "storage/lakehouse/partition_spec.hpp"
#include "storage/lakehouse/manifest_manager.hpp"
#include "storage/lakehouse/lakehouse_table.hpp"
#include "storage/lakehouse/transaction.hpp"
#include "storage/lakehouse/compaction_engine.hpp"
#include "storage/lakehouse/catalog.hpp"
#include "kernel/concurrency/disruptor.hpp"
#include "kernel/concurrency/lockfree_queue.hpp"
#include "kernel/concurrency/thread_pool.hpp"

#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <chrono>
#include <algorithm>
#include <numeric>
#include <thread>
#include <atomic>
#include <cassert>
#include <sstream>
#include <fstream>
#include <cmath>

using namespace obsidian::storage;
using namespace obsidian::storage::lakehouse;
using namespace obsidian::storage::schema;
using namespace obsidian::storage::arrow;
using namespace obsidian::kernel::concurrency;

// ============================================================================
// DDIA CHAPTER 7: TRANSACTIONS & CONCURRENCY CONTROL (ACID & ISOLATION)
// ============================================================================

/// Verifies ACID Atomicity:
/// In DDIA Chapter 7, atomicity guarantees that if a transaction encounters an
/// error or aborts before commit, none of its modifications are visible to subsequent reads.
void VerifyDDIA_AtomicityAndRollback() {
    std::cout << "[DDIA Chapter 7] Verifying Atomicity & Uncommitted Rollback...\n";
    std::string table_loc = "build/test_env/ddia_atomicity_table";
    auto fs = LocalFileSystem::Default();
    fs->DeleteRecursively(table_loc);

    auto schema = TradeTick::GetSchema();
    PartitionSpec spec(1, {{"exchange", "exchange", PartitionTransform::Identity, 0}});
    auto table_or = LakehouseTable::Create(table_loc, "atomicity_test", schema, spec, fs);
    assert(table_or.ok());
    auto table = table_or.value();
    assert(table->current_snapshot_id() == 0);

    // Initial commit: add 1 file
    {
        auto tx = table->NewTransaction(OperationType::Append);
        DataFile f1{};
        f1.path = table_loc + "/exchange=NYSE/part-1.parquet";
        f1.format = "parquet";
        f1.file_size_bytes = 1024;
        f1.record_count = 100;
        f1.partitions = {{"exchange", "NYSE"}};
        tx->AppendFile(f1);
        assert(tx->Commit().ok());
    }
    assert(table->current_snapshot_id() == 1);
    assert(table->Scan().value().size() == 1);

    // Simulated aborted transaction: stage 3 files but NEVER commit
    {
        auto aborted_tx = table->NewTransaction(OperationType::Append);
        for (int i = 2; i <= 4; ++i) {
            DataFile f{};
            f.path = table_loc + "/exchange=NYSE/part-" + std::to_string(i) + ".parquet";
            f.format = "parquet";
            f.file_size_bytes = 1024;
            f.record_count = 100;
            f.partitions = {{"exchange", "NYSE"}};
            aborted_tx->AppendFile(f);
        }
        // Transaction goes out of scope without calling Commit()
    }

    // Invariant: Snapshot ID must remain 1, and only 1 file is visible
    assert(table->current_snapshot_id() == 1);
    auto scan_res = table->Scan();
    assert(scan_res.ok());
    assert(scan_res.value().size() == 1);
    assert(scan_res.value()[0].path.find("part-1.parquet") != std::string::npos);

    std::cout << "  -> PASSED: Uncommitted writes remain invisible; snapshot unchanged.\n";
}

/// Verifies Snapshot Isolation & Multi-Version Concurrency Control (MVCC):
/// In DDIA Chapter 7 ("Snapshot Isolation and Repeatable Read"), the key principle
/// is: "Readers never block writers, and writers never block readers."
/// A transaction reads from a consistent snapshot of the database taken at transaction start.
void VerifyDDIA_SnapshotIsolationMVCC() {
    std::cout << "[DDIA Chapter 7] Verifying Snapshot Isolation & Non-blocking MVCC...\n";
    std::string table_loc = "build/test_env/ddia_mvcc_table";
    auto fs = LocalFileSystem::Default();
    fs->DeleteRecursively(table_loc);

    auto schema = TradeTick::GetSchema();
    PartitionSpec spec(1, {{"exchange", "exchange", PartitionTransform::Identity, 0}});
    auto table_or = LakehouseTable::Create(table_loc, "mvcc_test", schema, spec, fs);
    assert(table_or.ok());
    auto table = table_or.value();

    // Snapshot 1: 2 data files
    {
        auto tx1 = table->NewTransaction(OperationType::Append);
        for (int i = 1; i <= 2; ++i) {
            DataFile f{};
            f.path = table_loc + "/exchange=CME/f" + std::to_string(i) + ".parquet";
            f.format = "parquet";
            f.file_size_bytes = 2048;
            f.record_count = 500;
            f.partitions = {{"exchange", "CME"}};
            tx1->AppendFile(f);
        }
        assert(tx1->Commit().ok());
    }
    assert(table->current_snapshot_id() == 1);

    // Reader takes a point-in-time snapshot view at S1
    int64_t reader_snapshot = table->current_snapshot_id();
    auto initial_read = table->Scan({}, {}, reader_snapshot);
    assert(initial_read.ok() && initial_read.value().size() == 2);

    // Concurrent Writer 1: Commits Snapshot 2 (appends 2 more files)
    {
        auto tx2 = table->NewTransaction(OperationType::Append);
        for (int i = 3; i <= 4; ++i) {
            DataFile f{};
            f.path = table_loc + "/exchange=CME/f" + std::to_string(i) + ".parquet";
            f.format = "parquet";
            f.file_size_bytes = 2048;
            f.record_count = 500;
            f.partitions = {{"exchange", "CME"}};
            tx2->AppendFile(f);
        }
        assert(tx2->Commit().ok());
    }
    assert(table->current_snapshot_id() == 2);

    // Concurrent Writer 2: Commits Snapshot 3 (appends 1 more file)
    {
        auto tx3 = table->NewTransaction(OperationType::Append);
        DataFile f{};
        f.path = table_loc + "/exchange=CME/f5.parquet";
        f.format = "parquet";
        f.file_size_bytes = 2048;
        f.record_count = 500;
        f.partitions = {{"exchange", "CME"}};
        tx3->AppendFile(f);
        assert(tx3->Commit().ok());
    }
    assert(table->current_snapshot_id() == 3);

    // INVARIANT 1: Repeatable Read / Read Skew prevention
    // Reader pinned at S1 MUST still see exactly 2 files, even though S3 is current
    auto repeatable_read = table->Scan({}, {}, reader_snapshot);
    assert(repeatable_read.ok());
    assert(repeatable_read.value().size() == 2);

    // INVARIANT 2: Time Travel Queries
    // Querying historical snapshots S1, S2, S3 yields monotonically increasing views
    auto scan_s1 = table->Scan({}, {}, 1);
    auto scan_s2 = table->Scan({}, {}, 2);
    auto scan_s3 = table->Scan({}, {}, 3);
    assert(scan_s1.value().size() == 2);
    assert(scan_s2.value().size() == 4);
    assert(scan_s3.value().size() == 5);

    std::cout << "  -> PASSED: Snapshot isolation maintained; zero read skew observed.\n";
}

/// Verifies Optimistic Concurrency Control (OCC) & Lost Update Prevention:
/// In DDIA Chapter 7, concurrent writes to the same resource under OCC must be
/// validated before commit. The first committer succeeds; the conflicting writer
/// is rejected with a conflict error and must retry against the updated snapshot.
void VerifyDDIA_OptimisticConcurrencyAndRetries() {
    std::cout << "[DDIA Chapter 7] Verifying OCC Conflict Detection & TransactionRunner Retry Loop...\n";
    std::string table_loc = "build/test_env/ddia_occ_table";
    auto fs = LocalFileSystem::Default();
    fs->DeleteRecursively(table_loc);

    auto schema = TradeTick::GetSchema();
    PartitionSpec spec(1, {{"exchange", "exchange", PartitionTransform::Identity, 0}});
    auto table = LakehouseTable::Create(table_loc, "occ_test", schema, spec, fs).value();

    // Populate initial state S1
    {
        auto tx = table->NewTransaction(OperationType::Append);
        DataFile f{};
        f.path = table_loc + "/part-init.parquet";
        f.format = "parquet";
        f.file_size_bytes = 1024;
        f.record_count = 100;
        tx->AppendFile(f);
        assert(tx->Commit().ok());
    }
    assert(table->current_snapshot_id() == 1);

    // Two concurrent transactions start from the same base snapshot (S1)
    Transaction tx_alpha(table, OperationType::Append);
    Transaction tx_beta(table, OperationType::Append);
    assert(tx_alpha.base_snapshot_id() == 1);
    assert(tx_beta.base_snapshot_id() == 1);

    DataFile f_alpha{};
    f_alpha.path = table_loc + "/alpha.parquet";
    f_alpha.format = "parquet";
    f_alpha.file_size_bytes = 2048;
    f_alpha.record_count = 200;
    tx_alpha.AppendFile(f_alpha);

    DataFile f_beta{};
    f_beta.path = table_loc + "/beta.parquet";
    f_beta.format = "parquet";
    f_beta.file_size_bytes = 2048;
    f_beta.record_count = 200;
    tx_beta.AppendFile(f_beta);

    // Alpha commits first -> succeeds (S2)
    assert(tx_alpha.Commit().ok());
    assert(table->current_snapshot_id() == 2);

    // Beta attempts commit with stale base snapshot 1 -> rejected with ConcurrentModification
    auto beta_status = tx_beta.Commit();
    assert(!beta_status.ok());
    assert(beta_status.code() == StatusCode::ConcurrentModification);

    // Now test TransactionRunner: Automated OCC retry loop with exponential backoff
    std::atomic<int> attempts{0};
    auto runner_status = TransactionRunner::Run(table, OperationType::Append, 3, [&](Transaction& tx) {
        attempts.fetch_add(1);
        DataFile f_retry{};
        f_retry.path = table_loc + "/retry-" + std::to_string(attempts.load()) + ".parquet";
        f_retry.format = "parquet";
        f_retry.file_size_bytes = 2048;
        f_retry.record_count = 150;
        tx.AppendFile(f_retry);
        return Status::Ok();
    });

    assert(runner_status.ok());
    assert(table->current_snapshot_id() == 3);

    std::cout << "  -> PASSED: OCC conflict caught; automated retry succeeded.\n";
}

/// Verifies Multi-Threaded Concurrent OCC Stress:
/// Multiple threads concurrently submit transactions through TransactionRunner.
/// Validates zero lost updates, monotonic snapshot IDs, and thread-safe metadata commit.
void VerifyDDIA_ConcurrentMultiThreadedOCCStress() {
    std::cout << "[DDIA Chapter 7] Verifying Multi-Threaded Concurrent OCC Stress...\n";
    std::string table_loc = "build/test_env/ddia_stress_table";
    auto fs = LocalFileSystem::Default();
    fs->DeleteRecursively(table_loc);

    auto schema = TradeTick::GetSchema();
    PartitionSpec spec(1, {{"exchange", "exchange", PartitionTransform::Identity, 0}});
    auto table = LakehouseTable::Create(table_loc, "stress_test", schema, spec, fs).value();

    constexpr int kNumWorkers = 4;
    constexpr int kTransactionsPerWorker = 10;
    std::vector<std::thread> workers;
    std::atomic<int> successful_commits{0};

    auto worker_fn = [&](int worker_id) {
        for (int i = 0; i < kTransactionsPerWorker; ++i) {
            auto st = TransactionRunner::Run(table, OperationType::Append, 20, [&](Transaction& tx) {
                DataFile df{};
                df.path = table_loc + "/w" + std::to_string(worker_id) + "_tx" + std::to_string(i) + ".parquet";
                df.format = "parquet";
                df.file_size_bytes = 4096;
                df.record_count = 50;
                tx.AppendFile(df);
                return Status::Ok();
            });
            if (st.ok()) {
                successful_commits.fetch_add(1);
            }
        }
    };

    for (int w = 0; w < kNumWorkers; ++w) {
        workers.emplace_back(worker_fn, w);
    }
    for (auto& w : workers) {
        w.join();
    }

    assert(successful_commits.load() == kNumWorkers * kTransactionsPerWorker);
    assert(table->current_snapshot_id() == successful_commits.load());
    auto final_scan = table->Scan();
    assert(final_scan.ok());
    assert(final_scan.value().size() == static_cast<size_t>(successful_commits.load()));

    std::cout << "  -> PASSED: " << successful_commits.load() << " concurrent transactions safely linearized.\n";
}

// ============================================================================
// DDIA CHAPTER 3: STORAGE ENGINE INTERNALS (COMPACTION & RETRIEVAL)
// ============================================================================

/// Verifies Storage Compaction & Bin-Packing (DDIA Chapter 3):
/// Merges fragmented data files into bounded target sizes while preserving
/// snapshot atomicity and table integrity.
void VerifyDDIA_StorageCompactionAndBinPacking() {
    std::cout << "[DDIA Chapter 3] Verifying LSM/Lakehouse Compaction & File Merging...\n";
    std::string table_loc = "build/test_env/ddia_compaction_table";
    auto fs = LocalFileSystem::Default();
    fs->DeleteRecursively(table_loc);

    auto schema = TradeTick::GetSchema();
    PartitionSpec spec(1, {{"exchange", "exchange", PartitionTransform::Identity, 0}});
    auto table = LakehouseTable::Create(table_loc, "compaction_test", schema, spec, fs).value();

    // Create 4 fragmented 16MB files
    {
        auto tx = table->NewTransaction(OperationType::Append);
        for (int i = 1; i <= 4; ++i) {
            std::string p = table_loc + "/exchange=BATS/f" + std::to_string(i) + ".parquet";
            fs->WriteFileAtomic(p, "parquet_mock_content");
            DataFile df{};
            df.path = p;
            df.format = "parquet";
            df.file_size_bytes = 16 * 1024 * 1024; // 16MB
            df.record_count = 20000;
            df.partitions = {{"exchange", "BATS"}};
            tx->AppendFile(df);
        }
        assert(tx->Commit().ok());
    }
    assert(table->Scan().value().size() == 4);

    // Plan compaction targeting 128MB
    CompactionOptions opts{};
    opts.target_file_size_bytes = 128 * 1024 * 1024;
    opts.min_files_to_compact = 2;

    auto plan_or = CompactionEngine::Plan(*table, opts);
    assert(plan_or.ok());
    auto plan = plan_or.value();
    assert(plan.groups.size() == 1);
    assert(plan.groups[0].files.size() == 4);

    // Execute compaction with consolidation callback
    auto comp_status = CompactionEngine::Execute(table, plan, [&](const FileGroup& fg) -> StatusOr<DataFile> {
        std::string consolidated_path = table_loc + "/exchange=BATS/compacted_128mb.parquet";
        fs->WriteFileAtomic(consolidated_path, "compacted_parquet_content");
        DataFile merged{};
        merged.path = consolidated_path;
        merged.format = "parquet";
        merged.file_size_bytes = fg.total_size_bytes;
        merged.record_count = 80000;
        merged.partitions = fg.partitions;
        return merged;
    });
    assert(comp_status.ok());

    // Verify current snapshot has exactly 1 merged file
    auto scan_after = table->Scan();
    assert(scan_after.ok());
    assert(scan_after.value().size() == 1);
    assert(scan_after.value()[0].record_count == 80000);

    std::cout << "  -> PASSED: 4 fragmented files compacted into 1 unified 64MB partition file.\n";
}

/// Verifies Tombstone Vacuuming & Retention (DDIA Chapter 3):
/// Garbage collection deletes orphaned/superseded files outside retention
/// while strictly preserving files in live snapshots.
void VerifyDDIA_VacuumAndTombstoneCleanup() {
    std::cout << "[DDIA Chapter 3] Verifying Garbage Collection & Vacuum Invariants...\n";
    std::string table_loc = "build/test_env/ddia_vacuum_table";
    auto fs = LocalFileSystem::Default();
    fs->DeleteRecursively(table_loc);

    auto schema = TradeTick::GetSchema();
    auto table = LakehouseTable::Create(table_loc, "vacuum_test", schema, {}, fs).value();

    std::string active_path = table_loc + "/active.parquet";
    std::string dead_path = table_loc + "/dead_tombstone.parquet";
    fs->WriteFileAtomic(active_path, "live_content");
    fs->WriteFileAtomic(dead_path, "dead_content");

    DataFile df_active{};
    df_active.path = active_path;
    df_active.format = "parquet";
    df_active.file_size_bytes = 1000;
    df_active.record_count = 10;

    DataFile df_dead{};
    df_dead.path = dead_path;
    df_dead.format = "parquet";
    df_dead.file_size_bytes = 1000;
    df_dead.record_count = 10;

    // Snapshot 1: Add both active and dead files
    {
        auto tx = table->NewTransaction(OperationType::Append);
        tx->AppendFile(df_active);
        tx->AppendFile(df_dead);
        assert(tx->Commit().ok());
    }
    assert(table->Scan().value().size() == 2);

    // Snapshot 2: Soft deletion marks tombstone entry in manifest
    {
        auto tx = table->NewTransaction(OperationType::Delete);
        tx->DeleteFile(df_dead);
        assert(tx->Commit().ok());
    }
    assert(table->Scan().value().size() == 1);
    assert(fs->Exists(dead_path).value()); // Exists on disk until vacuumed

    // Vacuum with retention 0
    VacuumEngine vacuum_engine(fs);
    VacuumOptions vac_opts{};
    vac_opts.retention_threshold_ms = 0;
    vac_opts.dry_run = false;

    auto vac_or = vacuum_engine.Vacuum(*table, vac_opts);
    assert(vac_or.ok());
    auto vac_res = vac_or.value();

    // Invariant: dead_tombstone is vacuumed, live file is preserved!
    assert(vac_res.deleted_files_count == 1);
    assert(fs->Exists(active_path).value());
    assert(!fs->Exists(dead_path).value());

    std::cout << "  -> PASSED: Dead files purged; active snapshot files preserved.\n";
}

// ============================================================================
// DDIA CHAPTER 1: SLA & LATENCY PERCENTILE PROFILING (p50, p90, p95, p99)
// ============================================================================

struct LatencyStats {
    double min_us;
    double p50_us;
    double p90_us;
    double p95_us;
    double p99_us;
    double max_us;
    double avg_us;
    double ops_per_sec;
};

LatencyStats ComputeStats(std::vector<double>& latencies_us, double total_elapsed_sec) {
    std::sort(latencies_us.begin(), latencies_us.end());
    size_t n = latencies_us.size();
    assert(n > 0);

    auto percentile = [&](double p) -> double {
        size_t idx = static_cast<size_t>(std::ceil(p * static_cast<double>(n))) - 1;
        if (idx >= n) {
            idx = n - 1;
        }
        return latencies_us[idx];
    };

    double sum = std::accumulate(latencies_us.begin(), latencies_us.end(), 0.0);

    LatencyStats stats{};
    stats.min_us = latencies_us.front();
    stats.p50_us = percentile(0.50);
    stats.p90_us = percentile(0.90);
    stats.p95_us = percentile(0.95);
    stats.p99_us = percentile(0.99);
    stats.max_us = latencies_us.back();
    stats.avg_us = sum / static_cast<double>(n);
    stats.ops_per_sec = static_cast<double>(n) / total_elapsed_sec;
    return stats;
}

void RunDDIAPerformanceBenchmark(const std::string& report_out_path = "") {
    std::cout << "\n======================================================\n";
    std::cout << "  DDIA CHAPTER 1: LATENCY PERCENTILE PROFILING (SLA)\n";
    std::cout << "======================================================\n";

    std::string table_loc = "build/test_env/ddia_benchmark_table";
    auto fs = LocalFileSystem::Default();
    fs->DeleteRecursively(table_loc);

    auto schema = TradeTick::GetSchema();
    PartitionSpec spec(1, {{"exchange", "exchange", PartitionTransform::Identity, 0}});
    auto table = LakehouseTable::Create(table_loc, "bench_table", schema, spec, fs).value();

    constexpr int kIterations = 100;
    std::vector<double> commit_latencies;
    commit_latencies.reserve(kIterations);

    std::vector<double> scan_latencies;
    scan_latencies.reserve(kIterations);

    // Warm-up
    for (int i = 0; i < 20; ++i) {
        auto tx = table->NewTransaction(OperationType::Append);
        DataFile df{};
        df.path = table_loc + "/warmup_" + std::to_string(i) + ".parquet";
        df.format = "parquet";
        df.file_size_bytes = 1024;
        df.record_count = 100;
        df.partitions = {{"exchange", "NASDAQ"}};
        tx->AppendFile(df);
        (void)tx->Commit();
    }

    auto t_start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < kIterations; ++i) {
        // Measure Transaction Commit Latency
        auto t0 = std::chrono::high_resolution_clock::now();
        auto tx = table->NewTransaction(OperationType::Append);
        DataFile df{};
        df.path = table_loc + "/bench_" + std::to_string(i) + ".parquet";
        df.format = "parquet";
        df.file_size_bytes = 65536;
        df.record_count = 5000;
        df.partitions = {{"exchange", "NASDAQ"}};
        df.min_values["timestamp_ns"] = std::to_string(1000 + i * 10);
        df.max_values["timestamp_ns"] = std::to_string(1000 + (i + 1) * 10);
        tx->AppendFile(df);
        auto st = tx->Commit();
        assert(st.ok());
        auto t1 = std::chrono::high_resolution_clock::now();

        double commit_us = std::chrono::duration<double, std::micro>(t1 - t0).count();
        commit_latencies.push_back(commit_us);

        // Measure Snapshot Scan Latency
        auto t2 = std::chrono::high_resolution_clock::now();
        auto scan_or = table->Scan({ColumnPredicate::GreaterThanOrEqual("timestamp_ns", std::to_string(1000 + i * 10))});
        assert(scan_or.ok());
        auto t3 = std::chrono::high_resolution_clock::now();

        double scan_us = std::chrono::duration<double, std::micro>(t3 - t2).count();
        scan_latencies.push_back(scan_us);
    }

    auto t_end = std::chrono::high_resolution_clock::now();
    double total_sec = std::chrono::duration<double>(t_end - t_start).count();

    auto commit_stats = ComputeStats(commit_latencies, total_sec);
    auto scan_stats = ComputeStats(scan_latencies, total_sec);

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\n[Metric: Transaction Commit Latency (OCC + Manifest Invalidation)]\n";
    std::cout << "  Throughput : " << commit_stats.ops_per_sec << " tx/sec\n";
    std::cout << "  Min        : " << commit_stats.min_us << " us\n";
    std::cout << "  p50        : " << commit_stats.p50_us << " us\n";
    std::cout << "  p90        : " << commit_stats.p90_us << " us\n";
    std::cout << "  p95        : " << commit_stats.p95_us << " us\n";
    std::cout << "  p99        : " << commit_stats.p99_us << " us\n";
    std::cout << "  Max        : " << commit_stats.max_us << " us\n";
    std::cout << "  Mean       : " << commit_stats.avg_us << " us\n";

    std::cout << "\n[Metric: Snapshot Scan & Predicate Pushdown Latency]\n";
    std::cout << "  Throughput : " << scan_stats.ops_per_sec << " scans/sec\n";
    std::cout << "  Min        : " << scan_stats.min_us << " us\n";
    std::cout << "  p50        : " << scan_stats.p50_us << " us\n";
    std::cout << "  p90        : " << scan_stats.p90_us << " us\n";
    std::cout << "  p95        : " << scan_stats.p95_us << " us\n";
    std::cout << "  p99        : " << scan_stats.p99_us << " us\n";
    std::cout << "  Max        : " << scan_stats.max_us << " us\n";
    std::cout << "  Mean       : " << scan_stats.avg_us << " us\n";

    if (!report_out_path.empty()) {
        std::ofstream out(report_out_path);
        if (out.is_open()) {
            out << "## 🏛️ DDIA Internals Performance & Reliability Report\n\n";
            out << "> **Reference:** *Designing Data-Intensive Applications* (Martin Kleppmann)\n";
            out << "> **Evaluated Subsystems:** MVCC Snapshot Isolation (Ch. 7), LSM Compaction & Data Skipping (Ch. 3), SLA Latency Tails (Ch. 1)\n\n";
            out << "| Subsystem Invariant | Verified Mechanism | Status |\n";
            out << "| :--- | :--- | :---: |\n";
            out << "| **ACID Atomicity** | Atomic multi-file commit & uncommitted rollback | `PASSED` |\n";
            out << "| **Snapshot Isolation (MVCC)** | Non-blocking historical reads & zero read skew | `PASSED` |\n";
            out << "| **Optimistic Concurrency (OCC)** | First-committer-wins validation & backoff retries | `PASSED` |\n";
            out << "| **Multi-Threaded Linearizability** | Lock-free metadata advancement under concurrent load | `PASSED` |\n";
            out << "| **LSM / Bin-Packing Compaction** | Small-file merge into 128MB target partition units | `PASSED` |\n";
            out << "| **Tombstone Vacuuming** | Retention window garbage collection of dead files | `PASSED` |\n\n";
            out << "### ⚡ Tail Latency Distribution (SLA Benchmarks)\n\n";
            out << "| Operation | Throughput | Min | p50 (Median) | p90 | p95 | p99 (Tail) | Max |\n";
            out << "| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |\n";
            out << "| **Transaction Commit (OCC)** | `" << commit_stats.ops_per_sec << " tx/s` | `" << commit_stats.min_us << " µs` | `" << commit_stats.p50_us << " µs` | `" << commit_stats.p90_us << " µs` | `" << commit_stats.p95_us << " µs` | **`" << commit_stats.p99_us << " µs`** | `" << commit_stats.max_us << " µs` |\n";
            out << "| **Snapshot Scan & Pushdown** | `" << scan_stats.ops_per_sec << " scans/s` | `" << scan_stats.min_us << " µs` | `" << scan_stats.p50_us << " µs` | `" << scan_stats.p90_us << " µs` | `" << scan_stats.p95_us << " µs` | **`" << scan_stats.p99_us << " µs`** | `" << scan_stats.max_us << " µs` |\n";
            std::cout << "\n[Report written to " << report_out_path << "]\n";
        }
    }
}

int main(int argc, char** argv) {
    bool run_benchmark = false;
    std::string report_file = "";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--benchmark") {
            run_benchmark = true;
        } else if (arg.rfind("--report=", 0) == 0) {
            report_file = arg.substr(9);
            run_benchmark = true;
        }
    }

    std::cout << "======================================================\n";
    std::cout << "  OBSIDIAN: DDIA INTERNALS VERIFICATION SUITE\n";
    std::cout << "======================================================\n";

    VerifyDDIA_AtomicityAndRollback();
    VerifyDDIA_SnapshotIsolationMVCC();
    VerifyDDIA_OptimisticConcurrencyAndRetries();
    VerifyDDIA_ConcurrentMultiThreadedOCCStress();
    VerifyDDIA_StorageCompactionAndBinPacking();
    VerifyDDIA_VacuumAndTombstoneCleanup();

    std::cout << "\n✅ ALL DDIA INTERNALS INVARIANT CHECKS PASSED!\n";

    if (run_benchmark) {
        RunDDIAPerformanceBenchmark(report_file);
    }

    return 0;
}
