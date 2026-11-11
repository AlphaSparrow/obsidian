# Strategic Roadmap: Phase Ideation & Architectural Next Steps

> Forward-looking development blueprint uniting the C++20 low-latency core with the Python behavioral quantitative machine learning pipeline.

---

## 1. Context & Motivation

With both the **C++20 infrastructure** (Kernel, Lakehouse Storage, Orchestrated Runtime) and the **Python Behavioral Quant Research** (Cumulative Prospect Theory, Walk-Forward ML, SHAP Analysis) established, the next architectural priority is **inter-system convergence**.

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          Obsidian Convergence Engine                         │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│   [ 16-Asset Historical CSVs ] ──▶ [ Lakehouse Ingestion Pipeline ]         │
│                                                │                            │
│                                                ▼                            │
│   [ C++ SIMD CPT Feature Engine ] ◀── [ Columnar File Reader ]              │
│                 │                                                           │
│                 ▼                                                           │
│   [ Embedded ML Inference Stage ] ──▶ [ EventBus Strategy Triggers ]        │
│                                                │                            │
│                                                ▼                            │
│   [ Terminal Desktop GUI ] ◀─────── [ Real-Time Execution Engine ]          │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Proposed Architectural Phases

### Phase A · C++ SIMD Vectorized CPT & Behavioral Feature Engine
*Target Subsystem*: `compute/simd/` and `compute/quant/`

- **Motivation**: In Python, computing 60-day rolling Cumulative Prospect Theory values across 50,000+ rows takes significant wall-clock time due to non-vectorized Python loops inside `pandas.apply()`.
- **Implementation**:
  - Implement C++20 `ProspectTheoryEngine` using AVX2 / AVX-512 vectorized power approximations ($x^{0.88}$).
  - Implement rolling window percentile ranking, 3rd moment Fisher-Pearson skewness, and maximum return tracking in $O(N)$ using sliding window deques.
  - Expected performance: sub-100 millisecond feature generation for 100,000+ historical bars.

---

### Phase B · Lakehouse Historical Crypto Universe Ingestion
*Target Subsystem*: `storage/dataset/` and `storage/lakehouse/`

- **Motivation**: Currently, `dataset/*.csv` resides on disk as raw flat text files. Ingesting this universe into the Lakehouse table format enables zero-copy Arrow memory mapping, partition pruning, and DuckDB analytics.
- **Implementation**:
  - Ingest all 16 assets (`Bitcoin`, `Ethereum`, `Solana`, `Cardano`, `Dogecoin`, etc.) into partition schema: `symbol=<ASSET>/year=<YYYY>/data-001.obsd`.
  - Apply **Gorilla XOR codec** for OHLC prices and **Delta-of-Delta codec** for timestamps.
  - Commit transactions atomically to the Lakehouse manifest catalog.

---

### Phase C · Real-Time ML Inference & Scoring Pipeline Stage
*Target Subsystem*: `runtime/pipeline/` and `engines/inference/`

- **Motivation**: Deploying the trained Random Forest / XGBoost model (`Test_AUC = 0.9215`) into the low-latency execution path.
- **Implementation**:
  - Export trained models to ONNX or header-only C++ compiled decision forest evaluator (zero Python runtime dependencies in production).
  - Implement `LotteryScoringStage : public IStage` in `runtime/pipeline/`.
  - Stream input market ticks through the pipeline and emit `EventType::SignalGenerated` events onto the `EventBus` when speculative lottery thresholds are breached.

---

### Phase D · Quantitative Execution & Risk Engine
*Target Subsystem*: `engines/execution/` and `engines/risk/`

- **Motivation**: Populate the empty `engines/` directory with automated order management and capital preservation controls.
- **Implementation**:
  - `RiskManager`: Hard loss limits, maximum position sizes, portfolio turnover caps, and drawdown circuit breakers.
  - `OrderManager`: Simulated exchange broker, limit/market order book matching, fill tracking, and slippage modeling.
  - Behavioral strategy: Exploits speculative retail attention windows identified by the CPT lottery model.

---

### Phase E · Terminal Dashboard & Real-Time Visualization
*Target Subsystem*: `terminal/desktop/`

- **Motivation**: Surface live analytics to human traders and quantitative researchers.
- **Implementation**:
  - Integrate Dear ImGui charts for candlestick bars, rolling CPT values, and lottery score overlays (matching `plots/plot_time_series_overlay.png`).
  - Lakehouse partition inspector and storage health telemetry pane.
  - Event bus throughput monitor and component lifecycle controls.
