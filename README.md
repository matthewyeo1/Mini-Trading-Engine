# Velox Trading Engine

A low-latency trading engine built on a foundation of lock-free data structures. Designed around the principle that latency is eliminated architecturally — not optimized away after the fact.

## Overview

Velox is a C++17 trading engine targeting sub-microsecond order-to-execution latency. Every component in the hot path is allocation-free, lock-free, and cache-aware. The architecture maps each data flow to the correct concurrent primitive rather than reaching for a general-purpose queue everywhere.

```
Market Feed → Decoder → Order Book → Matching Engine → Risk → Execution Gateway
                                          ↑                           ↓
                                    Symbol Config (RCU)      Position Manager
```

All inter-stage communication uses SPSC queues — the fastest possible channel when producer and consumer are known at design time.

---

## Architecture

### Pipeline Stages

| Stage | Thread | Structure Used | Latency Budget |
|---|---|---|---|
| Feed Handler | Core 0 | — | ~20ns |
| Market Data Decoder | Core 1 | SPSC Queue (feed→decoder) | ~50ns |
| Order Book Updater | Core 2 | SPSC Queue + HashMap | ~100ns |
| Matching Engine | Core 3 | Treiber Stack (level pool) | ~150ns |
| Risk Manager | Core 4 | Atomic Counter + HashMap | ~20ns |
| Execution Gateway | Core 5 | SPSC Queue (per strategy) | ~50ns |

### Lock-Free Primitives

Each structure is chosen for the specific access pattern of its stage; not as a general solution.

| Structure | Role | Why This One |
|---|---|---|
| **SPSC Queue** | Inter-stage message passing | Single producer/consumer → wait-free, ~3ns per op |
| **Ring Buffer** | Market data capture and replay logging | Fixed allocation, power-of-2 masking, zero GC pressure |
| **HashMap** | Order book symbol lookups, risk exposure | O(1) reads on the critical path, open addressing |
| **Object Pool** | Order and fill object reuse | Eliminates `new`/`delete` in the hot path entirely |
| **Atomic Counter** | Order ID generation | Wait-free monotonic sequence, no coordination |
| **RCU** | Symbol config and reference data | Readers never stall; config changes are rare writes |
| **Treiber Stack** | Price level free-list | LIFO reuse of level nodes, ABA-safe with hazard pointers |

### Why No MPMC Queue

Multiple-producer scenarios are handled by **partitioning**: each strategy thread owns its own SPSC queue to the execution gateway. The gateway services them in round-robin order. This gives equivalent throughput with strictly lower latency than a shared MPMC queue under contention.

---

## Project Structure

```
mini-trading-engine/
├── include/
│   └── velox/
|       ├── feed/
│       │   ├── feed_handler.hpp        
│       │   └── decoder.hpp             
│       │
│       ├── book/
│       │   ├── order_book.hpp          
│       │   ├── price_level.hpp         
│       │   └── book_snapshot.hpp       
│       │
│       ├── matching/
│       │   ├── matching_engine.hpp     
│       │   └── order.hpp               
│       │
│       ├── risk/
│       │   ├── risk_manager.hpp        
│       │   └── position_manager.hpp    
│       │
│       └── gateway/
│           ├── execution_gateway.hpp   
│           └── fix_encoder.hpp               
├── src/
│   ├── feed/
│   │   ├── feed_handler.cpp        # Raw market data ingestion
│   │   └── decoder.cpp             # ITCH/FIX message decoding
│   ├── book/
│   │   ├── order_book.cpp          # Price level management
│   │   ├── price_level.cpp         # Per-level order queue
│   │   └── book_snapshot.cpp       # RCU-protected read view
│   ├── matching/
│   │   ├── matching_engine.cpp     # Price-time priority matching
│   │   └── order.cpp               # Order struct (pool-allocated)
│   ├── risk/
│   │   ├── risk_manager.cpp        # Position limits and circuit breakers
│   │   └── position_manager.cpp    # Atomic P&L and exposure tracking
│   └── gateway/
│       ├── execution_gateway.cpp   # Outbound order routing
│       └── fix_encoder.cpp         # FIX 4.2 message formatting
├── benchmarks/
│   ├── bench_primitives.cpp        # Lock-free structure microbenchmarks
│   ├── bench_pipeline.cpp          # End-to-end pipeline throughput
│   └── bench_book.cpp              # Order book update latency
├── tests/
│   └── ...
├── third_party/                    # Third-party directory containing submodules 
│   ├── benchmark/                  # Google Benchmark library for performance testing
|   |   └── ...
│   ├── googletest/                 # Google Test framework for unit testing
|   |   └── ...
│   └── whirlpool/                  # Lock-free data structure library for core engine
|       └── ...
├── tools/
│   └── itch_replay.cpp             # Replay recorded NASDAQ ITCH 5.0 data
└── CMakeLists.txt
```

