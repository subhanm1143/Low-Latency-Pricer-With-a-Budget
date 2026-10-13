#include "replay.hpp"

#include <cstdio>

namespace qe {

// Deterministic replay: the same recorded session produces the same sequence
// of quotes, every run, on any machine.
//
// This exists because the bugs that matter here are rare. A tick-to-quote path
// that misbehaves once in ten million ticks cannot be debugged by staring at
// it — you need the bad tick to happen again, on demand, with a debugger
// attached.
//
// Achieving determinism means removing every input that varies between runs:
//
//   1. The market data — solved by replaying a recorded file rather than a
//      live socket.
//   2. Thread interleaving — solved by running feed and pricer on ONE thread
//      in replay mode. This changes the timing, which is why replay is for
//      correctness bugs, not for latency measurement.
//   3. The clock — solved below. This is the one people miss.
//
// Note what is deliberately NOT reproduced: absolute latency. Replay proves
// what the engine computed, not how fast it did so.
void ReplayEngine::run(const char* path) {
    std::FILE* f = std::fopen(path, "rb");
    if (f == nullptr) {
        return;
    }

    RawMessage msg{};
    std::uint64_t index = 0;

    while (std::fread(&msg, sizeof(msg), 1, f) == 1) {
        // The recorded ingress time is REPLAYED, not regenerated.
        //
        // If replay called steady_clock::now() it would produce different
        // timestamps every run. Anything downstream that branches on a
        // timestamp — a staleness check, a rate limiter, a time-decay term in
        // the pricer — would then take different branches on different runs,
        // and the replay would not be a replay.
        //
        // Theta is the concrete case here: time_to_expiry is derived from the
        // current time, so a live clock makes every option's price drift
        // between runs and no two replays agree.
        clock_.set(recorded_ingress_ns_[index]);

        feed_.on_message(msg);

        // Single-threaded: drain the queue fully before the next message, so
        // the interleaving is fixed rather than whatever the scheduler chose.
        while (engine_.run_once_if_available()) {
        }

        ++index;
    }

    std::fclose(f);
}

}  // namespace qe
