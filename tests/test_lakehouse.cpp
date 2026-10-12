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

#include <iostream>
#include <cassert>
#include <filesystem>

using namespace obsidian::storage;
using namespace obsidian::storage::lakehouse;
using namespace obsidian::storage::schema;
using namespace obsidian::storage::arrow;

namespace fs = std::filesystem;

void TestStatusAndFileSystem() {
    std::cout << "[TEST] Running TestStatusAndFileSystem...\n";
    auto fs = LocalFileSystem::Default();
    std::string test_dir = "build/test_env/fs_test";
    std::string test_file = test_dir + "/sample.txt";

    auto status = fs->CreateDirectories(test_dir);
    assert(status.ok());

    std::string content = "Obsidian high-performance quantitative lakehouse engine";
    status = fs->WriteFileAtomic(test_file, content);
    assert(status.ok());

    auto exists_or = fs->Exists(test_file);
    assert(exists_or.ok() && exists_or.value());

    auto text_or = fs->ReadAllText(test_file);
    assert(text_or.ok() && text_or.value() == content);

    auto size_or = fs->GetFileSize(test_file);
    assert(size_or.ok() && size_or.value() == static_cast<int64_t>(content.size()));

    status = fs->DeleteFile(test_file);
    assert(status.ok());

    exists_or = fs->Exists(test_file);
    assert(exists_or.ok() && !exists_or.value());
    std::cout << "  -> PASSED\n";
}

void TestMarketDataSchemaAndColumnarBatch() {
    std::cout << "[TEST] Running TestMarketDataSchemaAndColumnarBatch...\n";
    auto quote_schema = QuoteL1::GetSchema();
    assert(quote_schema.num_fields() == 7);
    assert(quote_schema.GetFieldIndex("bid_price") == 2);
    assert(quote_schema.GetFieldIndex("nonexistent") == -1);

    // Create aligned columnar vector
    auto price_col = std::make_shared<ColumnVector<double>>(DataType::Double, std::initializer_list<double>{100.5, 101.0, 101.5, 102.0});

    // Verify 64-byte alignment for SIMD
    uintptr_t ptr_val = reinterpret_cast<uintptr_t>(price_col->raw_data());
    assert(ptr_val % 64 == 0);
    assert(price_col->size() == 4);
    assert((*price_col)[1] == 101.0);

    // Verify zero-copy slicing
    auto sliced_col = price_col->Slice(1, 2);
    assert(sliced_col->size() == 2);
    auto typed_slice = std::dynamic_pointer_cast<ColumnVector<double>>(sliced_col);
    assert(typed_slice != nullptr);
    assert((*typed_slice)[0] == 101.0);
    assert((*typed_slice)[1] == 101.5);

    std::cout << "  -> PASSED\n";
}

void TestPartitionSpecAndDataSkipping() {
    std::cout << "[TEST] Running TestPartitionSpecAndDataSkipping...\n";
    PartitionSpec spec(1, {
        {"exchange", "exchange", PartitionTransform::Identity, 0},
        {"symbol", "bucket", PartitionTransform::Bucket, 4}
    });

    std::unordered_map<std::string, std::string> row1 = {
        {"exchange", "NASDAQ"},
        {"symbol", "AAPL"}
    };

    auto parts = spec.Evaluate(row1);
    assert(parts.size() == 2);
    assert(parts[0].name == "exchange" && parts[0].value == "NASDAQ");
    assert(parts[1].name == "bucket"); // hashed bucket

    std::string path = spec.FormatPartitionPath(parts);
    assert(path.find("exchange=NASDAQ/bucket=") != std::string::npos);

    // Data skipping evaluation
    DataFile file{};
    file.path = "data-001.parquet";
    file.min_values["price"] = "100";
    file.max_values["price"] = "150";

    ColumnPredicate in_range = ColumnPredicate::InRange("price", "120", "130");
    assert(!in_range.CanSkipFile(file)); // overlaps [100, 150]

    ColumnPredicate out_of_range = ColumnPredicate::InRange("price", "200", "250");
    assert(out_of_range.CanSkipFile(file)); // completely outside

    ColumnPredicate ge_filter = ColumnPredicate::GreaterThanOrEqual("price", "160");
    assert(ge_filter.CanSkipFile(file)); // max (150) is less than 160 -> skipped!

    std::cout << "  -> PASSED\n";
}

