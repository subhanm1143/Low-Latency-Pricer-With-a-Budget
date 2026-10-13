#pragma once

#include "option.hpp"

namespace qe {

// Black-Scholes with greeks, in closed form.
//
// Closed form matters here for a reason beyond convenience: a binomial tree or
// a Monte Carlo path would price the same option to the same value, but with a
// runtime that depends on the number of steps or paths. This function's cost
// is fixed and known, which is what makes a latency budget possible at all.
//
// Marked noexcept because an exception in the pricing path is not a recoverable
// condition — the caller has no meaningful response to "this option could not
// be priced" other than to stop quoting it, and the check for that belongs
// before we get here.
Greeks price(const Contract& c, const MarketState& m) noexcept;

// The normal CDF, which is the only non-elementary piece of Black-Scholes.
// Exposed for the test suite to pin against known values.
double norm_cdf(double x) noexcept;

}  // namespace qe
