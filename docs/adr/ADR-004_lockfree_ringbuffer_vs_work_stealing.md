# ADR-004: Lock-Free SPSC/MPMC Ring Buffer vs. Work-Stealing Queues for Tick Ingestion

## Status
**Accepted**

## Context
In the Obsidian quantitative runtime, market tick data must traverse from network ingestion sockets to feature calculation (CPT, GARCH, momentum) and strategy dispatch within nanosecond budgets. The primary design dilemma centers on the inter-thread messaging paradigm between the network I/O worker and strategy execution actors.

We evaluated two architectural patterns:
1. **Work-Stealing Task Queues** (e.g., Intel TBB, C++ std::jthread worker pools, Go-style work queues): Dynamically distributing market ticks across worker threads with deque stealing when workers starve.
2. **Lock-Free Pinned SPSC/MPMC Ring Buffers** (Disruptor / LMAX architecture): Dedicated thread pinning with single-producer single-consumer circular buffers, cacheline padding, and memory fences.

## Architectural Trade-Off Analysis

| Dimension | Work-Stealing Queues | Lock-Free Pinned Ring Buffer (Disruptor) |
| :--- | :--- | :--- |
| **P50 Latency** | ~450 ns - 1.2 $\mu$s | **~35 - 75 ns** |
| **P99.99 Tail Latency** | Extreme jitter (> 15 $\mu$s) due to lock contention & deque stealing | **Deterministic (< 180 ns)** |
| **Cache Behavior** | High L1/L2 cache invalidation; tasks migrate across CPU cores | **High locality; producer and consumer pinned to isolated cores** |
| **Memory Allocation** | Dynamic heap allocation per task descriptor | **Zero heap allocation** post-initialization |
| **False Sharing** | Prone to inter-core cache line bouncing | **Eliminated** via 64-byte alignment (`alignas(64)`) |
| **Throughput Under Load** | High under batch workloads | **> 40M messages/sec** per core pair |
| **Load Balancing** | Automatic across heterogeneous cores | Requires static topology assignment |

## Decision
We adopt **Lock-Free Pinned SPSC Ring Buffers** for the hot-path market data ingestion and strategy execution pipeline. 

Specific implementation mandates:
- Ring buffers must be statically allocated at startup with powers of 2 sizing ($2^N$) to permit bitwise mask indexing (`idx & (capacity - 1)`).
- Head and tail sequence cursors must reside on distinct cache lines via `alignas(OBSIDIAN_CACHE_LINE_SIZE)` to prevent false sharing.
- Memory ordering utilizes `std::memory_order_release` on cursor updates and `std::memory_order_acquire` on cursor reads.
- Dynamic work-stealing is strictly relegated to non-latency-critical background components (e.g., Parquet Lakehouse compaction, batch parameter scans).

## Consequences
- **Positive**: Eliminates GC and allocator overhead; deterministic P99.99 latency; guarantees tick chronological ordering per symbol stream.
- **Negative**: Fixed capacity requires proactive backpressure handling (head-dropping or circular overwrites with telemetry alarms).