void TestLakehouseTableTransactionsAndOCC() {
    std::cout << "[TEST] Running TestLakehouseTableTransactionsAndOCC...\n";
    std::string table_loc = "build/test_env/lakehouse_test_table";
    auto fs = LocalFileSystem::Default();
    fs->DeleteRecursively(table_loc);

    auto schema = TradeTick::GetSchema();
    PartitionSpec spec(1, {{"exchange", "exchange", PartitionTransform::Identity, 0}});

    auto table_or = LakehouseTable::Create(table_loc, "trades_tick", schema, spec, fs);
    assert(table_or.ok());
    auto table = table_or.value();
    assert(table->current_snapshot_id() == 0);

    // Transaction 1: Append two data files
    {
        auto tx = table->NewTransaction(OperationType::Append);
        DataFile df1{};
        df1.path = table_loc + "/exchange=NASDAQ/part-1.parquet";
        df1.format = "parquet";
        df1.file_size_bytes = 1024 * 1024;
        df1.record_count = 50000;
        df1.partitions = {{"exchange", "NASDAQ"}};
        df1.min_values["timestamp_ns"] = "1000";
        df1.max_values["timestamp_ns"] = "2000";
        tx->AppendFile(df1);

        DataFile df2{};
        df2.path = table_loc + "/exchange=NASDAQ/part-2.parquet";
        df2.format = "parquet";
        df2.file_size_bytes = 1024 * 1024;
        df2.record_count = 60000;
        df2.partitions = {{"exchange", "NASDAQ"}};
        df2.min_values["timestamp_ns"] = "2001";
        df2.max_values["timestamp_ns"] = "3000";
        tx->AppendFile(df2);

        auto commit_status = tx->Commit();
        assert(commit_status.ok());
    }

    assert(table->current_snapshot_id() == 1);
    auto scan_all = table->Scan();
    assert(scan_all.ok() && scan_all.value().size() == 2);

    // Test data skipping during scan
    ColumnPredicate skip_pred = ColumnPredicate::GreaterThanOrEqual("timestamp_ns", "2500");
    auto scan_filtered = table->Scan({skip_pred});
    assert(scan_filtered.ok() && scan_filtered.value().size() == 1);
    assert(scan_filtered.value()[0].path.find("part-2.parquet") != std::string::npos);

    // Test Optimistic Concurrency Control (OCC) Collision
    {
        Transaction tx_a(table, OperationType::Append);
        Transaction tx_b(table, OperationType::Append);

        assert(tx_a.base_snapshot_id() == 1);
        assert(tx_b.base_snapshot_id() == 1);

        DataFile df_a{};
        df_a.path = table_loc + "/exchange=NASDAQ/part-3.parquet";
        df_a.format = "parquet";
        df_a.file_size_bytes = 500000;
        df_a.record_count = 10000;
        df_a.partitions = {{"exchange", "NASDAQ"}};
        tx_a.AppendFile(df_a);

        // Tx A commits first, advancing snapshot from 1 to 2
        auto status_a = tx_a.Commit();
        assert(status_a.ok());
        assert(table->current_snapshot_id() == 2);

        // Tx B attempts commit based on stale snapshot 1 -> must fail with ConcurrentModification!
        DataFile df_b{};
        df_b.path = table_loc + "/exchange=NASDAQ/part-4.parquet";
        df_b.format = "parquet";
        df_b.file_size_bytes = 500000;
        df_b.record_count = 10000;
        df_b.partitions = {{"exchange", "NASDAQ"}};
        tx_b.AppendFile(df_b);

        auto status_b = tx_b.Commit();
        assert(!status_b.ok());
        assert(status_b.code() == StatusCode::ConcurrentModification);
    }

    // Time Travel: Scan as of snapshot 1
    auto scan_snap1 = table->Scan({}, {}, 1);
    assert(scan_snap1.ok() && scan_snap1.value().size() == 2);

    // Scan as of snapshot 2
    auto scan_snap2 = table->Scan({}, {}, 2);
    assert(scan_snap2.ok() && scan_snap2.value().size() == 3);

    std::cout << "  -> PASSED\n";
}

