# Velox Trading Engine

A low-latency trading engine built on a foundation of lock-free data structures. Designed around the principle that latency is eliminated architecturally — not optimized away after the fact.

## Overview

Velox is a C++17 trading engine targeting sub-microsecond order-to-execution latency. Every component in the hot path is allocation-free, lock-free, and cache-aware. The architecture maps each data flow to the correct concurrent primitive rather than reaching for a general-purpose queue everywhere.

```
Market Feed → Feed Handler → Order Book → Matching Engine → Risk Manager → Execution Gateway → Position Manager
                                                                 ↓
                                                            P&L Tracking
```

All inter-stage communication uses SPSC queues — the fastest possible channel when producer and consumer are known at design time.

---

## Architecture

### Pipeline Stages

| Stage | Thread | Structure Used | Latency |
|---|---|---|---|
| Feed Handler | Core 0 | ITCH 5.0 Parser | ~15 ns per message |
| Order Book | Core 1 | Sorted Vector + HashMap | ~26 ns per order |
| Matching Engine | Core 2 | Price-Time Priority | ~8 μs per match |
| Risk Manager | Core 3 | Atomic Counter + HashMap | ~4 ns per check |
| Execution Gateway | Core 4 | SPSC Queue + Object Pool | ~33 ns per report |
| Position Manager | Core 5 | Weighted Average P&L | ~23 ns per update |

### Lock-Free Primitives

Each structure is chosen for the specific access pattern of its stage; not as a general solution.

| Structure | Role | Why This One |
|---|---|---|
| **SPSC Queue** | Inter-stage message passing | Single producer/consumer → wait-free |
| **Ring Buffer** | Price level order queue | Fixed allocation, FIFO, cache-friendly |
| **HashMap** | Price level lookup, order cancellation | O(1) access on critical path |
| **Object Pool** | Order and report reuse | Eliminates `new`/`delete` in hot path |
| **Atomic Counter** | Sequence numbers, statistics | Wait-free monotonic increments |
| **PooledPtr** | RAII memory management | Automatic return to pool |
| **Hazard Pointers** | Treiber stack reclamation | Safe memory reuse |

### Why No MPMC Queue

Multiple-producer scenarios are handled by **partitioning**: each strategy thread owns its own SPSC queue to the execution gateway. The gateway services them in round-robin order. This gives equivalent throughput with strictly lower latency than a shared MPMC queue under contention.

---

## Project Structure

```
mini-trading-engine/
├── include/velox/
│ ├── feed/
│ │ └── feed_handler.hpp # ITCH 5.0 parser
│ ├── book/
│ │ ├── order_book.hpp # Price level management
│ │ ├── price_level.hpp # Ring buffer per level
│ │ └── book_snapshot.hpp # RCU-protected snapshots
│ ├── matching/
│ │ ├── matching_engine.hpp # Order matching
│ │ └── order.hpp # Order struct
│ ├── risk/
│ │ ├── risk_manager.hpp # Position limits
│ │ └── position_manager.hpp # P&L tracking
│ ├── gateway/
│ │ └── execution_gateway.hpp # Report routing
│ └── core/
│ └── symbol_engine.hpp # Per-symbol engine
├── src/ (matching .cpp files)
├── benchmarks/
│ └── (10+ benchmarking routines, including one for overall pipeline)
├── tests/
│ └── (74+ unit and integration tests)
├── tools/
│ ├── create_mock_itch.cpp # Mock ITCH generator
│ └── itch_parser.cpp # Real ITCH replay tool
├── third_party/
│ ├── googletest/ # Unit testing
│ ├── benchmark/ # Performance benchmarking
│ └── whirlpool/ # Custom lock-free library (submodule)
└── CMakeLists.txt
```

---

## Building

### Requirements

- C++17 compiler (GCC 9+, Clang 10+)
- CMake 3.16+
- Google Benchmark (for benchmarks)

### Build

```bash
# With tests and benchmarks
Remove-Item -Recurse -Force build -ErrorAction SilentlyContinue
cmake -B build -DBUILD_BENCHMARKS=ON -DBUILD_TESTS=ON
cmake --build build --config Release
```

### Run Trading Engine
```bash
./build/src/Release/velox_trading_engine.exe
```

### Run Tests

```bash
# Run all tests
./build/tests/Release/velox_tests.exe
```

### Run Benchmarks

```bash
# Run benchmark routine for Order Book
./build/benchmarks/Release/bench_order_book.exe  
```

## Performance

All measurements taken on a pinned core with frequency scaling disabled. Latency reported as percentiles over 1M samples; mean latency is not reported because it is not meaningful for tail-sensitive workloads.

### Lock-Free Primitives

| Structure | Operation | Time
|---|---|---|
| SPSC Queue | push/pop | ~3ns |
| Object Pool | acquire/release | ~23ns |
| Order Book | add order | ~26ns |
| Order Book | match | ~8μs |
| Risk Manager | check order | ~4ns |
| Position Manager | update P&L | ~23ns |
| Execution Gateway | send report | ~33ns | 

*Results pending hardware benchmarking. Target: SPSC within 2x of LMAX Disruptor.*

### Pipeline Throughput

| Metric | Measured |
|---|---|
| Match latency | ~8μs |
| Order throughput | ~70,000 orders/s |
| Full pipeline | 1.9ms |
| ITCH parse rate | 15ns/message |

### Key Design Considerations

No dynamic allocation in the hot path. All orders and reports come from object pools. The matching engine processes an entire order lifecycle without calling new or delete.

Ring buffer for price levels. Unlike traditional doubly-linked lists, the ring buffer provides O(1) FIFO operations with excellent cache locality. Lazy deletion marks cancelled orders without immediate removal.

Partitioned concurrency over shared queues. Each symbol runs on its own thread with dedicated SPSC queues. Contention is eliminated by design, not managed at runtime.

Price-time priority matching. Orders at the same price are executed in FIFO order using the ring buffer's natural ordering.

Real-time ITCH replay. The `itch_parser` tool reads binary ITCH files message-by-message (streaming, not loading entire file) and respects original timestamps for realistic simulation.

## Validation Methods

- 74+ unit and integration tests covering all components

- Google Benchmark suite for performance regression detection

- ITCH 5.0 sample files from NASDAQ

- Mock ITCH generator for deterministic testing

## References

- Michael & Scott, *Simple, Fast, and Practical Non-Blocking and Blocking Concurrent Queue Algorithms*, PODC 1996
- Vyukov, *Bounded MPMC Queue*, 2010 — http://www.1024cores.net/home/lock-free-algorithms/queues/bounded-mpmc-queue  
- LMAX Disruptor technical paper — https://lmax-exchange.github.io/disruptor/disruptor.html
- NASDAQ ITCH 5.0 Specification — https://www.nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/NQTVITCHspecification.pdf
- Herlihy & Shavit, *The Art of Multiprocessor Programming*