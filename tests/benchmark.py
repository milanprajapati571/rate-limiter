#!/usr/bin/env python3
"""
Comprehensive Concurrent Rate Limiter Benchmark & Validation Suite
Validates rate limiting algorithms across C++ and Node.js implementations.
"""

import argparse
import concurrent.futures
import json
import math
import os
import sys
import time
import urllib.error
import urllib.request
import uuid

# Terminal formatting
GREEN = "\033[92m"
RED = "\033[91m"
YELLOW = "\033[93m"
CYAN = "\033[96m"
BOLD = "\033[1m"
RESET = "\033[0m"

ALGORITHMS = [
    ("fixed-window", "Fixed Window Counter"),
    ("sliding-window-log", "Sliding Window Log"),
    ("sliding-window-counter", "Sliding Window Counter"),
    ("token-bucket", "Token Bucket"),
    ("leaky-bucket", "Leaky Bucket"),
]


class RateLimitRequestResult:
    def __init__(self, status_code, latency_ms, headers=None, error=None):
        self.status_code = status_code
        self.latency_ms = latency_ms
        self.headers = headers or {}
        self.error = error

    @property
    def is_success(self):
        return self.status_code == 200

    @property
    def is_rate_limited(self):
        return self.status_code == 429


def send_http_request(url, client_id=None, method="POST", timeout=5.0):
    headers = {
        "User-Agent": "RateLimiterBenchmark/1.0",
        "Content-Type": "application/json",
    }
    if client_id:
        headers["X-Client-Id"] = client_id

    req = urllib.request.Request(url, data=b"{}", headers=headers, method=method)
    start_time = time.perf_counter()

    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            elapsed_ms = (time.perf_counter() - start_time) * 1000.0
            resp_headers = dict(resp.info())
            return RateLimitRequestResult(resp.status, elapsed_ms, resp_headers)
    except urllib.error.HTTPError as e:
        elapsed_ms = (time.perf_counter() - start_time) * 1000.0
        resp_headers = dict(e.headers)
        return RateLimitRequestResult(e.code, elapsed_ms, resp_headers)
    except Exception as e:
        elapsed_ms = (time.perf_counter() - start_time) * 1000.0
        return RateLimitRequestResult(0, elapsed_ms, {}, str(e))


def check_server_health(base_url, name):
    health_url = f"{base_url}/health"
    try:
        res = send_http_request(health_url, method="GET", timeout=2.0)
        if res.status_code == 200:
            print(f"[{GREEN}ONLINE{RESET}] {BOLD}{name}{RESET} at {base_url}")
            return True
        else:
            print(f"[{RED}OFFLINE{RESET}] {BOLD}{name}{RESET} returned status {res.status_code}")
            return False
    except Exception as e:
        print(f"[{RED}OFFLINE{RESET}] {BOLD}{name}{RESET} connection failed: {e}")
        return False


def validate_algorithm_functionality(base_url, server_name, algo_slug, algo_name):
    """
    Validates that:
    1. Initial requests return 200 OK.
    2. Headers (X-RateLimit-Limit, Remaining, Reset) are returned.
    3. Requests exceeding limit return 429 Too Many Requests with Retry-After header.
    """
    url = f"{base_url}/api/rate-limit/{algo_slug}"
    test_client_id = f"test-val-{algo_slug}-{uuid.uuid4().hex[:6]}"
    
    # Send rapid burst of 15 requests (configured limit is 10)
    results = []
    for _ in range(15):
        results.append(send_http_request(url, client_id=test_client_id, method="POST"))

    success_count = sum(1 for r in results if r.is_success)
    rate_limited_count = sum(1 for r in results if r.is_rate_limited)

    # Check header conformity
    first_res = results[0]
    has_limit_header = "x-ratelimit-limit" in [k.lower() for k in first_res.headers]
    has_remaining_header = "x-ratelimit-remaining" in [k.lower() for k in first_res.headers]
    has_reset_header = "x-ratelimit-reset" in [k.lower() for k in first_res.headers]

    first_429 = next((r for r in results if r.is_rate_limited), None)
    has_retry_after = False
    if first_429:
        has_retry_after = "retry-after" in [k.lower() for k in first_429.headers]

    headers_valid = has_limit_header and has_remaining_header and has_reset_header
    enforcement_valid = (success_count <= 10) and (rate_limited_count > 0)

    is_passed = headers_valid and enforcement_valid and (has_retry_after if rate_limited_count > 0 else True)
    status_tag = f"{GREEN}PASS{RESET}" if is_passed else f"{RED}FAIL{RESET}"

    return {
        "algo_slug": algo_slug,
        "algo_name": algo_name,
        "server": server_name,
        "passed": is_passed,
        "status_tag": status_tag,
        "success_count": success_count,
        "rate_limited_count": rate_limited_count,
        "headers_valid": headers_valid,
        "has_retry_after": has_retry_after,
    }


