//! obsidian_actor_runtime — Lock-Free High-Performance Actor Runtime
//! -----------------------------------------------------------------------------
//! Optimized for sub-millisecond quantitative finance pipelines.
//!
//! Algorithmic Architecture (DSA & Systems Foundations):
//!   1. Lock-Free MPSC Mailbox:
//!      Producers append messages using an atomic pointer swap (wait-free O(1)).
//!      The assigned actor consumer drains messages sequentially in O(1).
//!   2. Cooperative Batch Scheduling:
//!      Actors process up to MAX_BATCH (32) messages per activation, preventing
//!      single-actor starvation while maximizing instruction cache warmth.
//!   3. Atomic State Machine:
//!      State transitions (IDLE -> SCHEDULED -> RUNNING) use atomic CAS
//!      to guarantee an actor is enqueued in the ready queue at most once.
//!   4. Hardware Cache Symbiosis:
//!      Mailbox pointers and actor metadata are cache-line aligned (64 bytes)
//!      to eliminate false-sharing and core invalidation storms.
//!   5. Zero-Overhead C-ABI FFI:
//!      Clean extern "C" functions for seamless integration into C++ kernel.
//! -----------------------------------------------------------------------------

#![no_std]

extern crate alloc;

use alloc::boxed::Box;
use alloc::vec::Vec;
use core::ffi::c_void;
use core::ptr;
use core::sync::atomic::{AtomicBool, AtomicPtr, AtomicU32, AtomicU64, AtomicUsize, Ordering};

// ── Constants & Status Codes ──────────────────────────────────────────────────

pub const ACTOR_SUCCESS: i32 = 0;
pub const ACTOR_ERR_NULL_PTR: i32 = -1;
pub const ACTOR_ERR_NOT_FOUND: i32 = -2;
pub const ACTOR_ERR_QUEUE_FULL: i32 = -3;
pub const ACTOR_ERR_SHUTDOWN: i32 = -4;

const CACHE_LINE_SIZE: usize = 64;
const MAX_ACTORS: usize = 4096;
const DEFAULT_BATCH_SIZE: usize = 32;

// Actor State Constants
const STATE_IDLE: u32 = 0;
const STATE_SCHEDULED: u32 = 1;
const STATE_RUNNING: u32 = 2;

// ── C-ABI Compatible Message & Handler ────────────────────────────────────────

/// C-ABI compatible message structure passed to actor handlers.
#[repr(C)]
pub struct ActorMessage {
    pub type_id: u32,
    pub payload_len: u32,
    pub payload: *const u8,
}

/// Function pointer signature for C-compatible actor message handlers.
pub type ActorHandlerFn = extern "C" fn(context: *mut c_void, msg: *const ActorMessage) -> i32;

// ── Lock-Free MPSC Node-Based Mailbox ─────────────────────────────────────────

struct MailboxNode {
    next: AtomicPtr<MailboxNode>,
    message: ActorMessage,
    payload_buf: Option<Vec<u8>>,
}

impl MailboxNode {
    fn new(msg: &ActorMessage) -> *mut Self {
        let payload_buf = if !msg.payload.is_null() && msg.payload_len > 0 {
            let mut vec = Vec::with_capacity(msg.payload_len as usize);
            unsafe {
                ptr::copy_nonoverlapping(msg.payload, vec.as_mut_ptr(), msg.payload_len as usize);
                vec.set_len(msg.payload_len as usize);
            }
            Some(vec)
        } else {
            None
        };

        let payload_ptr = payload_buf
            .as_ref()
            .map_or(ptr::null(), |v| v.as_ptr());

        let node = Box::new(Self {
            next: AtomicPtr::new(ptr::null_mut()),
            message: ActorMessage {
                type_id: msg.type_id,
                payload_len: msg.payload_len,
                payload: payload_ptr,
            },
            payload_buf,
        });

        Box::into_raw(node)
    }

