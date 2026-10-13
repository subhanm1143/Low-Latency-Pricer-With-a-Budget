# Optimization log

Every entry: a hypothesis, the change, and the measured result. An entry with
no numbers is not an entry.

Baseline, 10M ticks replayed, release build, pinned cores:

    p50    1,180 ns
    p99    4,920 ns
    p99.9 87,400 ns
    max  312,000 ns

## 1. Cache-line padding on the ring buffer indices

**Hypothesis.** `head_` and `tail_` share a cache line, so every push
invalidates the line the consumer is reading. Expect improvement across the
whole distribution, largest at p50 where the coherence traffic dominates.

**Change.** `alignas(64)` on both indices.

    p50    1,180 →   340 ns   (-71%)
    p99    4,920 → 1,510 ns   (-69%)
    p99.9 87,400 → 79,200 ns  (-9%)

**Result.** Confirmed, and the shape is informative: the tail barely moved,
which says the p99.9 is caused by something else entirely. Next hypothesis.

## 2. Denormal guard in the pricer

**Hypothesis.** The p99.9 is dominated by near-expiry contracts producing
denormal intermediates, each costing a microcode assist. If so, the slow ticks
should correlate with time-to-expiry, not with book size.

**Check first.** Tagged the slowest 0.1% of ticks and looked at which contracts
they priced. 94% were contracts with less than one day to expiry. Hypothesis
supported before making the change.

**Change.** Early return on `t <= 1e-10 || v <= 1e-10`, returning intrinsic.

    p50      340 →   338 ns   (unchanged)
    p99    1,510 → 1,490 ns   (unchanged)
    p99.9 79,200 → 2,760 ns   (-97%)

**Result.** Confirmed. The mean would have moved 0.5% and told us nothing.

## 3. Reserving the quote publisher's buffer

**Hypothesis.** The publisher's output vector grows during large batches,
allocating in the hot path. Expect a tail improvement.

**Change.** `reserve(kMaxBookSize)` at construction.

    p50      338 →   338 ns   (unchanged)
    p99    1,490 → 1,488 ns   (unchanged)
    p99.9  2,760 → 2,750 ns   (unchanged)

**Result.** REFUTED. The vector had already grown to its high-water mark within
the first few thousand ticks of the run, so steady-state never allocated. The
change is harmless and kept for cold-start behaviour, but it did not do what I
predicted and the log says so.
