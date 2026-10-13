# Belvedere Trading | Options Quote Engine — A Low-Latency Pricer With a Budget

An intermediate C++ systems project shaped like the entry-level engineering work a options market maker hires into. You build a market data feed handler and an options pricing engine that quotes a book in real time, then you measure it — not with an average, but with a p99.9 tick-to-quote histogram that a CI gate enforces. Along the way you meet the things that actually cost microseconds: allocation in the hot path, a mutex where a ring buffer belongs, false sharing between two threads that never touch the same variable, and a denormal float that makes one branch fifty times slower than its neighbour. Every optimization is a hypothesis with a measured before and after, because on this desk a change you cannot measure is a change you cannot defend.

Built step-by-step with [KhwajaLabs Build](https://khwajalabs.com).

## Stack
- C++
- Black-Scholes
- lock-free programming
- benchmarking
- CMake
- perf
