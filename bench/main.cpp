#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "engine.hpp"
#include "histogram.hpp"

namespace {

// A latency gate has to survive run-to-run variance or it gets disabled within
// a week. Three defences, in order of importance:
//
//   1. WARMUP. The first thousands of ticks populate caches and let the branch
//      predictor learn the loop. Including them measures startup, not steady
//      state, and startup varies far more between runs.
//
//   2. REPEATED TRIALS, TAKE THE BEST. A single trial can be ruined by another
//      process, an interrupt, or a migration between cores. The best trial is
//      the one with the least external interference — closest to what the code
//      is actually capable of. Taking the median instead would bake in
//      whatever noise the CI machine happened to have.
//
//   3. A BUDGET WITH HEADROOM. The gate fails at 2500ns while the measured
//      p99 sits near 1500ns. A gate set at the current value fails on noise
//      the first week and is switched off; one set with headroom fails only
//      on a real regression.
constexpr std::size_t kWarmupTicks = 100'000;
constexpr int kTrials = 5;

}  // namespace

int main(int argc, char** argv) {
    std::uint64_t budget_p99_ns = 2500;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--budget-p99-ns" && i + 1 < argc) {
            budget_p99_ns = std::strtoull(argv[++i], nullptr, 10);
        }
    }

    std::uint64_t best_p99 = UINT64_MAX;
    std::uint64_t best_p999 = UINT64_MAX;

    for (int trial = 0; trial < kTrials; ++trial) {
        qe::Engine engine;
        engine.warmup(kWarmupTicks);

        qe::Histogram h = engine.run_benchmark();

        const std::uint64_t p99 = h.percentile(0.99);
        if (p99 < best_p99) {
            best_p99 = p99;
            best_p999 = h.percentile(0.999);
        }

        std::printf("trial %d: p50=%llu p99=%llu p99.9=%llu\n", trial,
                    (unsigned long long)h.percentile(0.50),
                    (unsigned long long)p99,
                    (unsigned long long)h.percentile(0.999));
    }

    std::printf("\nbest p99:   %llu ns (budget %llu)\n",
                (unsigned long long)best_p99,
                (unsigned long long)budget_p99_ns);
    std::printf("best p99.9: %llu ns\n", (unsigned long long)best_p999);

    // Fail the build on a p99 regression. Exit code is the entire interface a
    // CI system needs — no parsing of output, no separate report to publish.
    if (best_p99 > budget_p99_ns) {
        std::printf("\nFAIL: p99 %llu ns exceeds budget %llu ns\n",
                    (unsigned long long)best_p99,
                    (unsigned long long)budget_p99_ns);
        return 1;
    }

    std::printf("\nPASS\n");
    return 0;
}
