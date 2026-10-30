## 📋 Description of Changes

<!-- Provide a concise description of the motivation, design decisions, and architectural impact of this change. -->

### 🏷️ Subsystem Affected
- [ ] `kernel` (Concurrency, lock-free queues, threadpool, memory allocators)
- [ ] `storage` (Lakehouse catalog, MVCC snapshot isolation, Arrow columnar batch, compaction)
- [ ] `compute` (Vector kernels, pricing engines, financial maths)
- [ ] `runtime` (Pipeline orchestration, execution graph)
- [ ] `ci / infra` (CI/CD workflows, toolchains, CMake presets, packaging)

---

## 🏛️ DDIA Architecture & Reliability Invariants

Please verify that your change adheres to the system invariants:
- [ ] **DDIA Chapter 7 (ACID & Isolation):** Modifications preserve atomicity and uncommitted rollback invariants.
- [ ] **Non-blocking MVCC:** Readers never block writers, and writers never block readers.
- [ ] **DDIA Chapter 1 (Latency & SLAs):** No critical-path latency regressions; complies with `docs/architecture/08_performance_budget.md`.
- [ ] **Memory Safety:** Passes AddressSanitizer (ASan) and UndefinedBehaviorSanitizer (UBSan).
- [ ] **Concurrency:** Passes ThreadSanitizer (TSan) without data races or lock inversions.

---

## 🧪 Testing & Verification
- [ ] Added unit or integration tests in `tests/`.
- [ ] Verified locally with `cmake --preset dev` or `cmake --preset ci-linux-sanitizers`.
- [ ] All CTest suites passing.

---

## 📦 Documentation & Supply Chain
- [ ] Code formatted according to `.clang-format`.
- [ ] Static analysis clean (`clang-tidy` and `cppcheck`).
- [ ] Architecture documentation in `docs/` updated if internal APIs or formats changed.
