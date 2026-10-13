#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

namespace qe {

// A fixed-bucket latency histogram.
//
// Fixed buckets, allocated once at construction, because recording a sample
// happens inside the measured path. A std::vector that might reallocate, or a
// sorted container, would make the act of measuring perturb the thing being
// measured.
//
// Buckets are logarithmic: fine resolution where the samples actually live
// (hundreds of nanoseconds to a few microseconds) and coarse in the tail,
// which is the opposite of what a linear histogram gives you.
class Histogram {
public:
    static constexpr std::size_t kBuckets = 64;

    void record(std::uint64_t ns) noexcept {
        buckets_[bucket_for(ns)]++;
        ++count_;
        max_ = std::max(max_, ns);
    }

    // The percentile the whole project is graded on.
    //
    // Not the mean. The mean of a latency distribution is dominated by the
    // common case, which is the case nobody is harmed by. A system can have a
    // 400ns mean and a 90us p99.9, and the p99.9 is where the money is lost:
    // those are the ticks that arrived during the market move that made the
    // system busy in the first place.
    std::uint64_t percentile(double p) const noexcept {
        const std::uint64_t target =
            static_cast<std::uint64_t>(static_cast<double>(count_) * p);
        std::uint64_t running = 0;
        for (std::size_t i = 0; i < kBuckets; ++i) {
            running += buckets_[i];
            if (running >= target) {
                return upper_bound_of(i);
            }
        }
        return max_;
    }

    std::uint64_t count() const noexcept { return count_; }
    std::uint64_t max() const noexcept { return max_; }

private:
    // Bucket index is the position of the highest set bit — a logarithmic
    // scale computed with one instruction rather than a loop or a log call.
    static std::size_t bucket_for(std::uint64_t ns) noexcept {
        if (ns == 0) return 0;
        return static_cast<std::size_t>(63 - __builtin_clzll(ns));
    }

    static std::uint64_t upper_bound_of(std::size_t bucket) noexcept {
        return (1ULL << (bucket + 1)) - 1;
    }

    std::array<std::uint64_t, kBuckets> buckets_{};
    std::uint64_t count_{0};
    std::uint64_t max_{0};
};

}  // namespace qe
