#include "algorithms.hpp"
#include "memory_storage.hpp"
#include <chrono>
#include <cmath>

static const char* SLIDING_COUNTER_LUA = R"(
local currKey = KEYS[1]
local prevKey = KEYS[2]
local limit = tonumber(ARGV[1])
local windowSec = tonumber(ARGV[2])
local overlapRatio = tonumber(ARGV[3])
local windowReset = tonumber(ARGV[4])

local prevCount = tonumber(redis.call('GET', prevKey) or '0')
local currCount = tonumber(redis.call('GET', currKey) or '0')

local estimated = math.floor(prevCount * (1 - overlapRatio) + currCount)

if estimated < limit then
    local newCount = redis.call('INCR', currKey)
    if newCount == 1 then
        redis.call('EXPIRE', currKey, windowSec * 2)
    end
    return {1, math.max(0, limit - estimated - 1), 0}
else
    local ttl = redis.call('TTL', currKey)
    if ttl < 0 then ttl = windowSec end
    return {0, 0, math.max(1, ttl)}
end
)";

SlidingWindowCounterRateLimiter::SlidingWindowCounterRateLimiter(int limit, int window_seconds, std::shared_ptr<RedisClient> redis)
    : limit_(limit), window_seconds_(window_seconds), redis_(redis) {}

RateLimitResult SlidingWindowCounterRateLimiter::check(const std::string& key) {
    auto now_epoch = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    long long window_id = now_epoch / window_seconds_;
    long long time_in_curr_window = now_epoch % window_seconds_;
    double overlap_ratio = static_cast<double>(time_in_curr_window) / window_seconds_;
    long long window_reset = (window_id + 1) * window_seconds_;

    if (redis_ && redis_->is_connected()) {
        std::string curr_key = "rl:cpp:swcnt:" + key + ":" + std::to_string(window_id);
        std::string prev_key = "rl:cpp:swcnt:" + key + ":" + std::to_string(window_id - 1);
        auto reply = redis_->eval(SLIDING_COUNTER_LUA, 2, {curr_key, prev_key}, {
            std::to_string(limit_),
            std::to_string(window_seconds_),
            std::to_string(overlap_ratio),
            std::to_string(window_reset)
        });

        if (reply.type == RedisReply::Type::Array && reply.elements.size() >= 3) {
            bool allowed = reply.elements[0].integer_val == 1;
            int remaining = static_cast<int>(reply.elements[1].integer_val);
            int retry_after = static_cast<int>(reply.elements[2].integer_val);
            return {allowed, limit_, std::max(0, remaining), window_reset, allowed ? 0 : std::max(1, retry_after)};
        }
    }

    // In-memory fallback
    auto [estimated_count, allowed] = MemoryStorage::instance().check_sliding_counter(key, window_id, overlap_ratio, limit_);
    RateLimitResult res;
    res.allowed = allowed;
    res.limit = limit_;
    res.remaining = allowed ? std::max(0, limit_ - estimated_count) : 0;
    res.reset = window_reset;
    res.retry_after = allowed ? 0 : static_cast<int>(std::max(1LL, window_reset - now_epoch));
    return res;
}