    fn stub() -> *mut Self {
        let node = Box::new(Self {
            next: AtomicPtr::new(ptr::null_mut()),
            message: ActorMessage {
                type_id: 0,
                payload_len: 0,
                payload: ptr::null(),
            },
            payload_buf: None,
        });
        Box::into_raw(node)
    }
}

/// Intrusive Multi-Producer Single-Consumer (MPSC) Lock-Free Mailbox.
///
/// Algorithmic Guarantees:
///   - Producer push: Atomic pointer swap with Acquire-Release ordering (O(1) wait-free).
///   - Consumer pop: Single-consumer pointer traversal (O(1) lock-free).
#[repr(align(64))]
struct MpscMailbox {
    head: AtomicPtr<MailboxNode>,
    tail: *mut MailboxNode,
}

impl MpscMailbox {
    fn new() -> Self {
        let stub = MailboxNode::stub();
        Self {
            head: AtomicPtr::new(stub),
            tail: stub,
        }
    }

    /// Enqueues a message into the mailbox.
    /// Time Complexity: O(1) wait-free.
    fn push(&self, msg: &ActorMessage) {
        let node = MailboxNode::new(msg);
        // Swap head to point to new node; old head will link to it
        let prev = self.head.swap(node, Ordering::AcqRel);
        unsafe {
            (*prev).next.store(node, Ordering::Release);
        }
    }

    /// Dequeues the next message from the mailbox.
    /// Time Complexity: O(1). Only called by the single actor executor thread.
    fn pop(&mut self) -> Option<Box<MailboxNode>> {
        let tail = self.tail;
        let next = unsafe { (*tail).next.load(Ordering::Acquire) };

        if !next.is_null() {
            self.tail = next;
            // Free the previous stub node and return the current node
            unsafe {
                let _ = Box::from_raw(tail);
            }
            let node_box = unsafe { Box::from_raw(next) };
            Some(node_box)
        } else {
            None
        }
    }

    fn is_empty(&self) -> bool {
        let tail = self.tail;
        let next = unsafe { (*tail).next.load(Ordering::Relaxed) };
        next.is_null()
    }
}

impl Drop for MpscMailbox {
    fn drop(&mut self) {
        while let Some(_) = self.pop() {}
        unsafe {
            if !self.tail.is_null() {
                let _ = Box::from_raw(self.tail);
                self.tail = ptr::null_mut();
            }
        }
    }
}

// ── Actor Control Block ───────────────────────────────────────────────────────

/// Represents an isolated stateful actor instance.
#[repr(align(64))]
struct ActorControlBlock {
    id: u64,
    state: AtomicU32,
    mailbox: MpscMailbox,
    handler: ActorHandlerFn,
    context: *mut c_void,
    messages_processed: AtomicU64,
}

impl ActorControlBlock {
    fn new(id: u64, handler: ActorHandlerFn, context: *mut c_void) -> Self {
        Self {
            id,
            state: AtomicU32::new(STATE_IDLE),
            mailbox: MpscMailbox::new(),
            handler,
            context,
            messages_processed: AtomicU64::new(0),
        }
    }

    /// Tries to transition state from IDLE to SCHEDULED.
    /// Returns true if the transition succeeded and the actor must be queued.
    fn try_schedule(&self) -> bool {
        self.state
            .compare_exchange(
                STATE_IDLE,
                STATE_SCHEDULED,
                Ordering::AcqRel,
                Ordering::Relaxed,
            )
            .is_ok()
    }
}

// ── Bounded Lock-Free Ready Queue ─────────────────────────────────────────────

/// High-throughput bounded lock-free ready queue for runnable actors.
#[repr(align(64))]
struct ReadyQueue {
    buffer: [*mut ActorControlBlock; MAX_ACTORS],
    head: AtomicUsize,
    tail: AtomicUsize,
}

impl ReadyQueue {
    fn new() -> Self {
        Self {
            buffer: [ptr::null_mut(); MAX_ACTORS],
            head: AtomicUsize::new(0),
            tail: AtomicUsize::new(0),
        }
    }

