import { BaseRateLimiter } from './base.js';
import { redisManager } from '../storage/redis.js';
import { memoryStorage } from '../storage/memory.js';

const SLIDING_LOG_LUA = `
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
`;

export class SlidingWindowLogRateLimiter extends BaseRateLimiter {
  constructor(limit = 10, windowSeconds = 10) {
    super('SlidingWindowLog');
    this.limit = limit;
    this.windowSeconds = windowSeconds;
    this.windowMs = windowSeconds * 1000;
  }

  async consume(key) {
    const nowMs = Date.now();
    const redisKey = `rl:node:swlog:${key}`;
    const member = `${nowMs}:${Math.random().toString(36).substring(2, 9)}`;
    const resetTime = Math.ceil((nowMs + this.windowMs) / 1000);

    if (redisManager.isConnected) {
      try {
        const [allowed, remaining, retryAfter] = await redisManager.eval(
          SLIDING_LOG_LUA,
          1,
          redisKey,
          nowMs,
          this.windowMs,
          this.limit,
          member
        );
        return {
          allowed: allowed === 1,
          limit: this.limit,
          remaining: Math.max(0, remaining),
          reset: resetTime,
          retryAfter: allowed === 1 ? 0 : Math.max(1, retryAfter),
        };
      } catch (err) {
        console.warn(`[SlidingWindowLog] Redis error, falling back to memory: ${err.message}`);
      }
    }

    // In-memory fallback
    const memKey = `swlog:${key}`;
    let timestamps = memoryStorage.get(memKey) || [];
    const clearBefore = nowMs - this.windowMs;
    timestamps = timestamps.filter(t => t > clearBefore);

    if (timestamps.length < this.limit) {
      timestamps.push(nowMs);
      memoryStorage.set(memKey, timestamps, this.windowSeconds + 2);
      return {
        allowed: true,
        limit: this.limit,
        remaining: this.limit - timestamps.length,
        reset: resetTime,
        retryAfter: 0,
      };
    } else {
      const oldest = timestamps[0] || (nowMs - this.windowMs);
      const retryAfterSec = Math.max(1, Math.ceil(((oldest + this.windowMs) - nowMs) / 1000));
      return {
        allowed: false,
        limit: this.limit,
        remaining: 0,
        reset: resetTime,
        retryAfter: retryAfterSec,
      };
    }
  }
}
