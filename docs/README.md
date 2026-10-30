# Distributed Rate Limiter: Technical Documentation Hub

Welcome to the comprehensive technical documentation for the **Distributed Multi-Stack Rate Limiter (C++17 & Node.js)**. 

Every architectural decision, mathematical proof, concurrency pattern, and benchmark result is thoroughly documented across six dedicated chapters below.

---

## 📚 Documentation Chapters

| # | Chapter Document | Focus Area & Key Topics |
| :---: | :--- | :--- |
| **01** | [Architecture & System Design](01_architecture_and_system_design.md) | Dual-stack design (C++ ThreadPool vs. Node EventLoop), automatic in-memory fallback, fail-open vs. fail-closed trade-offs, and multi-tenant RBAC scaling roadmap. |
| **02** | [Rate Limiting Algorithms Deep Dive](02_rate_limiting_algorithms.md) | Mathematical formulas, $O(1)$ vs $O(N)$ Big-O complexity, boundary burst vulnerability proofs ($2M/T$), burst handling trade-offs, master comparison matrix, and algorithm selection tree. |
| **03** | [Distributed State & Redis Lua Scripts](03_distributed_state_and_redis_lua.md) | Distributed race conditions (Check-Then-Act anti-pattern), atomic 1-RTT Redis Lua scripts, RESP protocol parsing, and automatic TTL expiration. |
| **04** | [Setup & Execution Guide](04_setup_and_execution_guide.md) | Environment prerequisites, Docker Redis deployment, building C++ with CMake/MSVC, running Express, and executing the Python automated benchmark suite. |
| **05** | [API Specification & Contract](05_api_specification_and_contract.md) | Standard REST endpoints, `X-Client-Id` resolution hierarchy, RFC rate-limiting headers, JSON schemas for `200 OK` and `429 Too Many Requests`, and cURL commands. |
| **06** | [Benchmarks & Performance Analysis](06_benchmarks_and_performance_analysis.md) | High-concurrency benchmark results (50 worker threads, 200 requests), p50/p95/p99 latency distributions, RPS throughput comparison (C++ 2,213 RPS vs Node 1,830 RPS), and audit tables. |

---

## 🧭 Suggested Reading Paths

### 1. For System Design & Architecture Reviewers
1. Start with [01. Architecture & System Design](01_architecture_and_system_design.md) to understand the dual-stack topology and resilience layers.
2. Review [03. Distributed State & Redis Lua Scripts](03_distributed_state_and_redis_lua.md) for atomicity and concurrency guarantees.
3. Review [06. Benchmarks & Performance Analysis](06_benchmarks_and_performance_analysis.md) for empirical latency and throughput metrics.

### 2. For Algorithmic & Computer Science Reviewers
1. Study [02. Rate Limiting Algorithms Deep Dive](02_rate_limiting_algorithms.md) for mathematical proofs of boundary flaws, token bucket refill dynamics, and Big-O complexities.
2. Read the master comparison matrix to evaluate trade-offs between burst tolerance and traffic smoothing.

### 3. For Developers & Evaluators
1. Follow [04. Setup & Execution Guide](04_setup_and_execution_guide.md) to build and run the services locally.
2. Test the API contracts and response headers in [05. API Specification & Contract](05_api_specification_and_contract.md).
