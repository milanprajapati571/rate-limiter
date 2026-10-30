# 04. Setup & Execution Guide

This guide provides end-to-end instructions for building, running, and benchmarking both the C++ and Node.js rate-limiting engines.

---

## 1. Prerequisites

Before getting started, ensure the following toolchains are installed:

| Component | Minimum Version | Recommended | Notes |
| :--- | :--- | :--- | :--- |
| **C++ Compiler** | C++17 support | Visual Studio 2022 (MSVC) or MinGW GCC 11+ | Windows / Linux compatible |
| **CMake** | 3.15+ | 3.25+ | Required to configure C++ build |
| **Node.js** | v18.0.0+ | v22.x LTS | Includes `npm` |
| **Python** | 3.8+ | 3.11+ / 3.12+ | Zero external pip dependencies required |
| **Redis** | 6.0+ | 7.x (Alpine) | Required for distributed state (optional) |

---

## 2. Redis Setup (Optional)

Both servers feature an **automatic in-memory fallback**. If Redis is not running, the servers will automatically serve requests in local memory without crashing.

To run Redis distributed state via Docker:
```powershell
docker run -d --name rate-limiter-redis -p 6379:6379 redis:alpine
```

To verify Redis connectivity:
```powershell
# Using redis-cli or docker exec
docker exec -it rate-limiter-redis redis-cli ping
# Response: PONG
```

---

## 3. Building & Running the C++ Server

### Option A: Using the One-Click Build Script (Recommended)
From the repository root:
```powershell
.\build_cpp.bat
```
Then start the executable:
```powershell
.\cpp_rate_limiter\build\Release\cpp_rate_limiter.exe
```

### Option B: Manual CMake Commands
1. Navigate to the C++ project directory:
   ```powershell
   cd cpp_rate_limiter
   ```
2. Configure CMake:
   ```powershell
   cmake -B build -S .
   ```
3. Compile Release binary:
   ```powershell
   cmake --build build --config Release
   ```
4. Launch server:
   ```powershell
   .\build\Release\cpp_rate_limiter.exe
   ```

* **Default Port**: `8080`
* **Health Check**: `http://localhost:8080/health`

---

## 4. Running the Node.js Server

1. Open PowerShell and navigate to `node_rate_limiter/`:
   ```powershell
   cd node_rate_limiter
   ```
2. Install dependencies:
   ```powershell
   npm install
   ```
3. Start the server:
   ```powershell
   npm start
   ```

* **Default Port**: `3000`
* **Health Check**: `http://localhost:3000/health`

---

## 5. Running the Python Validation & Benchmark Suite

The automated test runner (`tests/benchmark.py`) requires zero third-party dependencies and runs out of the box using Python's standard library.

### Option A: Using the One-Click Benchmark Script
From the repository root:
```powershell
# Quick functional assertion test
.\run_benchmark.bat --validate-only

# High-concurrency load test (50 workers, 200 requests)
.\run_benchmark.bat --concurrency 50 --requests 200
```

### Option B: Direct Python CLI Commands
```powershell
# 1. Quick functional assertion checks against all algorithms on both servers
python tests/benchmark.py --validate-only

# 2. Concurrency load test (50 worker threads, 200 requests per algorithm)
python tests/benchmark.py --concurrency 50 --requests 200

# 3. Test a specific algorithm on C++ only
python tests/benchmark.py --servers cpp --algorithm token-bucket

# 4. Heavy stress test (100 concurrent workers, 500 requests)
python tests/benchmark.py --concurrency 100 --requests 500
```

---

## 6. Verifying Health & Storage Modes

You can test either running server using `curl`:

```powershell
# Check C++ server health
curl http://localhost:8080/health

# Sample response:
# {"status":"healthy","server":"C++ httplib","redis":true,"mode":"Redis Distributed"}

# Check Node.js server health
curl http://localhost:3000/health

# Sample response:
# {"status":"healthy","server":"Node.js Express","redis":true,"mode":"Redis Distributed"}
```
