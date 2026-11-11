# ADR-005: Native C++ Branchless Forest Inference vs. ONNX Runtime FFI

## Status
**Accepted**

## Context
Obsidian integrates behavioral finance models (trained via XGBoost and Random Forest in `src/train.py`) that predict speculative lottery-demand spikes in crypto assets from prospect theory variables ($v(x), w(p)$, skewness, volume shocks).

To execute inference during live streaming bars, we evaluated two integration approaches:
1. **ONNX Runtime C++ FFI**: Exporting trained scikit-learn / XGBoost models to `.onnx` and invoking ONNX Runtime C++ API (`Ort::Session`).
2. **Native C++ Branchless Code Generation / Decision Kernel**: Transpiling tree nodes into static flat arrays or branchless bitmask lookups compiled directly into the `obsidian_runtime` binary.

## Architectural Trade-Off Analysis

| Metric | ONNX Runtime C++ FFI | Native C++ Decision Tree Kernel |
| :--- | :--- | :--- |
| **Inference Latency** | 12 - 35 $\mu$s per sample (FFI + tensor marshalling) | **0.15 - 0.65 $\mu$s per sample** |
| **Binary Footprint** | Heavy (+45 MB shared libraries) | **Lightweight (< 120 KB native code)** |
| **Memory Allocations** | Tensor creation on heap per inference call | **Zero allocations** (in-place buffer evaluation) |
| **SIMD Vectorization** | Opaque to compiler auto-vectorizer | Fully vectorizable via AVX2 / AVX-512 intrinsics |
| **Branch Mispredictions**| Dynamic pointer chasing through runtime graph | Branchless conditional moves (`cmov`) or flat table scans |
| **Deployment Simplicity**| Dynamic library version matching across environments | Fully self-contained static binary |
| **Model Flexibility** | Arbitrary neural network architectures supported | Restricted to tree ensembles and linear combinations |

## Decision
We select **Native C++ Decision Forest Compilation** for production trading runtime inference.

Key architectural rules:
- Tree structures are flattened into contiguous, cache-aligned arrays (`struct TreeNode { float split_val; uint16_t left_child; uint16_t feature_idx; }`).
- Inference functions are marked `noexcept` and operate directly over `std::span<const float>` feature slices.
- ONNX Runtime and Python bindings remain supported exclusively in the offline research sandbox (`research/`, `src/`) for rapid model exploration and SHAP feature attribution.

## Consequences
- **Positive**: 20x to 50x lower inference latency; no external shared library runtime dependencies; predictable memory profile.
- **Negative**: Retraining models requires emitting an updated header/source file or binary parameter file and recompiling/reloading the inference stage.

