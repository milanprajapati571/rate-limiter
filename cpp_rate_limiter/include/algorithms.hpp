#pragma once

#include "rate_limiter.hpp"
#include "redis_client.hpp"
#include <memory>

class FixedWindowRateLimiter : public IRateLimiter {
public:
    FixedWindowRateLimiter(int limit, int window_seconds, std::shared_ptr<RedisClient> redis);
    RateLimitResult check(const std::string& key) override;
    std::string get_name() const override { return "FixedWindow"; }

private:
    int limit_;
    int window_seconds_;
    std::shared_ptr<RedisClient> redis_;
};

class SlidingWindowLogRateLimiter : public IRateLimiter {
public:
    SlidingWindowLogRateLimiter(int limit, int window_seconds, std::shared_ptr<RedisClient> redis);
    RateLimitResult check(const std::string& key) override;
    std::string get_name() const override { return "SlidingWindowLog"; }

private:
    int limit_;
    int window_seconds_;
    std::shared_ptr<RedisClient> redis_;
};

class SlidingWindowCounterRateLimiter : public IRateLimiter {
public:
    SlidingWindowCounterRateLimiter(int limit, int window_seconds, std::shared_ptr<RedisClient> redis);
    RateLimitResult check(const std::string& key) override;
    std::string get_name() const override { return "SlidingWindowCounter"; }

private:
    int limit_;
    int window_seconds_;
    std::shared_ptr<RedisClient> redis_;
};

class TokenBucketRateLimiter : public IRateLimiter {
public:
    TokenBucketRateLimiter(int capacity, double refill_rate, std::shared_ptr<RedisClient> redis);
    RateLimitResult check(const std::string& key) override;
    std::string get_name() const override { return "TokenBucket"; }

private:
    int capacity_;
    double refill_rate_;
    std::shared_ptr<RedisClient> redis_;
};

class LeakyBucketRateLimiter : public IRateLimiter {
public:
    LeakyBucketRateLimiter(int capacity, double leak_rate, std::shared_ptr<RedisClient> redis);
    RateLimitResult check(const std::string& key) override;
    std::string get_name() const override { return "LeakyBucket"; }

private:
    int capacity_;
    double leak_rate_;
    std::shared_ptr<RedisClient> redis_;
};
