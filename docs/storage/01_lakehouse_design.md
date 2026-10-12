# 01 · Obsidian Quantitative Lakehouse Engine

## Overview
The Obsidian Lakehouse Subsystem provides high-throughput, deterministic storage and retrieval for quantitative finance datasets, including high-frequency market depth (L2/L3), order books, tick-by-tick trades, and multi-resolution OHLCV bars.

It combines the open columnar storage benefits of Apache Parquet with ACID transactional semantics, metadata-driven snapshot isolation, and sub-millisecond predicate pushdown.

---

## Architecture

### 1. Directory & File Organization
A lakehouse table lives in a base directory structured into data partitions and a transaction log directory:

```
/lakehouse/market_data/trades_tick/
├── _delta_log/
│   ├── 00000000000000000000.json     (Initial snapshot metadata)
│   ├── 00000000000000000001.json     (Append transaction)
│   ├── 00000000000000000002.checkpoint.parquet (Periodic state checkpoint)
│   └── manifests/
│       ├── manifest-snap-1-a1b2c3.bin
│       └── manifest-snap-2-d4e5f6.bin
└── exchange=NASDAQ/
    └── date=2026-10-12/
        ├── bucket_0/
        │   └── data-001.parquet
        └── bucket_1/
            └── data-002.parquet
```

### 2. Transaction Log & Atomic Commits
Every state modification (e.g., `append`, `overwrite`, `compaction`, `delete`) produces an immutable `Snapshot`:
- `Snapshot ID`: Monotonically increasing 64-bit integer.
- `Timestamp`: UTC Unix millisecond timestamp.
- `Parent Snapshot ID`: Lineage tracking enabling branching and rollbacks.
- `Manifest List`: Points to all active manifest files comprising the snapshot.

The commit mechanism uses **Optimistic Concurrency Control (OCC)**:
1. Reader obtains current snapshot $S_{read}$.
2. Writer stages new data files and compiles new manifest entries.
3. Writer verifies that no conflicting concurrent writer committed changes to overlapping partitions since $S_{read}$.
4. If no conflict:
   - Writer atomically writes `_delta_log/<snapshot_id>.json.tmp`.
   - Atomically renames `.tmp` to `<snapshot_id>.json` via OS-level atomic filesystem primitive.
5. If conflict:
   - Transaction aborts with `StatusCode::ConcurrentModification`.
   - Retry with exponential backoff and randomized jitter.

---

## 3. Data Skipping & Min/Max Indexes
To eliminate table scan overhead during backtesting or model calibration, each `ManifestEntry` maintains column-level boundary statistics for every tracked Parquet file:

```cpp
struct DataFile {
    std::string path;
    int64_t file_size_bytes;
    int64_t record_count;
    std::vector<PartitionValue> partitions;
    std::unordered_map<std::string, std::string> min_values;
    std::unordered_map<std::string, std::string> max_values;
};
```

When evaluating a scan predicate such as `timestamp_ns >= T_START && timestamp_ns <= T_END`:
- Files where `max_values["timestamp_ns"] < T_START` are instantly pruned.
- Files where `min_values["timestamp_ns"] > T_END` are instantly pruned.
- Unpruned files are scanned in parallel using work-stealing worker threads.

---

## 4. Partition Specifications
Obsidian supports standard partitioning transforms:
- **Identity**: Exact value matching (e.g. `exchange`, `currency`).
- **Bucket**: Hash bucketing into $N$ buckets (`bucket(N, symbol)`) to prevent directory skew on high-liquidity tickers.
- **Year / Month / Day / Hour**: Temporal bucket extraction from nanosecond timestamps.

---

## 5. Maintenance Operations

### Bin-Packing Compaction
Streaming ingestion naturally generates small Parquet files (e.g. every 5 seconds). Querying millions of small files degrades performance due to metadata overhead.
- The **Compaction Engine** identifies small files within identical partition keys whose sizes are below `min_file_size_threshold` (e.g. 32 MB).
- It reads and merges these files into optimal 128 MB – 256 MB Parquet row groups.
- It commits an atomic transaction: marks old small files as `DELETED` in the manifest and registers the new consolidated file as `ADDED`.

### Vacuum & Snapshot Expiration
- Snapshots older than the retention threshold (default: 7 days) are expired.
- Orphan files and deleted data files unreferenced by any active snapshot past the retention grace period are securely purged by the `Vacuum` service.