    fn push(&mut self, actor: *mut ActorControlBlock) -> bool {
        let head = self.head.load(Ordering::Relaxed);
        let tail = self.tail.load(Ordering::Acquire);

        if head.wrapping_sub(tail) >= MAX_ACTORS {
            return false;
        }

        self.buffer[head % MAX_ACTORS] = actor;
        self.head.store(head.wrapping_add(1), Ordering::Release);
        true
    }

    fn pop(&mut self) -> Option<*mut ActorControlBlock> {
        let tail = self.tail.load(Ordering::Relaxed);
        let head = self.head.load(Ordering::Acquire);

        if tail == head {
            return None;
        }

        let actor = self.buffer[tail % MAX_ACTORS];
        self.tail.store(tail.wrapping_add(1), Ordering::Release);
        Some(actor)
    }

    fn len(&self) -> usize {
        let head = self.head.load(Ordering::Relaxed);
        let tail = self.tail.load(Ordering::Relaxed);
        head.wrapping_sub(tail)
    }
}

// ── Actor Runtime ─────────────────────────────────────────────────────────────

/// Core Actor Runtime managing actor lifecycle, mailboxes, and execution scheduling.
pub struct ActorRuntime {
    actors: Vec<Option<Box<ActorControlBlock>>>,
    ready_queue: ReadyQueue,
    next_actor_id: AtomicU64,
    active_actors: AtomicUsize,
    is_running: AtomicBool,
}

impl ActorRuntime {
    pub fn new() -> Self {
        let mut actors = Vec::with_capacity(MAX_ACTORS);
        for _ in 0..MAX_ACTORS {
            actors.push(None);
        }

        Self {
            actors,
            ready_queue: ReadyQueue::new(),
            next_actor_id: AtomicU64::new(1),
            active_actors: AtomicUsize::new(0),
            is_running: AtomicBool::new(true),
        }
    }

    /// Spawns a new actor and returns its unique 64-bit ActorId.
    /// Time Complexity: O(1).
    pub fn spawn(&mut self, handler: ActorHandlerFn, context: *mut c_void) -> u64 {
        let id = self.next_actor_id.fetch_add(1, Ordering::Relaxed);
        let slot = (id as usize) % MAX_ACTORS;

        let acb = Box::new(ActorControlBlock::new(id, handler, context));
        self.actors[slot] = Some(acb);
        self.active_actors.fetch_add(1, Ordering::Release);

        id
    }

    /// Sends a message to a specific actor.
    /// Time Complexity: O(1) wait-free append + O(1) state CAS.
    pub fn send(&mut self, actor_id: u64, msg: &ActorMessage) -> i32 {
        if !self.is_running.load(Ordering::Relaxed) {
            return ACTOR_ERR_SHUTDOWN;
        }

        let slot = (actor_id as usize) % MAX_ACTORS;
        if let Some(ref actor) = self.actors[slot] {
            if actor.id != actor_id {
                return ACTOR_ERR_NOT_FOUND;
            }

            // Append to actor's lock-free mailbox
            actor.mailbox.push(msg);

            // If actor was IDLE, transition to SCHEDULED and place in ready queue
            if actor.try_schedule() {
                let raw_ptr = actor.as_ref() as *const ActorControlBlock as *mut ActorControlBlock;
                if !self.ready_queue.push(raw_ptr) {
                    return ACTOR_ERR_QUEUE_FULL;
                }
            }

            ACTOR_SUCCESS
        } else {
            ACTOR_ERR_NOT_FOUND
        }
    }

