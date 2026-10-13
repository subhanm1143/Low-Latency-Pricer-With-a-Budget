#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

#include "pricer.hpp"
#include "ring_buffer.hpp"

namespace {

int failures = 0;

void check(bool cond, const char* what) {
    if (!cond) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

void check_near(double a, double b, double tol, const char* what) {
    if (std::fabs(a - b) > tol) {
        std::printf("FAIL: %s (%.10f vs %.10f)\n", what, a, b);
        ++failures;
    }
}

// Known values, computed independently. These come from a published reference
// implementation, NOT from running this pricer and recording what it printed —
// which would assert only that the code does what the code does.
void test_pricer_known_values() {
    // S=100, K=100, t=1, r=0.05, vol=0.20 → call 10.4506, put 5.5735.
    const qe::MarketState m{100.0, 0.05, 0.20};
    const qe::Contract call{1, 100.0, 1.0, qe::Right::Call};
    const qe::Contract put{2, 100.0, 1.0, qe::Right::Put};

    check_near(qe::price(call, m).theo, 10.450583572, 1e-6, "ATM call value");
    check_near(qe::price(put, m).theo, 5.573526022, 1e-6, "ATM put value");

    // Delta of an at-the-money call with these parameters.
    check_near(qe::price(call, m).delta, 0.636830651, 1e-6, "ATM call delta");
}

// A property test: put-call parity must hold for ANY inputs, so it catches
// errors a handful of known values would miss.
//
//     C - P = S - K*exp(-r*t)
void test_put_call_parity() {
    for (double spot : {80.0, 95.0, 100.0, 105.0, 130.0}) {
        for (double vol : {0.10, 0.25, 0.60}) {
            for (double t : {0.05, 0.5, 2.0}) {
                const qe::MarketState m{spot, 0.03, vol};
                const qe::Contract c{1, 100.0, t, qe::Right::Call};
                const qe::Contract p{2, 100.0, t, qe::Right::Put};

                const double lhs = qe::price(c, m).theo - qe::price(p, m).theo;
                const double rhs = spot - 100.0 * std::exp(-0.03 * t);

                check_near(lhs, rhs, 1e-9, "put-call parity");
            }
        }
    }
}

// The denormal guard: a near-expiry option must return intrinsic and must not
// produce nan. Both halves matter — nan would propagate silently.
void test_expiry_guard() {
    const qe::MarketState m{105.0, 0.05, 0.20};
    const qe::Contract expiring{1, 100.0, 0.0, qe::Right::Call};

    const qe::Greeks g = qe::price(expiring, m);

    check_near(g.theo, 5.0, 1e-12, "expired call is intrinsic");
    check(!std::isnan(g.theo), "expired call is not nan");
    check(!std::isnan(g.delta), "expired delta is not nan");
}

// Stress the ring buffer across two real threads.
//
// What this CAN establish: that no item is lost, duplicated, or reordered
// across many millions of operations under real contention.
//
// What it CANNOT establish: the absence of a memory ordering bug. A missing
// release store may be harmless on x86 — whose memory model is strong enough
// to hide it — and break on ARM. Passing here is evidence, not proof. The
// proof is reading the ordering annotations and reasoning about them, which is
// why the ring buffer's comments explain each one.
void test_ring_buffer_stress() {
    constexpr std::size_t kItems = 4'000'000;
    qe::RingBuffer<std::uint64_t, 1024> ring;

    std::atomic<bool> producer_done{false};
    std::uint64_t consumed = 0;
    std::uint64_t expected_next = 0;
    bool order_violation = false;

    std::thread producer([&] {
        for (std::uint64_t i = 0; i < kItems;) {
            if (ring.try_push(i)) {
                ++i;
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    std::uint64_t value = 0;
    while (consumed < kItems) {
        if (ring.try_pop(value)) {
            if (value != expected_next) {
                order_violation = true;
            }
            ++expected_next;
            ++consumed;
        } else if (producer_done.load(std::memory_order_acquire) &&
                   consumed < kItems) {
            continue;
        }
    }

    producer.join();

    check(!order_violation, "ring buffer preserved FIFO order");
    check(consumed == kItems, "ring buffer lost no items");
}

}  // namespace

int main() {
    test_pricer_known_values();
    test_put_call_parity();
    test_expiry_guard();
    test_ring_buffer_stress();

    if (failures == 0) {
        std::printf("all tests passed\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
