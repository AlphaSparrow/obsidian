# Obsidian

> High-performance quantitative finance platform engineered for ultra-low tail latency, lock-free concurrency, and deterministic execution.

## Architecture Overview

```
obsidian/
├── kernel/
│   ├── concurrency/           # Low-latency lock-free concurrency primitives
│   │   ├── atomic_utils.hpp   # SpinWait, TTAS SpinLock, TicketLock, SeqLock, CachePadded
│   │   ├── lockfree_queue.hpp # Dmitry Vyukov bounded MPMC lock-free queue
│   │   ├── lockfree_queue.cpp # Pre-instantiated MPMC types
│   │   ├── work_stealing.hpp  # Zero-allocation Chase-Lev work-stealing deque (LIFO/FIFO)
│   │   ├── work_stealing.cpp  # Work-stealing deque instantiations
│   │   ├── thread_pool.hpp    # Work-stealing thread pool with affinity & wake events
│   │   ├── thread_pool.cpp    # Distributed work-stealing implementation
│   │   ├── task_scheduler.hpp # Multi-lane priority scheduler (RealTime, Normal, Low)
│   │   ├── task_scheduler.cpp # Prioritized non-blocking batch execution
│   │   ├── disruptor.hpp      # LMAX Disruptor lock-free ring buffer (zero-alloc HFT pipeline)
│   │   ├── channels.go        # Go lock-free RingChannel with Vyukov sequence ticketing
│   │   ├── actor_runtime.rs   # Rust lock-free MPSC actor runtime + C-ABI FFI
│   │   ├── Cargo.toml         # Rust crate config (opt-level=3, thin LTO, panic=abort)
│   │   └── build.rs           # cbindgen C-ABI header generation
│   ├── memory/                # Cache-aligned allocators, object pools, bump allocators
│   └── logging/               # Zero-allocation lock-free logging
├── compute/                   # Numerical routines, math kernels, SIMD acceleration
└── terminal/                  # CLI & Desktop interactive financial terminal
```

## Concurrency Subsystem Status

| Component | Language | Paradigm / Algorithm | Status |
|---|---|---|---|
| `atomic_utils.hpp` | C++ | TTAS SpinLock, TicketLock, SeqLock, CachePadded | Optimized |
| `lockfree_queue` | C++ | Vyukov Bounded MPMC Queue (O(1) wait-free) | Optimized |
| `work_stealing` | C++ | Chase-Lev Deque (SPMC, zero-alloc in-place cells) | Optimized |
| `thread_pool` | C++ | Work-stealing Thread Pool + Affinity + Idle Parking | Optimized |
| `task_scheduler` | C++ | Multi-lane Priority Scheduler (RealTime/Normal/Low) | Optimized |
| `disruptor.hpp` | C++ | LMAX Disruptor Pre-allocated RingBuffer | **NEW** |
| `channels.go` | Go | Cache-aligned Lock-Free RingChannel[T] | Implemented |
| `actor_runtime.rs` | Rust | MPSC Lock-Free Actors + Batch Send + C-ABI | Optimized |

## Next Steps

See [Concurrency Roadmap](docs/roadmap/concurrency_roadmap.md) and [Kernel Concurrency Guide](docs/kernel/02_concurrency.md) for future milestones.