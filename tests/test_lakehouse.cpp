#include "storage/common/status.hpp"
#include "storage/common/file_system.hpp"
#include "storage/schema/market_data_schema.hpp"
#include "storage/schema/schema_validator.hpp"
#include "storage/arrow/columnar_batch.hpp"
#include "storage/parquet/columnar_file_writer.hpp"
#include "storage/parquet/columnar_file_reader.hpp"
#include "storage/duckdb/duckdb_bridge.hpp"
#include "storage/lakehouse/partition_spec.hpp"
#include "storage/lakehouse/manifest_manager.hpp"
#include "storage/lakehouse/lakehouse_table.hpp"
#include "storage/lakehouse/transaction.hpp"
#include "storage/lakehouse/integrity_guard.hpp"
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
    (void)ptr_val;
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

void TestColumnarFileWriterAndReader() {
    std::cout << "[TEST] Running TestColumnarFileWriterAndReader...\n";
    std::string test_file = "build/test_env/columnar_test.obsd";
    auto fs = LocalFileSystem::Default();
    fs->DeleteFile(test_file);

    // 1. Prepare ColumnarBatch with 5 rows of QuoteL1 data
    auto quote_schema = QuoteL1::GetSchema();
    size_t count = 5;

    std::vector<int64_t> ts_data = {1000000000LL, 1000001000LL, 1000002000LL, 1000003000LL, 1000004000LL};
    std::vector<int32_t> sym_data = {101, 101, 101, 101, 101};
    std::vector<double> bid_px = {150.25, 150.30, 150.20, 150.35, 150.40};
    std::vector<double> bid_sz = {100.0, 200.0, 150.0, 300.0, 250.0};
    std::vector<double> ask_px = {150.30, 150.35, 150.25, 150.40, 150.45};
    std::vector<double> ask_sz = {120.0, 180.0, 210.0, 190.0, 310.0};
    std::vector<int64_t> seq_data = {1, 2, 3, 4, 5};

    std::vector<std::shared_ptr<IColumnVector>> cols;
    cols.push_back(std::make_shared<ColumnVector<int64_t>>(DataType::TimestampNs, ts_data.data(), count));
    cols.push_back(std::make_shared<ColumnVector<int32_t>>(DataType::Int32, sym_data.data(), count));
    cols.push_back(std::make_shared<ColumnVector<double>>(DataType::Double, bid_px.data(), count));
    cols.push_back(std::make_shared<ColumnVector<double>>(DataType::Double, bid_sz.data(), count));
    cols.push_back(std::make_shared<ColumnVector<double>>(DataType::Double, ask_px.data(), count));
    cols.push_back(std::make_shared<ColumnVector<double>>(DataType::Double, ask_sz.data(), count));
    cols.push_back(std::make_shared<ColumnVector<int64_t>>(DataType::Int64, seq_data.data(), count));

    ColumnarBatch batch(quote_schema, cols, count);
    assert(batch.num_rows() == 5);
    assert(batch.num_columns() == 7);

    // 2. Write Columnar Batch to disk
    obsidian::storage::parquet::ColumnarFileWriter writer(fs);
    auto write_or = writer.Write(test_file, batch, {{"exchange", "NASDAQ"}, {"date", "2026-11-08"}});
    assert(write_or.ok());

    auto df = write_or.value();
    assert(df.path == test_file);
    assert(df.record_count == 5);
    assert(df.file_size_bytes > 0);
    assert(df.partitions.size() == 2);
    assert(df.min_values.find("bid_price") != df.min_values.end());
    assert(df.min_values["bid_price"].find("150.2") != std::string::npos);
    assert(df.max_values["bid_price"].find("150.4") != std::string::npos);

    // 3. Open and Read back with ColumnarFileReader
    auto reader_or = obsidian::storage::parquet::ColumnarFileReader::Open(test_file, fs);
    assert(reader_or.ok());
    auto reader = std::move(reader_or.value());

    assert(reader->num_rows() == 5);
    assert(reader->num_columns() == 7);
    assert(reader->min_values().at("bid_price") == df.min_values["bid_price"]);
    assert(reader->max_values().at("bid_price") == df.max_values["bid_price"]);

    // Full Read
    auto read_all_or = reader->ReadAll();
    assert(read_all_or.ok());
    auto loaded_batch = read_all_or.value();
    assert(loaded_batch->num_rows() == 5);
    assert(loaded_batch->num_columns() == 7);

    auto loaded_bid_px = loaded_batch->GetTypedColumn<double>(2);
    assert((*loaded_bid_px)[0] == 150.25);
    assert((*loaded_bid_px)[4] == 150.40);

    // Verify 64-byte alignment
    uintptr_t raw_ptr = reinterpret_cast<uintptr_t>(loaded_bid_px->raw_data());
    assert(raw_ptr % 64 == 0);
    (void)raw_ptr;

    // Projection Pushdown: Read only "timestamp_ns" and "bid_price"
    auto proj_or = reader->ReadColumns({"timestamp_ns", "bid_price"});
    assert(proj_or.ok());
    auto proj_batch = proj_or.value();
    assert(proj_batch->num_columns() == 2);
    assert(proj_batch->num_rows() == 5);
    assert(proj_batch->schema().fields()[0].name() == "timestamp_ns");
    assert(proj_batch->schema().fields()[1].name() == "bid_price");

    auto proj_px = proj_batch->GetTypedColumn<double>(1);
    assert((*proj_px)[0] == 150.25);
    assert((*proj_px)[3] == 150.35);

    // 4. Test Bit-Rot & Data Corruption Error Detection via CRC32C
    {
        std::string corrupted_file = "build/test_env/corrupted_test.obsd";
        auto copy_st = fs->CopyFile(test_file, corrupted_file);
        assert(copy_st.ok());

        auto raw_text_or = fs->ReadAllText(corrupted_file);
        assert(raw_text_or.ok());
        std::string raw_bytes = raw_text_or.value();

        // Corrupt a byte in the column payload area
        raw_bytes[raw_bytes.size() - 50] ^= 0xFF;
        auto write_corrupted = fs->WriteFileAtomic(corrupted_file, raw_bytes);
        assert(write_corrupted.ok());

        auto corrupt_reader_or = obsidian::storage::parquet::ColumnarFileReader::Open(corrupted_file, fs);
        assert(corrupt_reader_or.ok());
        auto corrupt_reader = std::move(corrupt_reader_or.value());

        auto read_corrupt_st = corrupt_reader->ReadAll();
        assert(!read_corrupt_st.ok());
        assert(read_corrupt_st.status().code() == StatusCode::DataLoss);
        assert(read_corrupt_st.status().message().find("CRC32C mismatch") != std::string::npos);
    }

    std::cout << "  -> PASSED\n";
}

