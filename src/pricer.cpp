#include "pricer.hpp"

#include <cmath>

namespace qe {

// The standard normal CDF via erfc.
//
// erfc rather than 0.5*(1+erf(x/sqrt2)): for large negative x, erf approaches
// -1 and the expression 1+erf becomes catastrophic cancellation — you subtract
// two nearly equal numbers and keep the noise. erfc computes the small tail
// directly and stays accurate where deep out-of-the-money options live.
double norm_cdf(double x) noexcept {
    // 1/sqrt(2), as a literal so no runtime division appears in the hot path.
    constexpr double kInvSqrt2 = 0.707106781186547524400844362104849039;
    return 0.5 * std::erfc(-x * kInvSqrt2);
}

Greeks price(const Contract& c, const MarketState& m) noexcept {
    // An expired or zero-vol option has no optionality left: it is worth its
    // intrinsic value and nothing more. This is not merely an edge case guard.
    //
    // Without it, sqrt(t) is zero, d1 divides by zero, and the result is inf
    // or nan. Worse for our purposes: as t becomes very small but nonzero, the
    // intermediate values become DENORMAL — floating point numbers below the
    // smallest normal magnitude. On most x86 hardware an operation on a
    // denormal takes a microcode assist costing on the order of 100 cycles
    // instead of 4.
    //
    // That is invisible in an average and enormous in a p99.9: options near
    // expiry are exactly the ones being quoted most actively on expiration
    // day. Step 6 measures this directly.
    const double t = c.time_to_expiry;
    const double v = m.vol;

    if (t <= 1e-10 || v <= 1e-10) {
        const double intrinsic =
            (c.right == Right::Call) ? std::fmax(m.spot - c.strike, 0.0)
                                     : std::fmax(c.strike - m.spot, 0.0);
        return Greeks{intrinsic, 0.0, 0.0, 0.0, 0.0};
    }

    const double sqrt_t = std::sqrt(t);
    const double vol_sqrt_t = v * sqrt_t;

    const double log_moneyness = std::log(m.spot / c.strike);
    const double d1 =
        (log_moneyness + (m.rate + 0.5 * v * v) * t) / vol_sqrt_t;
    const double d2 = d1 - vol_sqrt_t;

    const double discount = std::exp(-m.rate * t);
    const double nd1 = norm_cdf(d1);
    const double nd2 = norm_cdf(d2);

    // The standard normal PDF at d1, shared by gamma and vega.
    constexpr double kInvSqrt2Pi = 0.398942280401432677939946059934381868;
    const double pdf_d1 = kInvSqrt2Pi * std::exp(-0.5 * d1 * d1);

    Greeks g{};

    if (c.right == Right::Call) {
        g.theo = m.spot * nd1 - c.strike * discount * nd2;
        g.delta = nd1;
        g.theta = -(m.spot * pdf_d1 * v) / (2.0 * sqrt_t) -
                  m.rate * c.strike * discount * nd2;
    } else {
        // Put values via put-call parity rather than a second closed form.
        // One expression to get wrong instead of two, and the compiler folds
        // the shared subexpressions either way.
        g.theo = c.strike * discount * (1.0 - nd2) - m.spot * (1.0 - nd1);
        g.delta = nd1 - 1.0;
        g.theta = -(m.spot * pdf_d1 * v) / (2.0 * sqrt_t) +
                  m.rate * c.strike * discount * (1.0 - nd2);
    }

    // Gamma and vega are identical for calls and puts — the optionality is the
    // same, only the payoff direction differs.
    g.gamma = pdf_d1 / (m.spot * vol_sqrt_t);
    g.vega = m.spot * pdf_d1 * sqrt_t;

    return g;
}

}  // namespace qe