void TestCompactionAndVacuum() {
    std::cout << "[TEST] Running TestCompactionAndVacuum...\n";
    std::string table_loc = "build/test_env/lakehouse_compaction_table";
    auto fs = LocalFileSystem::Default();
    fs->DeleteRecursively(table_loc);

    auto schema = TradeTick::GetSchema();
    PartitionSpec spec(1, {{"exchange", "exchange", PartitionTransform::Identity, 0}});

    auto table_or = LakehouseTable::Create(table_loc, "trades_compaction", schema, spec, fs);
    assert(table_or.ok());
    auto table = table_or.value();

    // Create 3 small files
    auto tx = table->NewTransaction(OperationType::Append);
    for (int i = 1; i <= 3; ++i) {
        std::string fpath = table_loc + "/exchange=CME/small-" + std::to_string(i) + ".parquet";
        fs->WriteFileAtomic(fpath, "dummy parquet content");
        DataFile df{};
        df.path = fpath;
        df.format = "parquet";
        df.file_size_bytes = 10 * 1024 * 1024; // 10MB
        df.record_count = 1000;
        df.partitions = {{"exchange", "CME"}};
        tx->AppendFile(df);
    }
    assert(tx->Commit().ok());
    assert(table->Scan().value().size() == 3);

    // Plan compaction targeting 128MB
    CompactionOptions comp_opts{};
    comp_opts.target_file_size_bytes = 128 * 1024 * 1024;
    comp_opts.min_files_to_compact = 2;

    auto plan_or = CompactionEngine::Plan(*table, comp_opts);
    assert(plan_or.ok());
    assert(plan_or.value().groups.size() == 1);
    assert(plan_or.value().groups[0].files.size() == 3);

    // Execute compaction
    auto comp_status = CompactionEngine::Execute(table, plan_or.value(), [&](const FileGroup& fg) -> StatusOr<DataFile> {
        std::string consolidated_path = table_loc + "/exchange=CME/consolidated.parquet";
        fs->WriteFileAtomic(consolidated_path, "merged parquet data");
        DataFile merged{};
        merged.path = consolidated_path;
        merged.format = "parquet";
        merged.file_size_bytes = fg.total_size_bytes;
        merged.record_count = 3000;
        merged.partitions = fg.partitions;
        return merged;
    });
    assert(comp_status.ok());

    // Current scan should only see 1 consolidated file!
    auto scan_compacted = table->Scan();
    assert(scan_compacted.ok());
    assert(scan_compacted.value().size() == 1);
    assert(scan_compacted.value()[0].path.find("consolidated.parquet") != std::string::npos);

    // Run Vacuum
    VacuumEngine vacuum_engine(fs);
    VacuumOptions vac_opts{};
    vac_opts.retention_threshold_ms = 0; // immediate retention for test
    vac_opts.dry_run = false;

    auto vac_or = vacuum_engine.Vacuum(*table, vac_opts);
    assert(vac_or.ok());
    assert(vac_or.value().deleted_files_count == 3); // 3 small files cleaned up

    std::cout << "  -> PASSED\n";
}

void TestLakehouseCatalog() {
    std::cout << "[TEST] Running TestLakehouseCatalog...\n";
    std::string warehouse_root = "build/test_env/warehouse";
    auto fs = LocalFileSystem::Default();
    fs->DeleteRecursively(warehouse_root);

    LakehouseCatalog catalog(warehouse_root, fs);

    auto ns_status = catalog.CreateNamespace("equities");
    assert(ns_status.ok());

    auto namespaces_or = catalog.ListNamespaces();
    assert(namespaces_or.ok());
    assert(std::find(namespaces_or.value().begin(), namespaces_or.value().end(), "equities") != namespaces_or.value().end());

    auto table_or = catalog.CreateTable("equities", "quotes", QuoteL1::GetSchema());
    assert(table_or.ok());

    auto exists_or = catalog.TableExists("equities", "quotes");
    assert(exists_or.ok() && exists_or.value());

    auto tables_or = catalog.ListTables("equities");
    assert(tables_or.ok() && tables_or.value().size() == 1);
    assert(tables_or.value()[0] == "quotes");

    auto loaded_or = catalog.LoadTable("equities", "quotes");
    assert(loaded_or.ok() && loaded_or.value()->table_name() == "quotes");

    std::cout << "  -> PASSED\n";
}

int main() {
    std::cout << "=====================================================\n";
    std::cout << "   Obsidian Lakehouse & Backend Verification Suite   \n";
    std::cout << "=====================================================\n";

    try {
        TestStatusAndFileSystem();
        TestMarketDataSchemaAndColumnarBatch();
        TestPartitionSpecAndDataSkipping();
        TestLakehouseTableTransactionsAndOCC();
        TestCompactionAndVacuum();
        TestLakehouseCatalog();

        std::cout << "\n>>> ALL OBSIDIAN LAKEHOUSE TESTS PASSED! <<<\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FATAL ERROR during test execution: " << e.what() << "\n";
        return 1;
    }
}