void TestSchemaValidatorAndDriftDetection() {
    std::cout << "[TEST] Running TestSchemaValidatorAndDriftDetection...\n";
    auto base_schema = QuoteL1::GetSchema();

    // 1. Identical Schema passes
    auto identical_res = SchemaValidator::ValidateCompatibility(base_schema, base_schema);
    assert(identical_res.is_valid);
    assert(!identical_res.requires_evolution);
    assert(identical_res.drift_errors.empty());

    // 2. Incompatible Type Drift: bid_price changed from Double to Boolean
    Schema drifted_schema({
        {"timestamp_ns", DataType::TimestampNs, false},
        {"symbol_id", DataType::Int32, false},
        {"bid_price", DataType::Boolean, false}, // ERROR: Incompatible!
        {"bid_size", DataType::Double, false},
        {"ask_price", DataType::Double, false},
        {"ask_size", DataType::Double, false},
        {"sequence_num", DataType::Int64, false}
    });
    auto drift_res = SchemaValidator::ValidateCompatibility(base_schema, drifted_schema);
    assert(!drift_res.is_valid);
    assert(!drift_res.drift_errors.empty());
    assert(drift_res.drift_errors[0].find("Incompatible type drift in column 'bid_price'") != std::string::npos);

    // 3. Missing Required Non-Nullable Column
    Schema missing_col_schema({
        {"symbol_id", DataType::Int32, false},
        {"bid_price", DataType::Double, false}
    });
    auto missing_res = SchemaValidator::ValidateCompatibility(base_schema, missing_col_schema);
    assert(!missing_res.is_valid);
    assert(!missing_res.drift_errors.empty());
    assert(missing_res.drift_errors[0].find("Missing required non-nullable column") != std::string::npos);

    // 4. Safe Type Widening: symbol_id from Int32 -> Int64
    Schema widened_schema({
        {"timestamp_ns", DataType::TimestampNs, false},
        {"symbol_id", DataType::Int64, false}, // WIDENED
        {"bid_price", DataType::Double, false},
        {"bid_size", DataType::Double, false},
        {"ask_price", DataType::Double, false},
        {"ask_size", DataType::Double, false},
        {"sequence_num", DataType::Int64, false}
    });
    auto widen_res = SchemaValidator::ValidateCompatibility(base_schema, widened_schema);
    assert(widen_res.is_valid);
    assert(widen_res.requires_evolution);
    assert(widen_res.evolved_schema.fields()[1].type() == DataType::Int64);

    // 5. Additive Evolution: new optional field added
    Schema additive_schema = base_schema;
    std::vector<Field> add_fields = base_schema.fields();
    add_fields.emplace_back("exchange_fee", DataType::Double, true);
    Schema evolved_input(add_fields);

    auto add_res = SchemaValidator::ValidateCompatibility(base_schema, evolved_input, true);
    assert(add_res.is_valid);
    assert(add_res.requires_evolution);
    assert(add_res.evolved_schema.num_fields() == 8);
    assert(add_res.evolved_schema.HasField("exchange_fee"));

    // Additive evolution disallowed flag
    auto strict_res = SchemaValidator::ValidateCompatibility(base_schema, evolved_input, false);
    assert(!strict_res.is_valid);
    assert(!strict_res.drift_errors.empty());

    std::cout << "  -> PASSED\n";
}

