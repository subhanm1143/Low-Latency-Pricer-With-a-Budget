#pragma once

#include <cstdint>

#include "ring_buffer.hpp"

namespace qe {

struct RawMessage {
    std::uint32_t symbol_id;
    std::uint64_t sequence;
    double price;
};

// A normalized tick, carrying the ingress timestamp that the latency
// histogram in step 6 subtracts from the publish time.
struct Tick {
    std::uint64_t ingress_ns;
    std::uint64_t sequence;
    std::uint32_t symbol_id;
    double price;
};

struct FeedStats {
    std::uint64_t drops{0};
    std::uint64_t gaps{0};
};

class FeedHandler {
public:
    static constexpr std::size_t kQueueCapacity = 4096;
    using Queue = RingBuffer<Tick, kQueueCapacity>;

    explicit FeedHandler(Queue& q) noexcept : queue_(q) {}

    void on_message(const RawMessage& raw) noexcept;

    const FeedStats& stats() const noexcept { return stats_; }

private:
    Queue& queue_;
    FeedStats stats_{};
    std::uint64_t expected_sequence_{0};
};

}  // namespace qe