---

## Building

### Requirements

- C++17 compiler (GCC 9+, Clang 10+)
- CMake 3.16+
- Google Benchmark (for benchmarks)
- Linux recommended — thread pinning and TSC measurement require it

### Build

```bash
# With tests and benchmarks
Remove-Item -Recurse -Force build -ErrorAction SilentlyContinue
cmake -B build -DBUILD_BENCHMARKS=ON -DBUILD_TESTS=ON
cmake --build build --config Release
```

### Run Tests

```bash
./build/tests/Release/velox_tests.exe
```

### Run Benchmarks

```bash
./build/benchmarks/Release/bench_order.exe  
```

## Performance

All measurements taken on a pinned core with frequency scaling disabled. Latency reported as percentiles over 1M samples; mean latency is not reported because it is not meaningful for tail-sensitive workloads.

### Lock-Free Primitives

| Structure | Operation | p50 | p99 | p99.9 |
|---|---|---|---|---|
| SPSC Queue | push + pop | — | — | — |
| Ring Buffer | push + pop | — | — | — |
| HashMap | lookup (hit) | — | — | — |
| Object Pool | acquire + release | — | — | — |
| Atomic Counter | increment | — | — | — |
| Treiber Stack | push + pop | — | — | — |

*Results pending hardware benchmarking. Target: SPSC within 2x of LMAX Disruptor.*

### Pipeline Latency

| Metric | Target | Measured |
|---|---|---|
| Feed decode latency | < 100ns | — |
| Order book update | < 200ns | — |
| Match + risk check | < 100ns | — |
| Order-to-execution (hot path) | < 1µs | — |
| Throughput at saturation | > 10M orders/sec | — |

### Measurement Methodology

Latency is measured using `rdtsc` directly — not `std::chrono`. TSC has ~1ns resolution with no syscall overhead. All measurements:

- Threads pinned to isolated cores via `pthread_setaffinity_np`
- CPU frequency scaling disabled
- 100k iteration warmup before recording
- Reported as p50 / p99 / p99.9 over 1M samples
- Compared against LMAX Disruptor and Chronicle Queue published benchmarks as a sanity check

---

## Validation Against Real Market Data

Validate by replaying real market data and verifying fill prices match expected outcomes.

**NASDAQ ITCH 5.0** sample files are freely available at [https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/](https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/). The `tools/itch_replay` tool parses these files and drives the full pipeline:

```bash
./build/tools/itch_replay --file 01302020.NASDAQ_ITCH50 --symbol AAPL --latency-report
```

This produces:
- Fill prices vs. expected prices (correctness)
- Per-stage latency breakdown (performance)
- Order book state snapshots at configurable intervals (accuracy)

A correct matching engine replaying ITCH data should produce fills identical to the exchange's public trade tape.

---

## Design Decisions

**No dynamic allocation in the hot path.** All order objects come from the pool. All price level nodes come from the Treiber stack free-list. The matching engine processes an entire order lifecycle without calling `new` or `delete`.

**Partitioned concurrency over shared queues.** Rather than a shared MPMC queue for strategy → gateway communication, each strategy owns a dedicated SPSC channel. Contention is eliminated by design, not managed at runtime.

**RCU for reference data.** Symbol configurations change rarely but are read on every order. RCU lets the matching engine read config with zero synchronization overhead on the critical path. Updates are handled on a background thread with copy-on-write semantics.

**TSC-based timing throughout.** All internal timestamps use `rdtsc`. The feed handler stamps each message on arrival; the execution gateway stamps each fill on departure. The difference is the authoritative latency number.

---

## Roadmap

- [ ] Feed handler — ITCH 5.0 parser
- [ ] Order book — price level management with RCU snapshots  
- [ ] Matching engine — price-time priority, partial fills, cancel/replace
- [ ] Risk manager — position limits, per-symbol circuit breakers
- [ ] Execution gateway — FIX 4.2 encoder, simulated wire
- [ ] ITCH replay tool
- [ ] Full pipeline benchmark with percentile reporting
- [ ] Comparison report vs. Disruptor / Chronicle Queue

---

## References

- Michael & Scott, *Simple, Fast, and Practical Non-Blocking and Blocking Concurrent Queue Algorithms*, PODC 1996
- Vyukov, *Bounded MPMC Queue*, 2010 — http://www.1024cores.net/home/lock-free-algorithms/queues/bounded-mpmc-queue  
- LMAX Disruptor technical paper — https://lmax-exchange.github.io/disruptor/disruptor.html
- NASDAQ ITCH 5.0 Specification — https://www.nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/NQTVITCHspecification.pdf
- Herlihy & Shavit, *The Art of Multiprocessor Programming*