#include "engine.hpp"

#include <chrono>

#include "pricer.hpp"

namespace qe {

static inline std::uint64_t now_ns() noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

// The spread around theoretical value.
//
// A fixed spread would be simpler and wrong. The width has to reflect how much
// risk the quote is taking:
//
//   delta — how fast this quote goes stale when the underlying moves. A high
//           delta option needs a wider quote because the window in which it is
//           correct is shorter.
//   vega  — exposure to a volatility move, which cannot be hedged by trading
//           the underlying, so it is compensated in the spread instead.
//
// The constants here are placeholders for what a trading desk would calibrate.
// What matters is the shape: spread grows with risk.
static inline double spread_for(const Greeks& g, double base_bps,
                                double spot) noexcept {
    constexpr double kDeltaWeight = 0.5;
    constexpr double kVegaWeight = 0.02;

    const double base = spot * base_bps * 1e-4;
    const double delta_component = kDeltaWeight * std::abs(g.delta) * base;
    const double vega_component = kVegaWeight * std::abs(g.vega) * 1e-2;

    return base + delta_component + vega_component;
}

void Engine::run_once() noexcept {
    Tick tick{};
    if (!queue_.try_pop(tick)) {
        return;
    }

    market_.spot = tick.price;

    // Reprice only the contracts on this underlying.
    //
    // The book is grouped by symbol so this is a contiguous range rather than
    // a filtered walk of everything. A tick on one underlying must not cost
    // time proportional to the size of the whole book — otherwise latency
    // degrades as the business grows, which is the worst possible scaling
    // property for a system with a latency budget.
    const auto& range = book_.range_for(tick.symbol_id);

    for (std::size_t i = range.begin; i < range.end; ++i) {
        const Contract& c = book_.contracts[i];
        const Greeks g = price(c, market_);

        const double half = 0.5 * spread_for(g, base_spread_bps_, market_.spot);

        Quote q{};
        q.contract_id = c.id;
        q.bid = g.theo - half;
        q.ask = g.theo + half;
        q.ingress_ns = tick.ingress_ns;  // carried through for the histogram

        publisher_.publish(q);
    }

    // One timestamp for the whole batch, taken after the last publish. Every
    // quote produced by this tick shares the same tick-to-quote latency, which
    // is the honest reading: the last quote in the batch is the one that
    // determines when the book was fully refreshed.
    const std::uint64_t publish_ns = now_ns();
    histogram_.record(publish_ns - tick.ingress_ns);
}

}  // namespace qe
