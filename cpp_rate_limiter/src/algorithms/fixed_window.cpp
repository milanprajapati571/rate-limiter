#include "algorithms.hpp"
#include "memory_storage.hpp"
#include <chrono>

static const char* FIXED_WINDOW_LUA = R"(
local key = KEYS[1]
local limit = tonumber(ARGV[1])
local windowSec = tonumber(ARGV[2])

local count = redis.call('INCR', key)
if count == 1 then
    redis.call('EXPIRE', key, windowSec + 1)
end

if count <= limit then
    return {1, limit - count, 0}
else
    local ttl = redis.call('TTL', key)
    if ttl < 0 then ttl = windowSec end
    return {0, 0, ttl}
end
)";

FixedWindowRateLimiter::FixedWindowRateLimiter(int limit, int window_seconds, std::shared_ptr<RedisClient> redis)
    : limit_(limit), window_seconds_(window_seconds), redis_(redis) {}

RateLimitResult FixedWindowRateLimiter::check(const std::string& key) {
    auto now_epoch = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    long long window_id = now_epoch / window_seconds_;
    long long window_reset = (window_id + 1) * window_seconds_;

    if (redis_ && redis_->is_connected()) {
        std::string redis_key = "rl:cpp:fw:" + key + ":" + std::to_string(window_id);
        auto reply = redis_->eval(FIXED_WINDOW_LUA, 1, {redis_key}, {std::to_string(limit_), std::to_string(window_seconds_)});
        if (reply.type == RedisReply::Type::Array && reply.elements.size() >= 3) {
            bool allowed = reply.elements[0].integer_val == 1;
            int remaining = static_cast<int>(reply.elements[1].integer_val);
            int retry_after = static_cast<int>(reply.elements[2].integer_val);
            return {allowed, limit_, std::max(0, remaining), window_reset, allowed ? 0 : std::max(1, retry_after)};
        }
    }

    // Fallback to in-memory
    auto [count, allowed] = MemoryStorage::instance().check_fixed_window(key, window_id, limit_);
    RateLimitResult res;
    res.allowed = allowed;
    res.limit = limit_;
    res.remaining = allowed ? std::max(0, limit_ - count) : 0;
    res.reset = window_reset;
    res.retry_after = allowed ? 0 : static_cast<int>(std::max(1LL, window_reset - now_epoch));
    return res;
}
