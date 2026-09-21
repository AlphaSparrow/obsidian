# 02 · Concurrency Subsystem

## Overview
The Obsidian Concurrency Subsystem provides deterministic, ultra-low-latency synchronization and execution primitives designed for high-frequency algorithmic trading and financial computation.

## Core Components

### 1. Atomic Utilities (`atomic_utils.hpp`)
- `cpu_pause()` & `thread_yield()`: Architecture-tuned spin loop controls (`_mm_pause` / `YieldProcessor`).
- `SpinLock`: Test-and-Test-and-Set (TTAS) spinlock.
- `TicketLock`: Fair FIFO spinlock preventing thread starvation under contention.
- `SeqLock`: Single-writer, optimistic lock-free multi-reader access for market data quotes and telemetry.
- `CachePadded<T>`: Isolates hot shared variables onto dedicated 64-byte cache lines.

### 2. Lock-Free MPMC Queue (`lockfree_queue.hpp` / `.cpp`)
- Dmitry Vyukov bounded MPMC queue with sequence-ticketed ring buffer cells.
- O(1) wait-free progression under low-to-medium contention.
- Bulk dequeue and approximate size tracking.

### 3. Chase-Lev Work-Stealing Deque (`work_stealing.hpp` / `.cpp`)
- Single-Producer, Multi-Consumer (SPMC).
- Worker thread executes `push_bottom` and `pop_bottom` in LIFO order for L1/L2 cache locality.
- Peer threads execute `steal_top` in FIFO order.
- In-place storage eliminates heap allocation per push.

### 4. Work-Stealing Thread Pool (`thread_pool.hpp` / `.cpp`)
- Per-worker Chase-Lev local deques.
- Global MPMC injection queue for external task submissions.
- Hardware core affinity pinning via platform thread abstraction.
- Two-tier adaptive backoff (spin then sleep on wake event) preventing idle CPU burn.

### 5. Multi-Lane Priority Task Scheduler (`task_scheduler.hpp` / `.cpp`)
- Prioritized execution lanes: `RealTime` (sub-millisecond order/risk execution), `Normal`, `Low`.
- Time-ordered min-heap for delayed/deferred tasks.
- Non-blocking batch dispatching outside lock boundaries.

### 6. LMAX Disruptor (`disruptor.hpp`) — *NEW*
- Pre-allocated zero-allocation circular ring buffer.
- Multi-producer claim sequence + publish sequence barriers.
- Tailored for high-throughput market data feed parsing and matching engine pipelining.

### 7. Go Channels Bridge (`channels.go`)
- Generic `RingChannel[T]` lock-free ring buffer for Go microservices and bridge processes.

### 8. Rust Actor Runtime (`actor_runtime.rs`)
- Lock-free MPSC node-based mailbox with atomic CAS state machine (`IDLE` -> `SCHEDULED` -> `RUNNING`).
- Cooperative batch scheduling with `send_batch` C-ABI exports.