void TestLakehouseIntegrityGuardAndOrphanDetection() {
    std::cout << "[TEST] Running TestLakehouseIntegrityGuardAndOrphanDetection...\n";
    std::string table_loc = "build/test_env/integrity_test_table";
    auto fs = LocalFileSystem::Default();
    fs->DeleteRecursively(table_loc);

    auto schema = TradeTick::GetSchema();
    PartitionSpec spec(1, {{"exchange", "exchange", PartitionTransform::Identity, 0}});

    auto table_or = LakehouseTable::Create(table_loc, "trades_audit", schema, spec, fs);
    assert(table_or.ok());
    auto table = table_or.value();

    std::string valid_file = table_loc + "/exchange=NASDAQ/part-1.parquet";
    fs->WriteFileAtomic(valid_file, "valid data payload");

    // Commit 1 valid file
    {
        auto tx = table->NewTransaction(OperationType::Append);
        DataFile df{};
        df.path = valid_file;
        df.format = "parquet";
        df.file_size_bytes = 18;
        df.record_count = 100;
        df.partitions = {{"exchange", "NASDAQ"}};
        df.min_values["price"] = "100.5";
        df.max_values["price"] = "105.0";
        tx->AppendFile(df);
        assert(tx->Commit().ok());
    }

    LakehouseIntegrityGuard guard(fs);

    // 1. Audit clean table: must be healthy
    auto audit_clean = guard.Audit(*table);
    assert(audit_clean.ok());
    assert(audit_clean.value().is_healthy);
    assert(audit_clean.value().active_data_files == 1);
    assert(audit_clean.value().missing_data_files == 0);
    assert(audit_clean.value().orphan_data_files == 0);
    assert(audit_clean.value().boundary_anomalies_count == 0);

    // 2. Inverted Boundary Anomaly: Commit a file with min > max
    std::string anom_file = table_loc + "/exchange=NASDAQ/part-2.parquet";
    fs->WriteFileAtomic(anom_file, "anomaly file payload");
    {
        auto tx = table->NewTransaction(OperationType::Append);
        DataFile df_anom{};
        df_anom.path = anom_file;
        df_anom.format = "parquet";
        df_anom.file_size_bytes = 20;
        df_anom.record_count = 50;
        df_anom.partitions = {{"exchange", "NASDAQ"}};
        df_anom.min_values["price"] = "200.0"; // MIN > MAX ANOMALY!
        df_anom.max_values["price"] = "150.0";
        tx->AppendFile(df_anom);
        assert(tx->Commit().ok());
    }

    auto audit_anom = guard.Audit(*table);
    assert(audit_anom.ok());
    assert(!audit_anom.value().is_healthy);
    assert(audit_anom.value().boundary_anomalies_count >= 1);

    // 3. Orphan File Leak Detection: create untracked staging file
    std::string orphan_file = table_loc + "/exchange=NASDAQ/orphan-staging.parquet";
    fs->WriteFileAtomic(orphan_file, "leaked staging data from crashed transaction");

    auto audit_orphan = guard.Audit(*table);
    assert(audit_orphan.ok());
    assert(!audit_orphan.value().is_healthy);
    assert(audit_orphan.value().orphan_data_files == 1);
    assert(!audit_orphan.value().orphan_file_paths.empty());

    // 4. Purge orphan files
    auto clean_st = guard.CleanOrphanFiles(audit_orphan.value().orphan_file_paths);
    assert(clean_st.ok());

    auto audit_post_clean = guard.Audit(*table);
    assert(audit_post_clean.ok());
    assert(audit_post_clean.value().orphan_data_files == 0);

    std::cout << "  -> PASSED\n";
}

