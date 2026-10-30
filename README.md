# Distributed Multi-Stack Rate Limiter

[![Standard](https://img.shields.io/badge/C%2B%2B-17-blue.svg?logo=c%2B%2B)](https://en.cppreference.com/w/cpp/17)
[![Node.js](https://img.shields.io/badge/Node.js-v18%2B%20%7C%20v22-green.svg?logo=node.js)](https://nodejs.org/)
[![Redis](https://img.shields.io/badge/Redis-Atomic%20Lua-red.svg?logo=redis)](https://redis.io/)
[![Python](https://img.shields.io/badge/Python-3.8%2B%20Zero--Dep-yellow.svg?logo=python)](https://python.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-purple.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux-lightgrey.svg)]()

> A high-performance, production-grade distributed rate-limiting engine engineered in parallel across two native stacks: **modern C++ (C++17/cpp-httplib)** and **Node.js (Express/ioredis)**. Designed to demonstrate, benchmark, and compare five foundational rate-limiting algorithms with atomic distributed state management via **Redis Lua scripts**, resilient zero-downtime **in-memory fallbacks**, and an automated multi-threaded **Python benchmarking suite**.

---

## 🏛️ Architecture Overview

```
                                  +------------------------------------------+
                                  |         Python Benchmark Suite           |
                                  |   (Concurrent Multi-Client Validation)   |
                                  +--------------------+---------------------+
                                                       |
                             +-------------------------+-------------------------+
                             |                                                   |
                             v                                                   v
                 +-----------------------+                           +-----------------------+
                 |   cpp_rate_limiter    |                           |   node_rate_limiter   |
                 |  (cpp-httplib/CMake)  |                           |     (Express.js)      |
                 | ThreadPool (128 wrks) |                           |  Single-Thread Event  |
                 |      Port: 8080       |                           |      Port: 3000       |
                 +-----------+-----------+                           +-----------+-----------+
                             |                                                   |
                             +-------------------------+-------------------------+
                                                       |
                                                       v
                                   +---------------------------------------+
                                   |         Redis State Store             |
                                   |    (Atomic Lua Scripts + Key TTL)     |
                                   +-------------------+-------------------+
                                                       |
                                          [If Redis Disconnected]
                                                       v
                                   +---------------------------------------+
                                   |     Local In-Memory Fallback          |
                                   |   (C++ std::mutex / Node Map Store)   |
                                   +---------------------------------------+
```

Both server implementations expose identical REST contracts and standardized RFC rate-limiting headers:
* `X-RateLimit-Limit`: Maximum requests permitted in the current period/bucket.
* `X-RateLimit-Remaining`: Remaining request quota for the calling client.
* `X-RateLimit-Reset`: Unix timestamp (seconds) when quota refreshes.
* `Retry-After`: (Returned on `429 Too Many Requests`) Seconds the client must pause before retrying.

---

## ⚡ Core Systems Engineering Features

| CS Domain | Feature Implemented | Technical Mechanism |
| :--- | :--- | :--- |
| **Distributed Systems** | Atomic Distributed State Management | Single-RTT Redis Lua scripts preventing Check-Then-Act race conditions |
| **System Design** | Multi-Algorithm Rate Governance | 5 algorithms comparing burst tolerance, smoothing, and boundary anomalies |
| **Operating Systems** | High-Concurrency Threading | Pre-allocated C++ worker thread pool with `std::mutex` vs Node.js Event Loop |
| **Computer Networks** | Wire Protocol & Header Governance | Native Winsock RESP protocol client & RFC draft standard rate-limiting headers |
| **Resilience Engineering** | Zero-Downtime Fallback | Automatic transition to thread-safe local memory store if Redis disconnects |

---

## 🔬 Core Algorithms Implemented

| Milestone | Algorithm | Burst Handling | Space Complexity | Core Mechanism |
| :---: | :--- | :--- | :---: | :--- |
| **FR1** | **Fixed Window Counter** | Vulnerable to $2\times$ boundary spikes | $O(1)$ | Disjoint epoch window buckets with atomic Redis increment |
| **FR2** | **Sliding Window Log** | 100% exact (No boundary spikes) | $O(N)$ | Sorted timestamp sets (Redis `ZSET`), range pruning of expired ticks |
| **FR3** | **Sliding Window Counter** | ~99.9% smooth weighted estimate | $O(1)$ | Sliding weighted sum between previous and current window counters |
| **FR4** | **Token Bucket** | Controlled bursts up to capacity $C$ | $O(1)$ | Lazy timestamp-based refill at rate $r$, $O(1)$ constant-time consumption |
| **FR5** | **Leaky Bucket** | Strictly buffers or smooths traffic | $O(1)$ | Constant outflow leak rate $r$, queue buffer overflow rejection |

---

## 📚 Dedicated Technical Documentation Hub

Every architectural decision, mathematical proof, concurrency pattern, and benchmark result is documented in depth:

* 📖 **[01. Architecture & System Design](docs/01_architecture_and_system_design.md)** — Dual-stack topology, threading models, fail-open vs fail-closed analysis, and RBAC roadmap.
* 📖 **[02. Rate Limiting Algorithms Deep Dive](docs/02_rate_limiting_algorithms.md)** — Mathematical proofs, boundary flaws ($2M/T$), Big-O complexities, and selection decision tree.
* 📖 **[03. Distributed State & Redis Lua Scripts](docs/03_distributed_state_and_redis_lua.md)** — Check-then-act race conditions, atomic 1-RTT Lua scripts, and native RESP client.
* 📖 **[04. Setup & Execution Guide](docs/04_setup_and_execution_guide.md)** — Docker Redis setup, one-click Windows scripts, CMake builds, and test commands.
* 📖 **[05. API Specification & Contract](docs/05_api_specification_and_contract.md)** — Complete REST endpoints, client identity resolution, and 200/429 JSON payloads.
* 📖 **[06. Benchmarks & Performance Analysis](docs/06_benchmarks_and_performance_analysis.md)** — Empirical 50-worker latency percentiles (p50/p95/p99), RPS throughput, and audit tables.
* 📑 **[Documentation Index & Reading Guide](docs/README.md)** — Overview portal and tailored reading paths.

---

## 🚀 Quickstart & Build Instructions

### 1. One-Click Automated Benchmark & Validation
Execute the verification script to run functional assertion tests against all algorithms:
```powershell
.\run_benchmark.bat --validate-only
```

To run a concurrent load benchmark (50 worker threads, 200 requests):
```powershell
.\run_benchmark.bat --concurrency 50 --requests 200
```

### 2. Building & Running the C++ Server
Using the one-click build script:
```powershell
.\build_cpp.bat
.\cpp_rate_limiter\build\Release\cpp_rate_limiter.exe
```
*(Default Port: `8080` | Health Check: `http://localhost:8080/health`)*

### 3. Running the Node.js Server
```powershell
cd node_rate_limiter
npm install
npm start
```
*(Default Port: `3000` | Health Check: `http://localhost:3000/health`)*

---

## 📊 Benchmark Performance Highlights

Conducted with 50 concurrent worker threads sending 200 requests per algorithm:

```
==============================================================================================
                     CONCURRENCY LOAD & LATENCY BENCHMARK RESULTS                     
==============================================================================================
Server       | Algorithm              | Reqs   | RPS      | p50(ms)  | p95(ms)  | p99(ms)  | 200/429 Ratio
----------------------------------------------------------------------------------------------
C++ Server   | Fixed Window Counter   | 200    | 1549.6   | 5.72     | 15.63    | 32.53    | 100/100
C++ Server   | Sliding Window Log     | 200    | 2039.4   | 5.37     | 13.19    | 16.66    | 100/100
C++ Server   | Sliding Window Counter | 200    | 2213.3   | 4.96     | 12.02    | 15.05    | 100/100
C++ Server   | Token Bucket           | 200    | 1894.6   | 5.71     | 16.27    | 23.39    | 100/100
C++ Server   | Leaky Bucket           | 200    | 1841.5   | 7.52     | 15.77    | 20.77    | 100/100
Node Server  | Fixed Window Counter   | 200    | 1640.8   | 13.32    | 24.09    | 26.53    | 100/100
Node Server  | Sliding Window Log     | 200    | 1619.5   | 17.81    | 25.88    | 29.45    | 100/100
Node Server  | Sliding Window Counter | 200    | 1735.9   | 9.45     | 20.45    | 24.29    | 100/100
Node Server  | Token Bucket           | 200    | 1830.9   | 7.76     | 18.12    | 22.87    | 100/100
Node Server  | Leaky Bucket           | 200    | 1804.1   | 8.87     | 14.91    | 17.90    | 100/100
==============================================================================================
```
* **Median Latency (p50)**: C++ achieved **sub-5ms** (4.96ms on Sliding Window Counter).
* **Peak Throughput**: C++ sustained **2,213.3 RPS** under heavy concurrency.
* **Accuracy & Concurrency**: 100% enforcement of quota limits across all test vectors.

---

## 🛠️ Build Toolchain & System Specifications

| Component | Technology | Version / Specification |
| :--- | :--- | :--- |
| **C++ Compiler** | MSVC / MinGW-w64 | Visual Studio 2022 (MSVC 19.3x) / GCC 11+ |
| **C++ Standard** | ISO C++ | C++17 (`-std=c++17` / `/std:c++17`) |
| **C++ HTTP Server** | `cpp-httplib` | Header-only multi-threaded socket engine |
| **Node.js Runtime** | Node.js | v18+ / v22 LTS (Express.js + ioredis) |
| **Distributed Store** | Redis | 6.x / 7.x (Atomic Lua 5.1 scripts) |
| **Benchmarking Engine** | Python | 3.8+ (`concurrent.futures`, zero dependencies) |
| **Platform** | OS | Windows x64 / Linux x86_64 |
| **License** | Open Source | MIT License |

---

## 📄 License

This project is licensed under the MIT License — see the [LICENSE](LICENSE) file for details.