    /// Polls and processes pending messages across scheduled actors up to `max_messages`.
    /// Enables cooperative multitasking and fine-grained latency budgeting.
    /// Time Complexity: O(K) where K is number of processed messages.
    pub fn poll(&mut self, max_messages: usize) -> usize {
        let mut processed_total = 0;

        while processed_total < max_messages {
            let actor_ptr = match self.ready_queue.pop() {
                Some(ptr) if !ptr.is_null() => ptr,
                _ => break,
            };

            let actor = unsafe { &mut *actor_ptr };

            // Transition from SCHEDULED to RUNNING
            actor.state.store(STATE_RUNNING, Ordering::Release);

            // Process a cooperative batch of messages
            let mut batch_count = 0;
            while batch_count < DEFAULT_BATCH_SIZE && processed_total < max_messages {
                if let Some(node) = actor.mailbox.pop() {
                    (actor.handler)(actor.context, &node.message);
                    actor.messages_processed.fetch_add(1, Ordering::Relaxed);
                    batch_count += 1;
                    processed_total += 1;
                } else {
                    break;
                }
            }

            // Check if more messages arrived while processing
            if !actor.mailbox.is_empty() {
                actor.state.store(STATE_SCHEDULED, Ordering::Release);
                self.ready_queue.push(actor_ptr);
            } else {
                // Return to IDLE state
                actor.state.store(STATE_IDLE, Ordering::Release);

                // Double check to eliminate race condition where a message arrived right before IDLE CAS
                if !actor.mailbox.is_empty() && actor.try_schedule() {
                    self.ready_queue.push(actor_ptr);
                }
            }
        }

    /// Dispatches a batch of messages to an actor with minimal atomic contention.
    pub fn send_batch(&mut self, actor_id: u64, messages: &[ActorMessage]) -> usize {
        let mut count = 0;
        for msg in messages {
            if self.send(actor_id, msg) == ACTOR_SUCCESS {
                count += 1;
            } else {
                break;
            }
        }
        count
    }

    pub fn total_active(&self) -> usize {
        self.active_actors.load(Ordering::Relaxed)
    }

    pub fn shutdown(&mut self) {
        self.is_running.store(false, Ordering::Release);
    }
}

// ── C-ABI FFI Exports ─────────────────────────────────────────────────────────

/// Initializes a new Obsidian Actor Runtime instance.
#[no_mangle]
pub unsafe extern "C" fn obsidian_actor_runtime_create() -> *mut ActorRuntime {
    let runtime = Box::new(ActorRuntime::new());
    Box::into_raw(runtime)
}

/// Destroys an existing Obsidian Actor Runtime instance and reclaims all resources.
#[no_mangle]
pub unsafe extern "C" fn obsidian_actor_runtime_destroy(runtime: *mut ActorRuntime) {
    if !runtime.is_null() {
        let mut rt = Box::from_raw(runtime);
        rt.shutdown();
    }
}

/// Spawns an actor in the runtime, returning its 64-bit Actor ID.
#[no_mangle]
pub unsafe extern "C" fn obsidian_actor_spawn(
    runtime: *mut ActorRuntime,
    handler: ActorHandlerFn,
    context: *mut c_void,
) -> u64 {
    if runtime.is_null() {
        return 0;
    }
    let rt = &mut *runtime;
    rt.spawn(handler, context)
}

/// Dispatches a message to the specified actor in O(1) wait-free time.
#[no_mangle]
pub unsafe extern "C" fn obsidian_actor_send(
    runtime: *mut ActorRuntime,
    actor_id: u64,
    msg: *const ActorMessage,
) -> i32 {
    if runtime.is_null() || msg.is_null() {
        return ACTOR_ERR_NULL_PTR;
    }
    let rt = &mut *runtime;
    rt.send(actor_id, &*msg)
}

/// Dispatches a batch of messages to an actor.
#[no_mangle]
pub unsafe extern "C" fn obsidian_actor_send_batch(
    runtime: *mut ActorRuntime,
    actor_id: u64,
    msgs: *const ActorMessage,
    count: usize,
) -> usize {
    if runtime.is_null() || msgs.is_null() || count == 0 {
        return 0;
    }
    let slice = core::slice::from_raw_parts(msgs, count);
    let rt = &mut *runtime;
    rt.send_batch(actor_id, slice)
}

/// Polls and runs scheduled actor tasks up to `max_messages`. Returns count of messages executed.
#[no_mangle]
pub unsafe extern "C" fn obsidian_actor_poll(
    runtime: *mut ActorRuntime,
    max_messages: usize,
) -> usize {
    if runtime.is_null() {
        return 0;
    }
    let rt = &mut *runtime;
    rt.poll(max_messages)
}
