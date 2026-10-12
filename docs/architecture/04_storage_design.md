# 04 · Storage Design & Lakehouse Architecture

> Schema, indexing strategy, lakehouse storage layers, and data retention policies for high-performance quantitative finance.

## 1. Storage Architecture Overview

Obsidian implements a hybrid **Medallion Lakehouse Architecture** tailored for ultra-low-latency financial telemetry, order book reconstruction, and distributed quantitative analytics.

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                       Market Data & Execution Streams                       │
│                   (PCAP, ITCH/OUCH, FIX, WebSocket Feeds)                    │
└──────────────────────────────────────┬──────────────────────────────────────┘
                                       │
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                          Bronze Tier (Raw Ingest)                           │
│  - Storage: Append-only memory-mapped ring buffers & raw blob storage       │
│  - Format: Raw binary frames, PCAP captures, uncompressed tick journals     │
│  - Latency: Sub-microsecond write path, zero allocation                     │
└──────────────────────────────────────┬──────────────────────────────────────┘
                                       │ Continuous Ingestion & Normalisation
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                         Silver Tier (Lakehouse Core)                        │
│  - Storage: Parquet columnar data with Delta/Iceberg manifest snapshots     │
│  - Format: Normalised L1/L2/L3 order books, tick-by-tick trades             │
│  - Partitioning: `exchange={id}/symbol={sym}/date={YYYY-MM-DD}`              │
│  - Guarantees: ACID transactions via Optimistic Concurrency Control (OCC)   │
└──────────────────────────────────────┬──────────────────────────────────────┘
                                       │ Vectorised Rollup & Feature Pipeline
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                       Gold Tier (Feature & Analytics)                       │
│  - Storage: Parquet / Arrow IPC / ClickHouse & DuckDB analytical tables     │
│  - Format: Resampled OHLCV bars (100ms, 1s, 1m), OFI, microstructure alpha  │
│  - Compute: Zero-copy Arrow arrays dispatched to SIMD/AVX-512 kernels       │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Storage Layer Inventory

| Layer | Technology | Primary Role | Access Pattern | Retention |
|-------|------------|--------------|----------------|-----------|
| **L0 Memory Ring** | Lock-Free Ring Buffer / Disruptor | Hot path quotes & telemetry | Lock-free sequential write, multi-reader | Volatile (In-memory) |
| **L1 Page Cache** | `mmap_storage` / Shared Memory | Microsecond order book lookups | Random read / sequential append | Intraday (24 hours) |
| **Lakehouse Silver** | Parquet + ACID Manifest Log | Historical tick & depth source of truth | Partition-pruned analytical scans | 90 days hot flash, 7 years cold |
| **Lakehouse Gold** | Columnar Arrow / DuckDB / ClickHouse | Alpha research, backtesting, factor matrices | Columnar vectorised scans | Indefinite / Versioned |
| **Metadata Catalog**| Versioned JSON & Binary Manifests | ACID snapshot tracking & transaction log | O(1) atomic commit & time travel | Snapshot history retention |

---

## 3. Lakehouse Schema Definitions

### Silver Tier: Market Data Schemas

#### L1 Quote (Top-of-Book BBO)
```
Table: quotes_l1
  timestamp_ns   INT64 (Delta-encoded) PK
  symbol_id      INT32 PK
  bid_price      DOUBLE (Gorilla-compressed)
  bid_size       DOUBLE
  ask_price      DOUBLE (Gorilla-compressed)
  ask_size       DOUBLE
  sequence_num   INT64
```

#### L2 Depth Snapshot (Aggregated Order Book)
```
Table: order_book_l2
  timestamp_ns   INT64 PK
  symbol_id      INT32 PK
  depth_level    INT16
  side           INT8 (0 = Bid, 1 = Ask)
  price          DOUBLE
  quantity       DOUBLE
  order_count    INT32
```

#### Trade Tick
```
Table: trades_tick
  timestamp_ns   INT64 PK
  symbol_id      INT32 PK
  trade_id       INT64
  price          DOUBLE
  quantity       DOUBLE
  aggressor_side INT8 (0 = Unknown, 1 = Buy, 2 = Sell)
```

### Gold Tier: Resampled Bars (OHLCV)
```
Table: bars_ohlcv
  bar_timestamp_ns INT64 PK
  symbol_id        INT32 PK
  resolution_ms    INT32 (e.g. 100, 1000, 60000)
  open             DOUBLE
  high             DOUBLE
  low              DOUBLE
  close            DOUBLE
  volume           DOUBLE
  vwap             DOUBLE
  tick_count       INT64
  buy_volume       DOUBLE
  sell_volume      DOUBLE
```

---

## 4. Partitioning & Data Skipping Strategy

### Partition Specifications
Silver and Gold tables are partitioned hierarchically to maximize partition pruning efficiency:
1. `exchange`: Categorical partition (e.g. `NASDAQ`, `CME`, `BINANCE`)
2. `date`: Temporal daily partition (`YYYY-MM-DD`)
3. `bucket(16, symbol)`: Hash-bucketed partition for high-volume equities to prevent single hot directories.

### Columnar Data Skipping
Each manifest entry tracks column-level min/max statistics for every Parquet data file:
- `timestamp_ns`: Min and Max allow query engines to bypass 99% of data files when querying specific trading sessions.
- `symbol_id`: Min and Max enable exact pruning in multi-symbol partition files.
- `price`: Bounding boxes allow instant skipping for out-of-money option depth queries.

---

## 5. ACID Transaction Protocol (Optimistic Concurrency Control)

Obsidian Lakehouse implements snapshot isolation using Optimistic Concurrency Control (OCC):

```
1. READ SNAPSHOT   ─► Read current snapshot metadata (version N).
2. STAGE DATA      ─► Write new Parquet data files to staging path.
3. CONFLICT CHECK  ─► Verify snapshot version N hasn't been committed over with overlapping partitions.
4. ATOMIC COMMIT   ─► Write new manifest and atomically advance snapshot to N+1 via atomic rename.
```

If a concurrent writer commits an overlapping partition between Step 1 and Step 4:
- The transaction aborts with `StatusCode::ConcurrentModification`.
- Exponential backoff with jitter triggers an automatic retry.

---

## 6. Compaction & Vacuum Policy

| Operation | Trigger | Target | Strategy |
|-----------|---------|--------|----------|
| **Small File Compaction** | File size < 32 MB or intraday batch end | 128 MB – 256 MB Parquet row groups | Bin-packing within identical partition |
| **Snapshot Vacuum** | Daily scheduled maintenance | Expire snapshots > 7 days old | Mark unreferenced data files for hard deletion |
| **Orphan Cleanup** | Weekly | Failed transaction temp files | Remove unstaged files older than 24 hours |

---

## 7. Zero-Copy In-Memory Vectorisation

All storage readers yield contiguous, 64-byte cache-line aligned **Columnar Batches** (`obsidian::storage::arrow::ColumnarBatch`):
- Memory layout is fully binary-compatible with Apache Arrow RecordBatches.
- Direct pointer sharing with the SIMD Vector Engine (`obsidian_simd`) without intermediate serialization.
- DuckDB and ClickHouse readers can map Columnar Batches directly into query execution pipelines.
