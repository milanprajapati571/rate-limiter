# 01. Architecture & System Design

This document details the high-level system architecture, threading and concurrency models, distributed state management, resilience strategies, and future multi-tenant scaling roadmaps of the **Distributed Multi-Stack Rate Limiter**.

---

## 1. High-Level System Architecture

The system is engineered as an L7 rate-limiting and traffic governance layer available in two native technology stacks: **C++17** and **Node.js (Express)**. Both stacks implement identical REST contracts, standardized rate-limiting headers, and distributed Redis coordination.

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

### Request Flow Lifecycle
1. **Client Ingestion**: An incoming HTTP request hits `/api/rate-limit/{algorithm}`.
2. **Client Identification**: The client identifier is resolved using the precedence chain:
   $$\text{Client ID} = \text{Header}(X\text{-Client-Id}) \parallel \text{QueryParam}(clientId) \parallel \text{Remote IP}$$
3. **Distributed Rate Limiting Check**:
   - The server invokes an atomic Lua script in Redis.
   - Redis evaluates the request against current quota in $O(1)$ time, increments or logs the event, updates TTL, and returns an atomic payload: `[allowed, limit, remaining, reset_timestamp, retry_after]`.
4. **Fallback Handling**: If Redis fails or times out, the server automatically routes the request through a thread-safe local in-memory rate limiter without throwing a 500 error.
5. **Header Enforcement**: Standard response headers (`X-RateLimit-*`) are appended.
6. **Dispatch or Throttle**:
   - If `allowed == true`: Returns `200 OK`.
   - If `allowed == false`: Returns `429 Too Many Requests` with a `Retry-After` header.

---

## 2. Concurrency & Threading Models: C++ vs. Node.js

Both engines solve high-concurrency request governance using contrasting concurrency primitives:

| Dimension | C++17 Implementation | Node.js Implementation |
| :--- | :--- | :--- |
| **I/O Model** | Multi-threaded blocking/non-blocking socket pool (`cpp-httplib`) | Single-threaded asynchronous Event Loop (`libuv`) |
| **Worker Model** | Pre-allocated ThreadPool (default: 128 workers) | Non-blocking asynchronous event dispatch |
| **State Synchronization** | `std::mutex` per algorithm bucket in memory fallback | Event Loop single-threaded sequential execution |
| **Redis Connectivity** | Custom native Winsock RESP protocol client | High-performance `ioredis` driver |
| **p50 Latency (200 Reqs)** | **4.96 ms - 5.72 ms** | **7.76 ms - 17.81 ms** |
| **Peak Throughput** | **2,213+ RPS** | **1,830+ RPS** |

### Why C++ Outperforms Node.js in Rate Limiting
1. **Zero-Copy Memory Access**: Minimal allocations and cache-friendly struct layouts.
2. **Direct OS Sockets**: Avoids V8 JavaScript garbage collection pauses and event loop tick latency.
3. **Static Dispatch**: Algorithm objects and route mappings are bound at compile time.

---

## 3. Resilience: Automatic In-Memory Fallback

Distributed systems must be engineered for partial network partitions. If Redis crashes or network latency spikes, naive rate limiters fail completely.

### The Self-Healing Dual-Layer Strategy
1. **Primary State**: Centralized Redis instance with atomic Lua evaluation.
2. **Fallback State**:
   - **C++**: Thread-safe in-memory maps guarded by `std::mutex` with microsecond-resolution clock references (`std::chrono::steady_clock`).
   - **Node.js**: Thread-safe JavaScript `Map` structures tracking bucket states.
3. **Automatic Reconnection**: As soon as Redis becomes reachable again, requests seamlessly transition back to distributed state without server restarts.

### Architectural Decision: Fail-Open vs. Fail-Closed

```
                        Network Partition / Redis Failure
                                        │
                    ┌───────────────────┴───────────────────┐
                    ▼                                       ▼
             [Fail-Open]                             [Fail-Closed]
    - Permit requests through               - Reject all traffic (500 / 429)
    - Prioritizes API Availability          - Prioritizes Backend Protection
    - Risk: Database saturation             - Risk: Complete business outage
                    │                                       │
                    └───────────────────┬───────────────────┘
                                        ▼
                        [Our Approach: Resilient Fallback]
                 Switch to Local In-Memory Limiting per Instance
```

> [!NOTE]
> GatePulse/RateLimiter adopts **Resilient In-Memory Fallback**. Instead of an extreme binary choice between Fail-Open (risking database death) or Fail-Closed (causing total outage), each server instance throttles traffic locally according to its memory store until Redis recovers.

---

## 4. Multi-Tenant Role-Based Rate Limiting (RBAC) Scaling

In enterprise systems, rate limits must differentiate between anonymous traffic, free tiers, paying customers, and internal microservices.

### 1. Conceptual Hierarchy

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

### 3. Hierarchical Redis Key Topology
To prevent key collisions across tenants:
```
rl:rbac:{tenant_id}:{role_tier}:{user_id}:{algorithm}:{window_id}
```
Examples:
- `rl:rbac:acme_corp:pro:user_9821:tb`
- `rl:rbac:global:free:user_102:swcnt:179189520`
