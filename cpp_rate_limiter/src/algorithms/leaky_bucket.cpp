#include "algorithms.hpp"
#include "memory_storage.hpp"
#include <chrono>
#include <cmath>

static const char* LEAKY_BUCKET_LUA = R"(
local key = KEYS[1]
local capacity = tonumber(ARGV[1])
local leakRate = tonumber(ARGV[2])
local now = tonumber(ARGV[3])

local data = redis.call('HMGET', key, 'water', 'lastLeak')
local water = tonumber(data[1])
local lastLeak = tonumber(data[2])

if water == nil then
    water = 0
    lastLeak = now
else
    local delta = math.max(0, now - lastLeak)
    water = math.max(0, water - delta * leakRate)
    lastLeak = now
end

if water + 1 <= capacity then
    water = water + 1
    redis.call('HMSET', key, 'water', water, 'lastLeak', lastLeak)
    redis.call('EXPIRE', key, math.ceil(capacity / leakRate) + 10)
    return {1, math.floor(capacity - water), 0}
else
    redis.call('HMSET', key, 'water', water, 'lastLeak', lastLeak)
    redis.call('EXPIRE', key, math.ceil(capacity / leakRate) + 10)
    local retryAfter = math.ceil((water + 1 - capacity) / leakRate)
    return {0, 0, math.max(1, retryAfter)}
end
)";

LeakyBucketRateLimiter::LeakyBucketRateLimiter(int capacity, double leak_rate, std::shared_ptr<RedisClient> redis)
    : capacity_(capacity), leak_rate_(leak_rate), redis_(redis) {}

RateLimitResult LeakyBucketRateLimiter::check(const std::string& key) {
    auto now_epoch_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    double now_sec = static_cast<double>(now_epoch_ms) / 1000.0;

    if (redis_ && redis_->is_connected()) {
        std::string redis_key = "rl:cpp:lb:" + key;
        auto reply = redis_->eval(LEAKY_BUCKET_LUA, 1, {redis_key}, {
            std::to_string(capacity_),
            std::to_string(leak_rate_),
            std::to_string(now_sec)
        });

        if (reply.type == RedisReply::Type::Array && reply.elements.size() >= 3) {
            bool allowed = reply.elements[0].integer_val == 1;
            int remaining = static_cast<int>(reply.elements[1].integer_val);
            int retry_after = static_cast<int>(reply.elements[2].integer_val);
            long long reset = static_cast<long long>(std::ceil(now_sec + ((capacity_ - remaining) / leak_rate_)));
            return {allowed, capacity_, std::max(0, remaining), reset, allowed ? 0 : std::max(1, retry_after)};
        }
    }

    // In-memory fallback
    auto [remaining_capacity, retry_after] = MemoryStorage::instance().check_leaky_bucket(key, now_sec, capacity_, leak_rate_);
    RateLimitResult res;
    res.limit = capacity_;
    if (retry_after == 0) {
        res.allowed = true;
        res.remaining = remaining_capacity;
        res.reset = static_cast<long long>(std::ceil(now_sec + ((capacity_ - remaining_capacity) / leak_rate_)));
        res.retry_after = 0;
    } else {
        res.allowed = false;
        res.remaining = 0;
        res.reset = static_cast<long long>(std::ceil(now_sec + retry_after));
        res.retry_after = retry_after;
    }
    return res;
}
