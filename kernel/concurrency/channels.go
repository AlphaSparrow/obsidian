// Package concurrency provides ultra-low-latency concurrency primitives and lock-free channels
// tailored for high-frequency financial data streaming and kernel message passing.
// Created by Aayush Sakariya on 07-09-2026.
package concurrency

import (
	"errors"
	"runtime"
	"sync/atomic"
)

var (
	ErrChannelFull   = errors.New("channel is full")
	ErrChannelEmpty  = errors.New("channel is empty")
	ErrChannelClosed = errors.New("channel is closed")
)

const CacheLineSize = 64

// RingChannel is a bounded lock-free ring buffer channel supporting high-throughput
// zero-allocation message passing between concurrent goroutines.
type RingChannel[T any] struct {
	_pad0     [CacheLineSize]byte
	capacity  uint64
	mask      uint64
	_pad1     [CacheLineSize]byte
	writePos  atomic.Uint64
	_pad2     [CacheLineSize]byte
	readPos   atomic.Uint64
	_pad3     [CacheLineSize]byte
	closed    atomic.Bool
	slots     []slot[T]
}

type slot[T any] struct {
	sequence atomic.Uint64
	value    T
}

// NewRingChannel creates a new lock-free ring buffer channel with a capacity rounded to the next power of 2.
func NewRingChannel[T any](capacity uint64) *RingChannel[T] {
	cap := nextPowerOfTwo(capacity)
	if cap < 2 {
		cap = 2
	}

	rc := &RingChannel[T]{
		capacity: cap,
		mask:     cap - 1,
		slots:    make([]slot[T], cap),
	}

	for i := uint64(0); i < cap; i++ {
		rc.slots[i].sequence.Store(i)
	}

	return rc
}

// TrySend attempts to send an item without blocking. Returns ErrChannelFull or ErrChannelClosed if unsuccessful.
func (rc *RingChannel[T]) TrySend(val T) error {
	if rc.closed.Load() {
		return ErrChannelClosed
	}

	pos := rc.writePos.Load()
	for {
		if rc.closed.Load() {
			return ErrChannelClosed
		}

		s := &rc.slots[pos&rc.mask]
		seq := s.sequence.Load()
		dif := int64(seq) - int64(pos)

		if dif == 0 {
			if rc.writePos.CompareAndSwap(pos, pos+1) {
				s.value = val
				s.sequence.Store(pos + 1)
				return nil
			}
		} else if dif < 0 {
			return ErrChannelFull
		} else {
			pos = rc.writePos.Load()
		}
	}
}

// Send sends an item, spinning and yielding until space is available.
func (rc *RingChannel[T]) Send(val T) error {
	spins := 0
	for {
		err := rc.TrySend(val)
		if err == nil {
			return nil
		}
		if err == ErrChannelClosed {
			return ErrChannelClosed
		}

		spins++
		if spins < 32 {
			// CPU pause / yield hint
			continue
		}
		runtime.Gosched()
	}
}

// TryReceive attempts to receive an item without blocking. Returns ErrChannelEmpty or ErrChannelClosed.
func (rc *RingChannel[T]) TryReceive() (T, error) {
	pos := rc.readPos.Load()
	for {
		s := &rc.slots[pos&rc.mask]
		seq := s.sequence.Load()
		dif := int64(seq) - int64(pos+1)

		if dif == 0 {
			if rc.readPos.CompareAndSwap(pos, pos+1) {
				val := s.value
				var zero T
				s.value = zero // Avoid memory leak
				s.sequence.Store(pos + rc.mask + 1)
				return val, nil
			}
		} else if dif < 0 {
			if rc.closed.Load() && rc.IsEmpty() {
				var zero T
				return zero, ErrChannelClosed
			}
			var zero T
			return zero, ErrChannelEmpty
		} else {
			pos = rc.readPos.Load()
		}
	}
}

// Receive receives an item, spinning and yielding until an item is available.
func (rc *RingChannel[T]) Receive() (T, error) {
	spins := 0
	for {
		val, err := rc.TryReceive()
		if err == nil {
			return val, nil
		}
		if err == ErrChannelClosed {
			var zero T
			return zero, ErrChannelClosed
		}

		spins++
		if spins < 32 {
			continue
		}
		runtime.Gosched()
	}
}

// Close closes the ring channel. Subsequent sends will fail.
func (rc *RingChannel[T]) Close() {
	rc.closed.Store(true)
}

// IsEmpty returns true if there are no readable elements.
func (rc *RingChannel[T]) IsEmpty() bool {
	return rc.writePos.Load() <= rc.readPos.Load()
}

// Len returns the approximate number of buffered elements.
func (rc *RingChannel[T]) Len() uint64 {
	w := rc.writePos.Load()
	r := rc.readPos.Load()
	if w >= r {
		return w - r
	}
	return 0
}

// Cap returns the channel capacity.
func (rc *RingChannel[T]) Cap() uint64 {
	return rc.capacity
}

func nextPowerOfTwo(n uint64) uint64 {
	if n <= 1 {
		return 1
	}
	n--
	n |= n >> 1
	n |= n >> 2
	n |= n >> 4
	n |= n >> 8
	n |= n >> 16
	n |= n >> 32
	return n + 1
}
