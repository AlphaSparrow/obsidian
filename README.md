# Obsidian

[![Obsidian CI / CD Pipeline](https://github.com/AlphaSparrow/obsidian/actions/workflows/ci.yml/badge.svg)](https://github.com/AlphaSparrow/obsidian/actions/workflows/ci.yml)
[![Obsidian PR Hygiene & Quality Gate](https://github.com/AlphaSparrow/obsidian/actions/workflows/pr_hygiene.yml/badge.svg)](https://github.com/AlphaSparrow/obsidian/actions/workflows/pr_hygiene.yml)
[![Obsidian CodeQL Security Analysis](https://github.com/AlphaSparrow/obsidian/actions/workflows/codeql.yml/badge.svg)](https://github.com/AlphaSparrow/obsidian/actions/workflows/codeql.yml)
[![License: Apache-2.0](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)
[![C++ Standard](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Architecture](https://img.shields.io/badge/Architecture-DDIA--Compliant-success.svg)](docs/architecture/)

High-performance quantitative finance platform & DDIA-compliant lakehouse storage engine engineered for sub-millisecond market data ingestion, low-latency execution, and atomic MVCC transactions.

---

## 🏛️ Architecture Overview

- **Kernel Subsystem (`kernel/`)**: Lock-free LMAX-style Disruptor ring buffers, cacheline-aligned memory pools, NUMA-aware arenas, and a work-stealing threadpool dispatcher.
- **Compute Subsystem (`compute/`)**: Runtime SIMD dispatch (AVX-512, AVX2, NEON), numerical pricing kernels, and vectorized Cumulative Prospect Theory (CPT) behavioral feature extraction.
- **Storage Subsystem (`storage/`)**: Lakehouse transactional engine featuring Arrow columnar batching, optimistic concurrency control (OCC), manifest snapshot management, data skipping, Gorilla/Delta-Delta codecs, mmap journaling with torn-write recovery, and DuckDB/ClickHouse zero-copy bridges.
- **Runtime Subsystem (`runtime/`)**: EventBus with lock-free topic dispatch, 8-state component lifecycle registry, task dependency graph (DAG) with parallel wave scheduling, nanosecond-precision deterministic scheduler, streaming pipelines with backpressure, and native ML decision forest inference.
- **Behavioral Quant ML Layer (`src/`)**: Tversky & Kahneman (1992) Cumulative Prospect Theory modeling, expanding-window walk-forward cross-validation (Random Forest AUC 0.9215), SHAP attribution diagnostics, and 16-asset cryptocurrency universe analytics.
- **Terminal UI (`terminal/`)**: Low-latency desktop analytics terminal for real-time market visualization and order execution.

---

## ⚡ Quickstart & Local Building

Obsidian leverages standardized **CMake Presets** (CMake >= 3.25) and Ninja for deterministic cross-platform compilation.

### Prerequisites

- **CMake** >= 3.25
- **Ninja** >= 1.10
- **C++20 Compiler**: Clang 16+, GCC 13+, or MSVC 2022+
- **Ccache** (recommended for rapid rebuilds)

### Common Build Presets

```bash
# Fast inner-loop development (Debug with ASan + UBSan)
cmake --preset dev
cmake --build --preset dev

# Run full test suite
ctest --preset dev

# Native CPU release build (with ThinLTO)
cmake --preset release-native
cmake --build --preset release-native

# DDIA memory safety & latency benchmark verification
cmake --preset ci-linux-sanitizers
cmake --build --preset ci-linux-sanitizers
./build/ci-linux-sanitizers/tests/obsidian_ddia_tests --benchmark
```

---

## 🛡️ CI / CD Pipeline

Obsidian employs an industrial-grade, multi-stage continuous integration and distribution pipeline:

1. **Memory Safety & Sanitizers**: AddressSanitizer (ASan) & UndefinedBehaviorSanitizer (UBSan) verification.
2. **Concurrency & Race Detection**: ThreadSanitizer (TSan) testing across Disruptor, LockFreeQueue, and Lakehouse OCC transactions.
3. **Static Analysis & Linting**: `clang-format` (Google/LLVM style), `cppcheck` deep analysis, and `clang-tidy` modernize & performance checks.
4. **Code Coverage Quality Gate**: LCOV/gcov automated branch/statement tracking with minimum threshold enforcement.
5. **Supply Chain Security & SBOM**: Standardized SPDX 2.3 and CycloneDX 1.5 SBOM generation, combined with Trivy dependency vulnerability auditing.
6. **CodeQL Semantic Analysis**: GitHub Advanced Security semantic scanning for memory safety and vulnerabilities.
7. **Performance SLA Guard**: Automated tail latency (`p50`, `p95`, `p99`) verification against architectural budgets in `docs/architecture/08_performance_budget.md`.
8. **Hermetic Docker Build**: Multi-stage minimal container creation with in-container smoke tests.
9. **Cross-Platform Matrix & CPack Distribution**: Multi-architecture packaging (Linux tarball/deb, Windows zip, macOS universal tarball) with SHA-256 and SHA-512 cryptographic digests.
10. **PR Hygiene & Conventional Commits**: Strict verification of commit message format and automated scans against accidental secret/credential check-ins.

---

## 🤝 Contributing

We welcome contributions from the community! Please see our [**Contributing Guide**](CONTRIBUTING.md) for details on code style, Conventional Commits, DDIA invariants, and pull request procedures.

---

## 📜 License

Distributed under the [Apache License 2.0](LICENSE).
