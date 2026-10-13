# Distributed Multi-Stack Rate Limiter

A production-grade, dual-stack rate-limiting system engineered in **C++ (C++17/cpp-httplib)** and **Node.js (Express)** to demonstrate, benchmark, and compare five foundational rate-limiting algorithms. The system supports distributed state management via **Redis** (with atomic Lua scripts) alongside a seamless, thread-safe **in-memory fallback** when Redis is unavailable. A concurrent **Python benchmark suite** provides automated functional validation and high-throughput latency profiling.

---

## Table of Contents
1. [Architecture Overview](#architecture-overview)
2. [Rate Limiting Concepts & Algorithm Deep Dive](#rate-limiting-concepts--algorithm-deep-dive)
   - [1. Fixed Window Counter](#1-fixed-window-counter)
   - [2. Sliding Window Log](#2-sliding-window-log)
   - [3. Sliding Window Counter](#3-sliding-window-counter)
   - [4. Token Bucket](#4-token-bucket)
   - [5. Leaky Bucket](#5-leaky-bucket)
   - [Algorithm Comparison Matrix](#algorithm-comparison-matrix)
3. [Distributed State Management & Redis Lua Scripts](#distributed-state-management--redis-lua-scripts)
4. [Project Structure](#project-structure)
5. [Setup & Execution Guide](#setup--execution-guide)
   - [Prerequisites](#prerequisites)
   - [Redis Setup](#redis-setup)
   - [Building & Running the C++ Server](#building--running-the-c-server)
   - [Running the Node.js Server](#running-the-nodejs-server)
   - [Running the Python Validation & Benchmark Suite](#running-the-python-validation--benchmark-suite)
6. [API Specification](#api-specification)
7. [Benchmark Performance Results](#benchmark-performance-results)
8. [Future Work: Role-Based Rate Limiting (RBAC) Architecture](#future-work-role-based-rate-limiting-rbac-architecture)

---

## Architecture Overview

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
                 |      Port: 8080       |                           |      Port: 3000       |
                 +-----------+-----------+                           +-----------+-----------+
                             |                                                   |
                             +-------------------------+-------------------------+
                                                       |
                                                       v
                                   +---------------------------------------+
                                   |         Redis State Store             |
                                   |       (localhost:6379 / RESP)         |
                                   |    * Automatic In-Memory Fallback *   |
                                   +---------------------------------------+
```

Both server implementations expose identical REST contracts and rate-limiting headers:
* `X-RateLimit-Limit`: Maximum requests permitted in the current period/bucket.
* `X-RateLimit-Remaining`: Remaining request quota.
* `X-RateLimit-Reset`: Unix timestamp (seconds) when quota refreshes.
* `Retry-After`: (Returned on `429 Too Many Requests`) Seconds the client must pause before retrying.

---

## Rate Limiting Concepts & Algorithm Deep Dive

Rate limiting protects APIs from denial-of-service (DoS) attacks, brute-force attacks, web scrapers, and cascading system overloads. Five core algorithms are implemented across both stacks:

### 1. Fixed Window Counter
* **Concept:** Time is partitioned into discrete, non-overlapping windows of length $T$ (e.g., 10 seconds). A single counter tracks requests within window $W_i = \lfloor \text{now} / T \rfloor$.
* **Boundary Flaw:** While $O(1)$ memory, it is vulnerable to **traffic bursts at window boundaries**. A client could send $M$ requests at the tail of window $W_0$ and $M$ requests at the start of window $W_1$, resulting in $2M$ requests served in a window of size $T$.
* **Time Complexity:** $O(1)$
* **Space Complexity:** $O(1)$ per client

### 2. Sliding Window Log
* **Concept:** Instead of a counter, every request timestamp is retained in a sorted collection (Redis `ZSET` or in-memory deque). When a new request arrives at $t_{\text{now}}$, entries older than $t_{\text{now}} - T$ are purged. If the remaining count is less than $M$, the request is logged and admitted.
* **Characteristics:** Extremely accurate; completely eliminates boundary burst anomalies. However, it incurs significant memory overhead under high request volume because every request adds an entry.
* **Time Complexity:** $O(\log N)$ for insertion/purging
* **Space Complexity:** $O(N)$ where $N$ is the request count per window

### 3. Sliding Window Counter
* **Concept:** A hybrid approach blending Fixed Window efficiency with Sliding Window accuracy. It calculates an estimated request count by taking a weighted sum of the previous and current window counters:
  $$\text{Estimated Count} = \text{Count}_{\text{prev}} \times \left(1 - \frac{t_{\text{elapsed}}}{T}\right) + \text{Count}_{\text{curr}}$$
* **Characteristics:** Approximates sliding window behavior without storing individual timestamps, operating in constant space.
* **Time Complexity:** $O(1)$
* **Space Complexity:** $O(1)$

### 4. Token Bucket
* **Concept:** A bucket holds up to capacity $C$ tokens. Tokens regenerate continuously at a fixed refill rate $r$ (tokens/sec):
  $$\text{tokens} = \min\left(C, \text{tokens}_{\text{prev}} + \Delta t \times r\right)$$
  Each incoming request consumes 1 token. If sufficient tokens exist, the request is permitted; otherwise, it is rejected.
* **Characteristics:** **Permits bursty traffic** up to capacity $C$, while guaranteeing that long-term throughput never exceeds $r$. Extensively used by AWS and Cloudflare.
* **Time Complexity:** $O(1)$
* **Space Complexity:** $O(1)$

### 5. Leaky Bucket
* **Concept:** Requests enter a buffer (bucket) of capacity $B$ and drain (are processed) at a constant, steady leak rate $r$. If the bucket overflows (incoming rate exceeds leak rate + buffer capacity), new requests are dropped.
* **Characteristics:** **Smooths out traffic spikes** into a strictly uniform output stream. Essential for downstream services that cannot tolerate sudden spikes.
* **Time Complexity:** $O(1)$
* **Space Complexity:** $O(1)$

---

### Algorithm Comparison Matrix

| Algorithm | Burst Handling | Memory Overhead | Precision | CPU Cost | Ideal Use Case |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Fixed Window Counter** | Poor (2x burst at boundaries) | $O(1)$ (Very Low) | Low | Very Low | Coarse quotas (e.g., daily API limits) |
| **Sliding Window Log** | Smooth (No boundary spikes) | $O(N)$ (High) | 100% Exact | Medium-High | High-value, low-volume APIs |
| **Sliding Window Counter** | Very Good (Weighted smoothing) | $O(1)$ (Very Low) | ~99% Smooth | Low | High-throughput web APIs |
| **Token Bucket** | Controlled bursts up to capacity | $O(1)$ (Very Low) | High | Very Low | User-facing endpoints, burst-tolerant APIs |
| **Leaky Bucket** | Strictly buffers or drops bursts | $O(1)$ (Very Low) | High | Very Low | Queue smoothing, steady outbound streams |

---

## Distributed State Management & Redis Lua Scripts

In distributed microservice deployments, multiple application servers process requests concurrently for the same clients. Naive "read-then-update" checks introduce critical race conditions (the **Check-Then-Act anti-pattern**).

To guarantee atomicity and sub-millisecond execution, all five algorithms are implemented using **Redis Lua Scripts**:
1. Redis executes Lua scripts as single atomic transactions on the main thread; no competing worker can interleave writes.
2. Network roundtrips are reduced from multiple operations (`GET`, `INCR`, `EXPIRE`) to a single roundtrip.
3. Every script sets an explicit TTL to avoid unbounded memory accumulation for inactive clients.

### Automatic In-Memory Fallback
If Redis is down or unreachable:
* Both C++ and Node.js automatically switch to thread-safe in-memory stores (`std::mutex` in C++, asynchronous storage in Node.js).
* Zero crash or downtime occurs; the servers log a fallback notification and continue serving requests safely.
* As soon as Redis becomes available, the servers reconnect automatically.

---

## Project Structure

```
rate-limiter/
│
├── cpp_rate_limiter/                     # C++17/20 Implementation
│   ├── CMakeLists.txt                    # Modern CMake configuration (MSVC / GCC)
│   ├── include/
│   │   ├── httplib.h                     # Header-only multi-threaded HTTP server
│   │   ├── rate_limiter.hpp              # IRateLimiter interface & Result structs
│   │   ├── algorithms.hpp                # Concrete algorithm declarations
│   │   ├── redis_client.hpp              # Native Winsock RESP Redis client
│   │   └── memory_storage.hpp            # Thread-safe in-memory fallback
│   └── src/
│       ├── main.cpp                      # Server bootstrap & HTTP route dispatch
│       ├── storage/
│       │   └── redis_client.cpp          # RESP protocol client implementation
│       └── algorithms/
│           ├── fixed_window.cpp          # Fixed Window implementation
│           ├── sliding_window_log.cpp    # Sliding Window Log implementation
│           ├── sliding_window_counter.cpp# Sliding Window Counter implementation
│           ├── token_bucket.cpp          # Token Bucket implementation
│           └── leaky_bucket.cpp          # Leaky Bucket implementation
│
├── node_rate_limiter/                    # Node.js Express Implementation
│   ├── package.json                      # Dependencies (express, ioredis)
│   └── src/
│       ├── server.js                     # Express app & route mounting
│       ├── storage/
│       │   ├── redis.js                  # ioredis connection manager & Lua runner
│       │   └── memory.js                 # In-memory storage fallback
│       ├── middleware/
│       │   └── rateLimiter.js            # Express rate limiter middleware & headers
│       ├── routes/
│       │   └── api.js                    # Route mappings for all 5 algorithms
│       └── algorithms/
│           ├── base.js                   # Base rate limiter class
│           ├── fixedWindow.js            # Fixed Window implementation
│           ├── slidingWindowLog.js       # Sliding Window Log implementation
│           ├── slidingWindowCounter.js   # Sliding Window Counter implementation
│           ├── tokenBucket.js            # Token Bucket implementation
│           └── leakyBucket.js            # Leaky Bucket implementation
│
├── tests/
│   ├── requirements.txt                  # Python dependencies (zero-dependency compatible)
│   └── benchmark.py                      # Multi-threaded validation and benchmark tool
│
└── README.md                             # Unified system documentation
```

---

## Setup & Execution Guide

### Prerequisites
* **C++ Compiler:** Visual Studio 2022 / Build Tools (MSVC) or GCC/MinGW with C++17 support.
* **CMake:** Version 3.15 or later.
* **Node.js:** v18 or later (Node v22 recommended).
* **Python:** 3.8+ (for benchmark runner).
* **Redis (Optional):** Docker or native Redis instance on `localhost:6379`.

---

### Redis Setup (Optional)
If Docker is installed:
```powershell
docker run -d --name rate-limiter-redis -p 6379:6379 redis:alpine
```
*(If Redis is not running, both servers automatically run in In-Memory Fallback mode.)*

---

### Building & Running the C++ Server

1. Open PowerShell and navigate to the C++ project folder:
   ```powershell
   cd d:\FreeCodeCamp\system-project\rate-limiter\cpp_rate_limiter
   ```
2. Configure CMake:
   ```powershell
   cmake -B build -S .
   ```
3. Compile the executable:
   ```powershell
   cmake --build build --config Release
   ```
4. Run the server:
   ```powershell
   .\build\Release\cpp_rate_limiter.exe
   ```
* Default Port: `8080`
* Health Check: `http://localhost:8080/health`

---

### Running the Node.js Server

1. Open PowerShell and navigate to the Node.js project folder:
   ```powershell
   cd d:\FreeCodeCamp\system-project\rate-limiter\node_rate_limiter
   ```
2. Install dependencies:
   ```powershell
   npm install
   ```
3. Start the server:
   ```powershell
   npm start
   ```
* Default Port: `3000`
* Health Check: `http://localhost:3000/health`

---

### Running the Python Validation & Benchmark Suite

From the project root directory (`rate-limiter/`), run:

```powershell
# 1. Run quick functional assertion checks against all algorithms
python tests/benchmark.py --validate-only

# 2. Run concurrent load benchmarking across both servers (50 workers, 200 requests)
python tests/benchmark.py --concurrency 50 --requests 200

# 3. Test a specific algorithm on C++ only
python tests/benchmark.py --servers cpp --algorithm token-bucket

# 4. Stress test with 100 workers and 500 requests
python tests/benchmark.py --concurrency 100 --requests 500
```

---

## API Specification

All endpoints support `POST` and `GET`. Clients can pass identity via `X-Client-Id` header, query parameter `?clientId=...`, or default to client IP.

| Endpoint | Method | Algorithm | Default Limit Configuration |
| :--- | :--- | :--- | :--- |
| `/health` | `GET` | Health Check | Returns server name, Redis connectivity, and storage mode |
| `/api/rate-limit/fixed-window` | `POST` / `GET` | Fixed Window Counter | 10 requests / 10 seconds |
| `/api/rate-limit/sliding-window-log` | `POST` / `GET` | Sliding Window Log | 10 requests / 10 seconds |
| `/api/rate-limit/sliding-window-counter` | `POST` / `GET` | Sliding Window Counter | 10 requests / 10 seconds |
| `/api/rate-limit/token-bucket` | `POST` / `GET` | Token Bucket | Capacity: 10, Refill: 2.0 tokens/sec |
| `/api/rate-limit/leaky-bucket` | `POST` / `GET` | Leaky Bucket | Capacity: 10, Leak: 2.0 units/sec |

### Sample Success Response (`200 OK`)
```json
{
  "status": "success",
  "code": 200,
  "message": "Request successfully processed under 'TokenBucket' rate limiter.",
  "algorithm": "TokenBucket",
  "rateLimit": {
    "allowed": true,
    "limit": 10,
    "remaining": 8,
    "reset": 1791895200,
    "retryAfter": 0
  },
  "server": "C++ httplib"
}
```

### Sample Rate-Limited Response (`429 Too Many Requests`)
```json
{
  "status": "error",
  "code": 429,
  "message": "Too Many Requests: Rate limit exceeded for algorithm 'TokenBucket'",
  "algorithm": "TokenBucket",
  "retryAfter": 1,
  "reset": 1791895201,
  "server": "C++ httplib"
}
```

---

## Benchmark Performance Results

Conducted with 50 concurrent worker threads sending 200 requests per algorithm:

### Validation & Enforcement Audit
```
================================================================================
                ALGORITHM VALIDATION & ENFORCEMENT AUDIT                
================================================================================
Server         | Algorithm                | Allowed | Blocked | Headers | Status
--------------------------------------------------------------------------------
C++ Server     | Fixed Window Counter     | 10      | 5       | OK      | PASS
C++ Server     | Sliding Window Log       | 10      | 5       | OK      | PASS
C++ Server     | Sliding Window Counter   | 10      | 5       | OK      | PASS
C++ Server     | Token Bucket             | 10      | 5       | OK      | PASS
C++ Server     | Leaky Bucket             | 10      | 5       | OK      | PASS
Node Server    | Fixed Window Counter     | 10      | 5       | OK      | PASS
Node Server    | Sliding Window Log       | 10      | 5       | OK      | PASS
Node Server    | Sliding Window Counter   | 10      | 5       | OK      | PASS
Node Server    | Token Bucket             | 10      | 5       | OK      | PASS
Node Server    | Leaky Bucket             | 10      | 5       | OK      | PASS
================================================================================
```

### Concurrent Latency & Throughput Benchmark
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

---

## Future Work: Role-Based Rate Limiting (RBAC) Architecture

In real-world multi-tenant platforms, applying identical static rate limits to all consumers is insufficient. Free users, premium subscribers, enterprise clients, and internal services have distinct SLAs and traffic expectations.

### 1. Conceptual Architecture

```
Incoming Request
       │
       ▼
[Authentication Layer]
   Extracts Identity (JWT / API Key / Client ID)
       │
       ▼
[Role & Tier Resolver]
   Maps Identity to Tier (Anonymous, Free, Pro, Enterprise, Admin)
       │
       ▼
[Dynamic Policy Engine]
   Retrieves Tier Limits:
   - Max Requests / Window
   - Token Capacity & Refill Rate
   - Priority Queuing Flag
       │
       ▼
[Hierarchical Rate Limiting Engine]
   Check 1: Global System Safeguard (DDoS mitigation)
   Check 2: Organization / Tenant Limit (Multi-tenant cap)
   Check 3: User / Role Limit (Fine-grained quota)
       │
    ┌──┴──┐
    ▼     ▼
 [Pass] [Reject (429 with Tier Diagnostics)]
```

### 2. Tier Hierarchy & Quota Matrix

| Tier | Auth Mechanism | Algorithm Assigned | Burst Capacity | Refill / Rate | Over-Quota Policy |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Anonymous** | Client IP | Fixed Window | 10 reqs | 1 req/min | Strict 429 block |
| **Free Tier** | JWT (`role: free`) | Sliding Window Counter | 60 reqs | 60 reqs/min | 429 with upgrade link |
| **Pro Tier** | JWT (`role: pro`) | Token Bucket | 500 tokens | 50 tokens/sec | Soft warning + burst allowance |
| **Enterprise** | API Key (`tier: ent`)| Token Bucket | 5,000 tokens | 500 tokens/sec | Priority queue buffer, never dropped |
| **Internal Services**| mTLS / Service JWT| Leaky Bucket | 10,000 reqs | Uncapped leak | Bypasses public limiters |

### 3. Redis Key Namespace Schema
To support multi-tier isolation without key collisions, Redis keys will follow a structured hierarchical schema:
```
rl:rbac:{tenant_id}:{role_tier}:{user_id}:{algorithm}:{window_id}
```
Example:
* `rl:rbac:acme_corp:pro:user_9821:tb`
* `rl:rbac:global:free:user_102:swcnt:179189520`

### 4. Dynamic Policy Management & Hot Reloading
Rather than hardcoding limits in code:
1. **Redis Hash Policy Store:** Store policies in `rl:policies:{role_tier}` (e.g., `HSET rl:policies:pro capacity 500 refill_rate 50`).
2. **Pub/Sub Hot-Reloading:** When admin changes tier limits, a `PUBLISH rl:policy_update {tier}` message triggers in-memory cache refresh across all running C++ and Node.js nodes without restarting the services.

### 5. Implementation Roadmap
* **Milestone 1:** Integrate JWT middleware in Node.js and lightweight JWT claims parsing in C++.
* **Milestone 2:** Implement Redis Hash policy resolution with local LRU caching (TTL 60s) to keep latency under 1ms.
* **Milestone 3:** Add response headers for tier visibility: `X-RateLimit-Tier: Pro` and `X-RateLimit-Tier-Limit: 500`.
* **Milestone 4:** Implement enterprise priority queuing using Redis Streams or BullMQ.
