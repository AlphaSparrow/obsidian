# Contributing to Obsidian

Thank you for your interest in contributing to **Obsidian**! Obsidian is a high-performance quantitative finance platform and DDIA-compliant lakehouse storage engine engineered for sub-millisecond execution, lock-free concurrency, and atomic MVCC transactions.

We hold our codebase to rigorous engineering standards regarding latency budgets, memory safety, and thread safety. This document provides clear guidelines for setting up your environment, writing code, running local quality gates, and opening pull requests.

---

## 🧭 Table of Contents

- [Code of Conduct](#-code-of-conduct)
- [Prerequisites & Development Setup](#-prerequisites--development-setup)
- [CMake Presets & Build Workflow](#-cmake-presets--build-workflow)
- [Commit Message Conventions](#-commit-message-conventions)
- [Architecture & DDIA Invariants](#-architecture--ddia-invariants)
- [Testing & Quality Verification](#-testing--quality-verification)
- [Static Analysis & Code Style](#-static-analysis--code-style)
- [Pull Request Checklist](#-pull-request-checklist)

---

## 🤝 Code of Conduct

We are committed to providing a welcoming, inclusive, and professional environment for all contributors. Please ensure all interactions remain respectful, constructive, and focused on collaborative technical excellence.

---

## 🛠️ Prerequisites & Development Setup

Before building Obsidian, ensure you have the following installed:

- **CMake**: Version `3.25` or higher
- **Ninja**: Version `1.10` or higher (recommended build generator)
- **C++20 Compiler**:
  - Clang 16+ (LLVM)
  - GCC 13+
  - MSVC 2022+ (v143 toolset)
- **Python**: Version `3.10` or `3.11+` (for CI test validation scripts)
- **Ccache**: Strongly recommended to speed up compilation
- **Static Analysis Tools** (Optional for local development, enforced in CI):
  - `clang-format` (LLVM 17+)
  - `clang-tidy` (LLVM 17+)
  - `cppcheck` (2.10+)
  - `lcov` (for code coverage reports)

### Cloning the Repository

```bash
git clone --recurse-submodules https://github.com/AlphaSparrow/obsidian.git
cd obsidian
```

---

## ⚡ CMake Presets & Build Workflow

Obsidian uses standardized CMake Presets defined in [`CMakePresets.json`](CMakePresets.json) for cross-platform reproducibility.

### Common Presets

| Preset Name | Purpose | Build Type | Flags / Sanitizers |
| :--- | :--- | :--- | :--- |
| `dev` | Daily fast inner-loop development | `Debug` | ASan + UBSan enabled, `generic` arch |
| `perf` | Latency profiling & benchmarks | `RelWithDebInfo` | Native CPU target, no sanitizers |
| `release-native` | Production release bundle | `Release` | ThinLTO enabled, dead-code stripped |
| `ci-linux-sanitizers` | CI Memory safety verification | `Debug` | ASan + UBSan, `x86-64-v3` baseline |
| `ci-linux-tsan` | CI Concurrency & race detection | `RelWithDebInfo` | ThreadSanitizer (TSan) enabled |
| `ci-coverage` | Test coverage instrumentation | `Debug` | LCOV / gcov instrumentation |

### Building Locally

```bash
# 1. Configure with the development preset
cmake --preset dev

# 2. Compile targets in parallel
cmake --build --preset dev

# 3. Execute all unit and integration tests
ctest --preset dev --output-on-failure
```

---

## 📝 Commit Message Conventions

Obsidian strictly enforces the **Conventional Commits** standard (validated automatically via [`tools/ci/pr_hygiene_check.py`](tools/ci/pr_hygiene_check.py)).

### Format

```
<type>(<scope>): <short description>
```

- **Type** must be one of:
  - `feat`: A new feature or capability
  - `fix`: A bug fix
  - `perf`: A performance or latency improvement
  - `docs`: Documentation updates
  - `test`: Adding or refactoring test cases
  - `refactor`: Code reorganization with no functional changes
  - `style`: Formatting, whitespace, or style corrections
  - `build`: Build system or preset updates
  - `ci`: CI/CD workflows and automated scripts
  - `chore`: Auxiliary changes (dependencies, tooling)
  - `revert`: Reverting an earlier commit

- **Scope** indicates the affected subsystem (optional, but encouraged):
  - `kernel`: RingBuffer, Disruptor, Allocators, ThreadPool
  - `storage`: Lakehouse catalog, MVCC, Arrow columnar batch, transactions
  - `compute`: SIMD dispatch, pricing, vector math
  - `runtime`: Orchestration, pipelines, event bus
  - `terminal`: Desktop UI, charts, renderer
  - `ci`: Workflows, linter, SBOM, SLA guards
  - `data`: Datasets, schemas, CSV/parquet sources

### Examples

- ✅ `feat(storage): implement optimistic concurrency control for manifest commits`
- ✅ `perf(kernel): reduce Disruptor ring buffer producer contention`
- ✅ `fix(terminal): handle window resize event in gdiplus desktop renderer`
- ❌ `fixed bug in table` (Missing conventional commit prefix and scope)
- ❌ `WIP: updates` (Non-descriptive work-in-progress commit)

---

## 🏛️ Architecture & DDIA Invariants

When submitting PRs touching `kernel/` or `storage/`, contributions must strictly adhere to the system's core invariants:

1. **Non-blocking MVCC Isolation (DDIA Chapter 7)**:
   - Readers must **never** block writers, and writers must **never** block readers.
   - Transaction aborts must clean up staging files immediately without corrupting table state.
2. **Deterministic Manifest Sequencing**:
   - Snapshot manifests are atomic and append-only.
   - Version advancement occurs strictly via OCC comparison against catalog state.
3. **Latency & SLA Budgets (DDIA Chapter 1)**:
   - Core path execution must comply with budgets in [`docs/architecture/08_performance_budget.md`](docs/architecture/08_performance_budget.md).
   - `Transaction Commit`: `p50 < 1.5ms`, `p99 < 25ms`.
   - `Snapshot Scan`: `p50 < 500µs`, `p99 < 10ms`.
4. **Zero Allocation on Hot Paths**:
   - Market data ingestion loops must not invoke dynamic heap allocations (`malloc`/`new`). Use arenas or object pools from `obsidian_kernel_memory`.

---

## 🧪 Testing & Quality Verification

Every pull request must maintain or increase test coverage (quality gate threshold: **75.0%**).

### Running Test Suites

```bash
# Run unit and lakehouse tests
ctest --preset dev --output-on-failure

# Execute DDIA benchmark and generate markdown report
./build/ci-linux-sanitizers/tests/obsidian_ddia_tests --benchmark --report=ddia_benchmark.md

# Execute storage chaos and crash-recovery test
./build/ci-linux-sanitizers/tests/obsidian_ddia_tests --chaos
```

### Local CI Quality Gate Dry-Run

You can run the same automated validation scripts used by CI locally:

```bash
# 1. PR Hygiene & Commit Convention Check
python3 tools/ci/pr_hygiene_check.py --root .

# 2. SLA Budget Guard
python3 tools/ci/sla_budget_guard.py --mock-if-empty

# 3. Documentation & Cross-Reference Link Check
python3 tools/ci/validate_docs.py --docs docs --root .

# 4. SBOM Manifest Generation
python3 tools/ci/generate_sbom.py --root . --output-spdx spdx.json --output-cyclonedx cdx.json
```

---

## 🎨 Static Analysis & Code Style

Obsidian follows modern C++20 conventions and LLVM/Google style rules:

- **Formatting**: Run `clang-format` on changed files using the project's [`.clang-format`](.clang-format):
  ```bash
  clang-format -i path/to/file.cpp
  ```
- **Static Analysis**: Verify with `clang-tidy` ([`.clang-tidy`](.clang-tidy)) and `cppcheck`:
  ```bash
  cppcheck --enable=warning,performance,portability --std=c++20 kernel storage compute tests
  ```
- **File Hygiene**:
  - Do **not** commit keys, credentials, or `.env` files (templates like `.env.example` are permitted).
  - Do **not** track binaries or data files larger than `5 MB`.

---

## 🚀 Pull Request Checklist

Before opening or requesting review on a Pull Request:

1. [ ] Branch is based on the latest `main` branch.
2. [ ] PR title and all commits conform to [Conventional Commits](#-commit-message-conventions).
3. [ ] Code compiles cleanly under `-Wall -Wextra -Werror` (enforced via CMake).
4. [ ] AddressSanitizer and UndefinedBehaviorSanitizer pass with zero errors.
5. [ ] ThreadSanitizer passes with zero data races.
6. [ ] New tests added for any new feature or bug fix.
7. [ ] Architecture documentation in `docs/` updated if API contracts changed.
8. [ ] Pull request template is fully filled out.

Thank you for helping build **Obsidian** into the premier low-latency quantitative platform!
