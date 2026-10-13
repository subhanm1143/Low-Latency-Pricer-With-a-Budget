#pragma once

#include <cstdint>

namespace qe {

enum class Right : std::uint8_t { Call, Put };

// One option contract. Deliberately a plain aggregate of scalars: this struct
// is copied into the pricing thread on every tick, and anything with a
// destructor, a virtual table, or a heap pointer would show up in the tail of
// the latency histogram.
//
// 32 bytes, so two contracts share a cache line and a book of them walks
// memory in a straight line.
struct Contract {
    std::uint32_t id;
    double strike;
    double time_to_expiry;  // in years
    Right right;
};

// The pricing output. Theoretical value plus the sensitivities a trader quotes
// off. Delta and vega are here because they drive the spread the publisher
// puts around theo, not because they are decoration.
struct Greeks {
    double theo;
    double delta;  // d(theo) / d(spot)
    double gamma;  // d(delta) / d(spot)
    double vega;   // d(theo) / d(vol)
    double theta;  // d(theo) / d(time)
};

// Everything about the world that is not the contract itself. Passed by const
// reference and updated once per tick rather than per contract, because the
// whole book reprices against one spot.
struct MarketState {
    double spot;
    double rate;
    double vol;
};

}  // namespace qe
