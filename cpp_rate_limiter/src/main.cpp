#include "httplib.h"
#include "algorithms.hpp"
#include "redis_client.hpp"
#include <iostream>
#include <memory>
#include <sstream>

static std::string extract_client_id(const httplib::Request& req) {
    if (req.has_header("X-Client-Id")) {
        return req.get_header_value("X-Client-Id");
    }
    if (req.has_param("clientId")) {
        return req.get_param_value("clientId");
    }
    if (!req.remote_addr.empty()) {
        return req.remote_addr;
    }
    return "default-client";
}

static void handle_rate_limit_request(IRateLimiter& limiter, const httplib::Request& req, httplib::Response& res) {
    std::string client_id = extract_client_id(req);
    RateLimitResult result = limiter.check(client_id);

    res.set_header("X-RateLimit-Limit", std::to_string(result.limit));
    res.set_header("X-RateLimit-Remaining", std::to_string(result.remaining));
    res.set_header("X-RateLimit-Reset", std::to_string(result.reset));

    if (!result.allowed) {
        res.status = 429;
        res.set_header("Retry-After", std::to_string(result.retry_after));
        std::ostringstream json;
        json << "{"
             << "\"status\":\"error\","
             << "\"code\":429,"
             << "\"message\":\"Too Many Requests: Rate limit exceeded for algorithm '" << limiter.get_name() << "'\","
             << "\"algorithm\":\"" << limiter.get_name() << "\","
             << "\"retryAfter\":" << result.retry_after << ","
             << "\"reset\":" << result.reset << ","
             << "\"server\":\"C++ httplib\""
             << "}";
        res.set_content(json.str(), "application/json");
    } else {
        res.status = 200;
        std::ostringstream json;
        json << "{"
             << "\"status\":\"success\","
             << "\"code\":200,"
             << "\"message\":\"Request successfully processed under '" << limiter.get_name() << "' rate limiter.\","
             << "\"algorithm\":\"" << limiter.get_name() << "\","
             << "\"rateLimit\":{"
             << "\"allowed\":true,"
             << "\"limit\":" << result.limit << ","
             << "\"remaining\":" << result.remaining << ","
             << "\"reset\":" << result.reset << ","
             << "\"retryAfter\":0"
             << "},"
             << "\"server\":\"C++ httplib\""
             << "}";
        res.set_content(json.str(), "application/json");
    }
}

int main(int argc, char* argv[]) {
    int port = 8080;
    const char* env_port = std::getenv("PORT");
    if (env_port) {
        try { port = std::stoi(env_port); } catch (...) {}
    }

    std::string redis_host = "127.0.0.1";
    const char* env_host = std::getenv("REDIS_HOST");
    if (env_host) redis_host = env_host;

    int redis_port = 6379;
    const char* env_rport = std::getenv("REDIS_PORT");
    if (env_rport) {
        try { redis_port = std::stoi(env_rport); } catch (...) {}
    }

    auto redis = std::make_shared<RedisClient>(redis_host, redis_port);
    bool redis_ok = redis->connect();

    FixedWindowRateLimiter fixed_window(10, 10, redis);
    SlidingWindowLogRateLimiter sliding_log(10, 10, redis);
    SlidingWindowCounterRateLimiter sliding_counter(10, 10, redis);
    TokenBucketRateLimiter token_bucket(10, 2.0, redis);
    LeakyBucketRateLimiter leaky_bucket(10, 2.0, redis);

    httplib::Server svr;
    svr.new_task_queue = [] { return new httplib::ThreadPool(128); };

    // Health check
    svr.Get("/health", [redis](const httplib::Request&, httplib::Response& res) {
        bool connected = redis && redis->is_connected();
        std::ostringstream json;
        json << "{"
             << "\"status\":\"healthy\","
             << "\"server\":\"cpp-rate-limiter\","
             << "\"redisConnected\":" << (connected ? "true" : "false") << ","
             << "\"storageMode\":\"" << (connected ? "distributed-redis" : "in-memory-fallback") << "\""
             << "}";
        res.status = 200;
        res.set_content(json.str(), "application/json");
    });

    // Endpoints (both POST and GET supported for flexibility)
    auto bind_limiter = [&](const std::string& path, IRateLimiter& limiter) {
        svr.Post(path, [&](const httplib::Request& req, httplib::Response& res) {
            handle_rate_limit_request(limiter, req, res);
        });
        svr.Get(path, [&](const httplib::Request& req, httplib::Response& res) {
            handle_rate_limit_request(limiter, req, res);
        });
    };

    bind_limiter("/api/rate-limit/fixed-window", fixed_window);
    bind_limiter("/api/rate-limit/sliding-window-log", sliding_log);
    bind_limiter("/api/rate-limit/sliding-window-counter", sliding_counter);
    bind_limiter("/api/rate-limit/token-bucket", token_bucket);
    bind_limiter("/api/rate-limit/leaky-bucket", leaky_bucket);

    std::cout << "====================================================" << std::endl;
    std::cout << " C++ Rate Limiter running on port " << port << std::endl;
    std::cout << " Storage mode: " << (redis_ok ? "Redis Distributed" : "In-Memory Fallback") << std::endl;
    std::cout << " Health check: http://localhost:" << port << "/health" << std::endl;
    std::cout << "====================================================" << std::endl;

    svr.listen("0.0.0.0", port);
    return 0;
}