void TestDuckDBArrowBridgeAndAnalyticalEngine() {
    std::cout << "[TEST] Running TestDuckDBArrowBridgeAndAnalyticalEngine...\n";
    auto schema = QuoteL1::GetSchema();

    // 1. Arrow C Data Interface Schema Export
    obsidian::storage::duckdb_engine::ArrowSchema arrow_schema{};
    auto export_schema_st = obsidian::storage::duckdb_engine::DuckDBArrowBridge::ExportSchema(schema, &arrow_schema);
    assert(export_schema_st.ok());
    assert(std::string(arrow_schema.format) == "+s");
    assert(arrow_schema.n_children == 7);
    assert(std::string(arrow_schema.children[0]->name) == "timestamp_ns");
    assert(std::string(arrow_schema.children[2]->name) == "bid_price");
    assert(std::string(arrow_schema.children[2]->format) == "g"); // Double

    // 2. Prepare Batch and Export ArrowArray (Zero-Copy)
    size_t count = 4;
    std::vector<int64_t> ts = {1000, 2000, 3000, 4000};
    std::vector<int32_t> sym = {1, 1, 1, 1};
    std::vector<double> bid_px = {100.5, 105.0, 98.0, 110.5};
    std::vector<double> bid_sz = {10.0, 20.0, 15.0, 30.0};
    std::vector<double> ask_px = {101.0, 105.5, 98.5, 111.0};
    std::vector<double> ask_sz = {12.0, 18.0, 21.0, 19.0};
    std::vector<int64_t> seq = {1, 2, 3, 4};

    std::vector<std::shared_ptr<IColumnVector>> cols;
    cols.push_back(std::make_shared<ColumnVector<int64_t>>(DataType::TimestampNs, ts.data(), count));
    cols.push_back(std::make_shared<ColumnVector<int32_t>>(DataType::Int32, sym.data(), count));
    cols.push_back(std::make_shared<ColumnVector<double>>(DataType::Double, bid_px.data(), count));
    cols.push_back(std::make_shared<ColumnVector<double>>(DataType::Double, bid_sz.data(), count));
    cols.push_back(std::make_shared<ColumnVector<double>>(DataType::Double, ask_px.data(), count));
    cols.push_back(std::make_shared<ColumnVector<double>>(DataType::Double, ask_sz.data(), count));
    cols.push_back(std::make_shared<ColumnVector<int64_t>>(DataType::Int64, seq.data(), count));

    auto batch = std::make_shared<ColumnarBatch>(schema, cols, count);

    obsidian::storage::duckdb_engine::ArrowArray arrow_array{};
    auto export_array_st = obsidian::storage::duckdb_engine::DuckDBArrowBridge::ExportBatch(*batch, &arrow_array);
    assert(export_array_st.ok());
    assert(arrow_array.length == 4);
    assert(arrow_array.n_children == 7);

    // Verify zero-copy pointer equivalence
    assert(arrow_array.children[2]->buffers[1] == cols[2]->raw_data());

    // 3. Analytical Query Engine Filter
    obsidian::storage::duckdb_engine::DuckDBAnalyticalEngine engine;
    auto filter_or = engine.ExecuteFilter(batch, "bid_price", [](double px) {
        return px >= 100.0;
    });
    assert(filter_or.ok());
    auto res = filter_or.value();
    assert(res.rows_scanned == 4);
    assert(res.rows_matched == 3); // 100.5, 105.0, 110.5
    assert(res.result_batch->num_rows() == 3);

    auto filtered_px = res.result_batch->GetTypedColumn<double>(2);
    assert((*filtered_px)[0] == 100.5);
    assert((*filtered_px)[1] == 105.0);
    assert((*filtered_px)[2] == 110.5);

    // 4. Clean Release
    obsidian::storage::duckdb_engine::DuckDBArrowBridge::ReleaseSchema(&arrow_schema);
    obsidian::storage::duckdb_engine::DuckDBArrowBridge::ReleaseArray(&arrow_array);
    assert(arrow_schema.release == nullptr);
    assert(arrow_array.release == nullptr);

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
        TestColumnarFileWriterAndReader();
        TestSchemaValidatorAndDriftDetection();
        TestLakehouseIntegrityGuardAndOrphanDetection();
        TestDuckDBArrowBridgeAndAnalyticalEngine();

        std::cout << "\n>>> ALL OBSIDIAN LAKEHOUSE TESTS PASSED! <<<\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FATAL ERROR during test execution: " << e.what() << "\n";
        return 1;
    }
}
