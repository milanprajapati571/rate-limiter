#pragma once

#include <string>
#include <chrono>

struct RateLimitResult {
    bool allowed{false};
    int limit{0};
    int remaining{0};
    long long reset{0};       // Unix epoch timestamp in seconds
    int retry_after{0};       // Seconds to wait before retry
};

class IRateLimiter {
public:
    virtual ~IRateLimiter() = default;
    virtual RateLimitResult check(const std::string& key) = 0;
    virtual std::string get_name() const = 0;
};