def run_concurrency_benchmark(base_url, server_name, algo_slug, algo_name, total_requests, concurrency):
    """
    Runs concurrent load test against the specified algorithm endpoint.
    """
    url = f"{base_url}/api/rate-limit/{algo_slug}"
    # Use multiple client IDs across threads to simulate real distributed traffic
    num_unique_clients = max(1, concurrency // 5)
    client_ids = [f"bench-c{i}-{uuid.uuid4().hex[:4]}" for i in range(num_unique_clients)]

    results = []
    start_time = time.perf_counter()

    with concurrent.futures.ThreadPoolExecutor(max_workers=concurrency) as executor:
        futures = []
        for i in range(total_requests):
            cid = client_ids[i % len(client_ids)]
            futures.append(executor.submit(send_http_request, url, cid, "POST"))

        for f in concurrent.futures.as_completed(futures):
            results.append(f.result())

    total_duration_sec = time.perf_counter() - start_time
    rps = total_requests / total_duration_sec if total_duration_sec > 0 else 0

    latencies = [r.latency_ms for r in results if r.latency_ms > 0]
    latencies.sort()

    p50 = latencies[int(len(latencies) * 0.50)] if latencies else 0
    p95 = latencies[int(len(latencies) * 0.95)] if latencies else 0
    p99 = latencies[int(len(latencies) * 0.99)] if latencies else 0
    avg_lat = sum(latencies) / len(latencies) if latencies else 0

    success_count = sum(1 for r in results if r.is_success)
    rate_limited_count = sum(1 for r in results if r.is_rate_limited)
    error_count = sum(1 for r in results if r.status_code == 0)

    return {
        "server": server_name,
        "algo_name": algo_name,
        "algo_slug": algo_slug,
        "total": total_requests,
        "duration_sec": total_duration_sec,
        "rps": rps,
        "success": success_count,
        "rate_limited": rate_limited_count,
        "errors": error_count,
        "p50": p50,
        "p95": p95,
        "p99": p99,
        "avg_lat": avg_lat,
    }


def print_validation_table(val_results):
    print("\n" + "=" * 80)
    print(f"{BOLD}ALGORITHM VALIDATION & ENFORCEMENT AUDIT{RESET}".center(80))
    print("=" * 80)
    header = f"{'Server':<14} | {'Algorithm':<24} | {'Allowed':<7} | {'Blocked':<7} | {'Headers':<7} | {'Status':<6}"
    print(header)
    print("-" * 80)
    for r in val_results:
        print(
            f"{r['server']:<14} | "
            f"{r['algo_name']:<24} | "
            f"{r['success_count']:<7} | "
            f"{r['rate_limited_count']:<7} | "
            f"{'OK' if r['headers_valid'] else 'MISSING':<7} | "
            f"{r['status_tag']:<6}"
        )
    print("=" * 80)


def print_benchmark_table(bench_results):
    print("\n" + "=" * 94)
    print(f"{BOLD}CONCURRENCY LOAD & LATENCY BENCHMARK RESULTS{RESET}".center(94))
    print("=" * 94)
    header = f"{'Server':<12} | {'Algorithm':<22} | {'Reqs':<6} | {'RPS':<8} | {'p50(ms)':<8} | {'p95(ms)':<8} | {'p99(ms)':<8} | {'200/429 Ratio'}"
    print(header)
    print("-" * 94)
    for r in bench_results:
        ratio = f"{r['success']}/{r['rate_limited']}"
        print(
            f"{r['server']:<12} | "
            f"{r['algo_name']:<22} | "
            f"{r['total']:<6} | "
            f"{r['rps']:<8.1f} | "
            f"{r['p50']:<8.2f} | "
            f"{r['p95']:<8.2f} | "
            f"{r['p99']:<8.2f} | "
            f"{ratio}"
        )
    print("=" * 94)


def main():
    parser = argparse.ArgumentParser(description="Rate Limiter Benchmark & Validation Suite")
    parser.add_argument("--cpp-url", default="http://127.0.0.1:8080", help="C++ rate limiter URL")
    parser.add_argument("--node-url", default="http://127.0.0.1:3000", help="Node.js rate limiter URL")
    parser.add_argument("--concurrency", type=int, default=50, help="Number of concurrent workers (default: 50)")
    parser.add_argument("--requests", type=int, default=200, help="Total requests per test run (default: 200)")
    parser.add_argument("--servers", choices=["cpp", "node", "both"], default="both", help="Target server(s)")
    parser.add_argument(
        "--algorithm",
        choices=["all", "fixed-window", "sliding-window-log", "sliding-window-counter", "token-bucket", "leaky-bucket"],
        default="all",
        help="Algorithm to test",
    )
    parser.add_argument("--validate-only", action="store_true", help="Run validation tests only without load testing")

    args = parser.parse_args()

    print(f"\n{BOLD}{CYAN}=== Rate Limiter Benchmark & Verification Suite ==={RESET}")
    print(f"Concurrency: {args.concurrency} workers | Requests per test: {args.requests}")

    # Health checks
    servers = []
    if args.servers in ["cpp", "both"]:
        if check_server_health(args.cpp_url, "C++ httplib"):
            servers.append(("C++ Server", args.cpp_url))
    if args.servers in ["node", "both"]:
        if check_server_health(args.node_url, "Node.js Express"):
            servers.append(("Node Server", args.node_url))

    if not servers:
        print(f"\n{RED}Error: No target servers are online. Please start the servers before benchmarking.{RESET}")
        sys.exit(1)

    target_algos = ALGORITHMS if args.algorithm == "all" else [a for a in ALGORITHMS if a[0] == args.algorithm]

    # Phase A: Functional Validation
    print(f"\n{YELLOW}[Phase 1] Running functional assertion tests...{RESET}")
    val_results = []
    for s_name, s_url in servers:
        for slug, name in target_algos:
            val_results.append(validate_algorithm_functionality(s_url, s_name, slug, name))

    print_validation_table(val_results)

    # Check if all validations passed
    all_passed = all(r["passed"] for r in val_results)
    if not all_passed:
        print(f"\n{RED}Warning: Some validation assertions failed! Check rate limiter logic.{RESET}")

    # Phase B: Concurrency Load Benchmark
    if not args.validate_only:
        print(f"\n{YELLOW}[Phase 2] Running concurrent load benchmarks ({args.concurrency} threads, {args.requests} requests each)...{RESET}")
        bench_results = []
        for s_name, s_url in servers:
            for slug, name in target_algos:
                bench_results.append(
                    run_concurrency_benchmark(
                        s_url, s_name, slug, name, total_requests=args.requests, concurrency=args.concurrency
                    )
                )

        print_benchmark_table(bench_results)

    print(f"\n{GREEN}{BOLD}Benchmark & Validation completed successfully!{RESET}\n")


if __name__ == "__main__":
    main()
