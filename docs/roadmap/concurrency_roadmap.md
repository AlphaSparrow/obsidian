# Concurrency Subsystem Roadmap

## Completed Milestones
- [x] **TTAS SpinLock, TicketLock, & SeqLock**: Implemented in `atomic_utils.hpp`.
- [x] **Vyukov MPMC Queue**: Modular header-only template (`lockfree_queue.hpp`) with bulk operations and explicit instantiations in `lockfree_queue.cpp`.
- [x] **Zero-Allocation Chase-Lev Deque**: Fixed heap allocation bug in `work_stealing.cpp` and created template header `work_stealing.hpp`.
- [x] **True Work-Stealing Thread Pool**: Built in `thread_pool.hpp` / `.cpp` with thread affinity, local deques, global injection queue, and idle event notification.
- [x] **Multi-Lane Task Scheduler**: Latency-budgeted priority lanes and delayed task execution in `task_scheduler.hpp` / `.cpp`.
- [x] **New Component: LMAX Disruptor**: Created `disruptor.hpp` with sequence barriers for quantitative market data pipelines.
- [x] **Go Concurrency**: Implemented `RingChannel[T]` in `channels.go`.
- [x] **Rust Actor Runtime**: Enhanced `actor_runtime.rs` with batch sending and C-ABI exports.

## Next Milestones (For Later)
- [ ] **Lock-Free Epoch-Based Memory Reclamation (EBRO)**: Add hazard pointers or epoch reclamation for dynamic lock-free node retirement.
- [ ] **Disruptor DSL & Consumer Chains**: Add dependency chaining (Ingest -> Journal -> Business Logic -> Egress).
- [ ] **SIMD Batch Dequeuing**: Vectorized packet processing on top of `LockFreeMPMCQueue`.
- [ ] **Comprehensive Benchmarks**: Add nanosecond benchmark suite measuring P50, P99, and P99.9 latency under high contention.
