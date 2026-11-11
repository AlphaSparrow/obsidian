# 09 · State of the Platform: Dual C++ & Python Architecture

> Comprehensive system map, subsystem capabilities, and architectural audit of the Obsidian quantitative finance platform.

---

## 1. Executive Summary & Platform Mission

**Obsidian** is an end-to-end quantitative trading, market data lakehouse, and behavioral asset pricing platform. It unites two distinct performance paradigms:
1. **Ultra-Low Latency C++20 Core** (`kernel/`, `storage/`, `runtime/`, `compute/`): Cacheline-aligned memory, lock-free concurrency, zero-copy Arrow ABI, columnar file formats, Castagnoli CRC32C bit-rot protection, direct memory-mapped journals, and deterministic task orchestration.
2. **Behavioral Machine Learning & Research Layer** (`src/`, `dataset/`, `models/`, `plots/`): Empirical behavioral finance pipeline calculating Cumulative Prospect Theory (CPT) metrics, walk-forward time-series models, and SHAP explainability.

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│                          Obsidian Platform Architecture                         │
├───────────────────────────────────────┬─────────────────────────────────────────┤
│    C++20 High-Performance Subsystem   │   Python Quantitative Research & ML     │
├───────────────────────────────────────┼─────────────────────────────────────────┤
│  • kernel/ (concurrency, memory, log) │  • src/features.py (CPT mathematics)    │
│  • storage/ (lakehouse, parquet, mmap)│  • src/train.py (walk-forward CV)       │
│  • runtime/ (event_bus, DAG, sched)   │  • src/explain_shap.py (tree SHAP)      │
│  • compute/ (simd dispatch)           │  • src/plots.py (ROC/PR/time-series)    │
│  • terminal/ (desktop GUI harness)    │  • dataset/ (16 assets, 50k+ bars)      │
└───────────────────────────────────────┴─────────────────────────────────────────┘
```

---

## 2. Subsystem Capability Inventory

### 2.1 Storage Subsystem (`obsidian_storage`)

| Module | Location | Capabilities & Defensive Mechanisms |
|---|---|---|
| **Columnar File Format** | `storage/parquet/` | 64-byte aligned binary columnar writer/reader, min/max statistics generation, projection pushdown (`ReadColumns`), block checksum validation. |
| **Bit-Rot Detection** | `storage/common/` | Table-driven Castagnoli hardware-polynomial CRC32C (`0x82F63B78`) over memory spans and storage blocks. |
| **Compression Codecs** | `storage/codecs/` | Facebook Gorilla XOR floating-point price codec (with `NaN`/`Inf` anomaly rejection) and Delta-of-Delta integer timestamp/sequence codec (with clock regression detection). |
| **Memory-Mapped Journal** | `storage/mmap/` | Cross-platform zero-copy direct I/O (`mmap` on POSIX, `CreateFileMapping` on Win32); framing headers (`0x4F4253444D4D4150ULL`), torn-write boundary detection, and crash auto-recovery. |
| **Block Cache** | `storage/cache/` | Thread-safe concurrent LRU block cache with in-memory CRC32C validation on cache hits to trap bit-rot, rogue pointer overwrites, and cache poisoning. |
| **Lakehouse Catalog** | `storage/lakehouse/` | ACID transactions, manifest versioning, partition data-skipping (`PartitionSpec`), compaction engine, vacuuming, and orphan file leak detector (`IntegrityGuard`). |
| **MVCC Snapshots** | `storage/snapshots/` | Snapshot lineage tree verification, point-in-time time-travel reader, optimistic concurrency conflict detection across partitions, and query pinning guards. |
| **Dataset Ingestor** | `storage/dataset/` | Fast ISO-8601 parser, financial anomaly quarantine (`High < Low`, negative prices/volume, non-monotonic timestamps), and conversion into aligned `ColumnarBatch` instances. |
| **Analytical Bridges** | `storage/duckdb/` & `storage/clickhouse/` | Apache Arrow C Data Interface ABI zero-copy exporter, vectorized DuckDB filter scan, ClickHouse MergeTree DDL generator, and native `RowBinary` stream encoder. |

---

### 2.2 Runtime Subsystem (`obsidian_runtime`)

| Module | Location | Capabilities & Defensive Mechanisms |
|---|---|---|
| **Event Bus** | `runtime/event_bus/` | Low-latency typed publish-subscribe bus; topic ID and `EventType` filtering; synchronous and asynchronous ring-buffered dispatch; subscriber fault isolation. |
| **Service Registry** | `runtime/registry/` | Type-safe IoC service locator (`RegisterService<T>`); 8-state component lifecycle (`Uninitialized` → `Running` → `Stopped`); aggregate Liveness and Readiness probes. |
| **Dependency Graph** | `runtime/dependency_graph/` | Directed Acyclic Graph (DAG) for tasks; cycle detection via Kahn's algorithm; topological sort partitioning tasks into parallel execution waves (`BuildExecutionWaves()`). |
| **Task Scheduler** | `runtime/scheduler/` | Nanosecond-resolution deterministic min-heap priority scheduler; interval, delayed, and market tick triggers; drift compensation and missed-tick catchup policies (`SkipMissed`). |
| **Streaming Pipeline** | `runtime/pipeline/` | Pipelined streaming processing (`IStage`, `DataBatch`); dynamic backpressure mitigation with `Block`, `DropOldest`, and `RejectNew` queue policies. |
| **Plugin Architecture** | `runtime/plugins/` | Dynamic plugin engine conforming to `07_plugin_system.md`; `IPlugin` hooks (`onMarketTick`, `onSignal`); strict manifest schema validation and crash isolation. |
| **Runtime Orchestrator** | `runtime/orchestration/` | Central `RuntimeEngine` coordinating EventBus, Registry, Scheduler, Pipeline, and Plugins into a coherent execution lifecycle. |

---

### 2.3 Machine Learning & Research Subsystem (`src/` & `dataset/`)

- **Universe**: 16 cryptocurrencies with historical daily OHLCV from 2018/2019 to late 2026.
- **Combined Master Dataset**: `dataset/combined_crypto_data.csv` (50,923 rows).
- **Engineered ML Features**: `dataset/processed_ml_features.csv` (50,427 rows, 60-day rolling CPT, skewness, volatility, max return, turnover).
- **Validated Models**:
  - `models/rf_lottery_model.pkl`: Random Forest Classifier (`Test_AUC = 0.9215`, `Test_F1 = 0.7074`).
  - `models/xgboost_lottery_model.pkl`: XGBoost Classifier (`Test_AUC = 0.9182`, `Test_F1 = 0.7045`).
- **Research Artifacts**:
  - ROC and Precision-Recall Curves (`plots/plot_roc_curve.png`, `plots/plot_pr_curve.png`).
  - SHAP feature attribution summary and dependence plots (`plots/shap_summary_plot.png`, `plots/shap_dependence_pt.png`).
  - Bitcoin predictive speculative window overlay (`plots/plot_time_series_overlay.png`).

---

### 2.4 Kernel, Compute & Terminal

- `kernel/concurrency/`: Lock-free MPMC queue, Disruptor ring buffer, work-stealing threadpool, atomic utilities.
- `kernel/memory/`: Cacheline-aligned allocator, arena allocator, object pool.
- `compute/simd/`: Dynamic CPU microarchitecture dispatcher for AVX-512, AVX2, SSE4.2, and ARM NEON.
- `terminal/desktop/`: Desktop GUI application with ImGui rendering backend and resource manifests.

---

### 2.5 Current Unimplemented Stubs & Whitespace

The following directories exist as placeholders in the repository map and represent key expansion targets:
- `crypto/`: Low-level exchange gateway connectors (Binance, Coinbase, Kraken websockets/FIX).
- `engines/`: Strategy execution engine, risk management engine, portfolio optimizer.
- `infra/`: Terraform / Kubernetes / Docker deployment manifests.
- `research/`: Jupyter research notebooks and backtesting harnesses.
