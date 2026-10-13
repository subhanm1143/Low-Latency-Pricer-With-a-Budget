#include "feed.hpp"

#include <chrono>

namespace qe {

// Timestamp as early as possible: the first thing we do with a message, before
// parsing, before validation. Every nanosecond spent before this call is
// latency the histogram will never see, which would make our own measurements
// flattering and useless.
static inline std::uint64_t now_ns() noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

void FeedHandler::on_message(const RawMessage& raw) noexcept {
    const std::uint64_t ingress = now_ns();

    Tick tick{};
    tick.ingress_ns = ingress;
    tick.symbol_id = raw.symbol_id;
    tick.price = raw.price;
    tick.sequence = raw.sequence;

    // Sequence gap detection. A gap means we missed a message, which means our
    // view of the book is wrong until the next full refresh. We count it and
    // carry on rather than stopping, because a market maker with a stale book
    // that KNOWS it is stale can widen quotes; one that has halted is simply
    // not in the market.
    if (raw.sequence != expected_sequence_ && expected_sequence_ != 0) {
        ++stats_.gaps;
    }
    expected_sequence_ = raw.sequence + 1;

    // The policy that matters. When the pricing thread has fallen behind, we
    // DROP this tick rather than blocking.
    //
    // Blocking would be backpressure, and backpressure is correct for a queue
    // of work that must all eventually be done — orders, fills, settlement
    // records. Market data is not that. A tick is a statement about a price at
    // a moment; if we are too slow to price it, the correct action is to skip
    // to the newest one, because the stale tick's information is already
    // superseded by the tick behind it.
    //
    // Blocking here would be actively harmful: the feed thread would stop
    // draining the socket, the kernel buffer would fill, and we would fall
    // further behind while holding a tick nobody wants anymore.
    if (!queue_.try_push(tick)) {
        ++stats_.drops;
    }
}

}  // namespace qe
