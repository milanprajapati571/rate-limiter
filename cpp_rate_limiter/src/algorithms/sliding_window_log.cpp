#include "algorithms.hpp"
#include "memory_storage.hpp"
#include <chrono>
#include <random>

static const char* SLIDING_LOG_LUA = R"(
local key = KEYS[1]
local now = tonumber(ARGV[1])
local windowMs = tonumber(ARGV[2])
local limit = tonumber(ARGV[3])
local member = ARGV[4]

local clearBefore = now - windowMs
redis.call('ZREMRANGEBYSCORE', key, '-inf', clearBefore)

local currentCount = redis.call('ZCARD', key)
if currentCount < limit then
    redis.call('ZADD', key, now, member)
    redis.call('EXPIRE', key, math.ceil(windowMs / 1000) + 2)
    return {1, limit - currentCount - 1, 0}
else
    local oldest = redis.call('ZRANGE', key, 0, 0, 'WITHSCORES')
    local retryAfterMs = 1000
    if oldest and #oldest >= 2 then
        local oldestScore = tonumber(oldest[2])
        retryAfterMs = math.max(1, (oldestScore + windowMs) - now)
    end
    return {0, 0, math.ceil(retryAfterMs / 1000)}
end
)";

SlidingWindowLogRateLimiter::SlidingWindowLogRateLimiter(int limit, int window_seconds, std::shared_ptr<RedisClient> redis)
    : limit_(limit), window_seconds_(window_seconds), redis_(redis) {}

RateLimitResult SlidingWindowLogRateLimiter::check(const std::string& key) {
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    long long window_ms = static_cast<long long>(window_seconds_) * 1000LL;
    long long reset_epoch = (now_ms + window_ms + 999) / 1000;

    if (redis_ && redis_->is_connected()) {
        std::string redis_key = "rl:cpp:swlog:" + key;
        static thread_local std::mt19937_64 rng(std::random_device{}());
        std::string member = std::to_string(now_ms) + ":" + std::to_string(rng());
        auto reply = redis_->eval(SLIDING_LOG_LUA, 1, {redis_key}, {
            std::to_string(now_ms),
            std::to_string(window_ms),
            std::to_string(limit_),
            member
        });

        if (reply.type == RedisReply::Type::Array && reply.elements.size() >= 3) {
            bool allowed = reply.elements[0].integer_val == 1;
            int remaining = static_cast<int>(reply.elements[1].integer_val);
            int retry_after = static_cast<int>(reply.elements[2].integer_val);
            return {allowed, limit_, std::max(0, remaining), reset_epoch, allowed ? 0 : std::max(1, retry_after)};
        }
    }

    // In-memory fallback
    auto [current_count, retry_after_ms] = MemoryStorage::instance().check_sliding_log(key, now_ms, window_ms, limit_);
    RateLimitResult res;
    res.limit = limit_;
    res.reset = reset_epoch;
    if (retry_after_ms == 0) {
        res.allowed = true;
        res.remaining = std::max(0, limit_ - current_count);
        res.retry_after = 0;
    } else {
        res.allowed = false;
        res.remaining = 0;
        res.retry_after = static_cast<int>(std::max(1LL, (retry_after_ms + 999) / 1000));
    }
    return res;
}
