#pragma once

#include <atomic>
#include <cstddef>
#include <new>

namespace qe {

// A single-producer, single-consumer lock-free ring buffer.
//
// SPSC specifically. That constraint is what makes this ~40 lines instead of a
// research paper: with exactly one writer and one reader, each index has a
// single owner, and no compare-and-exchange is needed anywhere.
//
// Capacity must be a power of two so the wrap is a mask rather than a modulo.
// An integer division costs 20-40 cycles; the mask costs one.
template <typename T, std::size_t Capacity>
class RingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two");

public:
    // Returns false when full. The caller — the feed handler — must decide
    // what to do about that, and for market data the answer is to DROP, never
    // to block. See the discussion in step 4.
    bool try_push(const T& item) noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t next = head + 1;

        // Acquire on the consumer's index: everything the consumer did before
        // publishing this tail value must be visible to us before we decide
        // the slot is free to overwrite.
        if (next - tail_.load(std::memory_order_acquire) > Capacity) {
            return false;
        }

        buffer_[head & (Capacity - 1)] = item;

        // Release: the write to buffer_ above must be visible to the consumer
        // BEFORE it sees the new head. Without this the consumer can observe an
        // incremented index and then read a slot the producer has not finished
        // writing — a torn read that shows up as one impossible price in ten
        // million ticks and takes a week to find.
        head_.store(next, std::memory_order_release);
        return true;
    }

    bool try_pop(T& out) noexcept {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);

        // Acquire, pairing with the producer's release above.
        if (tail == head_.load(std::memory_order_acquire)) {
            return false;  // empty
        }

        out = buffer_[tail & (Capacity - 1)];
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

private:
    // Hardware destructive interference size — the granularity at which two
    // objects will not share a cache line. Typically 64 bytes on x86.
    static constexpr std::size_t kCacheLine = 64;

    // head_ and tail_ are padded onto SEPARATE cache lines.
    //
    // Without this padding the two atomics sit adjacent in memory and share a
    // line. The producer writes head_, the consumer writes tail_, and although
    // they never touch the same VARIABLE, the cache coherence protocol works
    // at line granularity: every write by one thread invalidates the line in
    // the other's cache. Two threads ping-pong a cache line that neither
    // needed from the other.
    //
    // This is false sharing, and it is worth 3-5x on this structure. Step 6
    // measures it with the padding removed.
    alignas(kCacheLine) std::atomic<std::size_t> head_{0};
    alignas(kCacheLine) std::atomic<std::size_t> tail_{0};
    alignas(kCacheLine) T buffer_[Capacity];
};

}  // namespace qe
