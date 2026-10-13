import { BaseRateLimiter } from './base.js';
import { redisManager } from '../storage/redis.js';
import { memoryStorage } from '../storage/memory.js';

const SLIDING_COUNTER_LUA = `
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
`;

export class SlidingWindowCounterRateLimiter extends BaseRateLimiter {
  constructor(limit = 10, windowSeconds = 10) {
    super('SlidingWindowCounter');
    this.limit = limit;
    this.windowSeconds = windowSeconds;
  }

  async consume(key) {
    const now = Math.floor(Date.now() / 1000);
    const windowId = Math.floor(now / this.windowSeconds);
    const timeInCurrentWindow = now % this.windowSeconds;
    const overlapRatio = timeInCurrentWindow / this.windowSeconds;
    const windowReset = (windowId + 1) * this.windowSeconds;

    const currKey = `rl:node:swcnt:${key}:${windowId}`;
    const prevKey = `rl:node:swcnt:${key}:${windowId - 1}`;

    if (redisManager.isConnected) {
      try {
        const [allowed, remaining, retryAfter] = await redisManager.eval(
          SLIDING_COUNTER_LUA,
          2,
          currKey,
          prevKey,
          this.limit,
          this.windowSeconds,
          overlapRatio,
          windowReset
        );
        return {
          allowed: allowed === 1,
          limit: this.limit,
          remaining: Math.max(0, remaining),
          reset: windowReset,
          retryAfter: allowed === 1 ? 0 : Math.max(1, retryAfter),
        };
      } catch (err) {
        console.warn(`[SlidingWindowCounter] Redis error, falling back to memory: ${err.message}`);
      }
    }

    // In-memory fallback
    const memKey = `swcnt:${key}`;
    let data = memoryStorage.get(memKey) || { currentWindowId: windowId, currentCount: 0, previousCount: 0 };

    if (data.currentWindowId !== windowId) {
      if (data.currentWindowId === windowId - 1) {
        data.previousCount = data.currentCount;
      } else {
        data.previousCount = 0;
      }
      data.currentCount = 0;
      data.currentWindowId = windowId;
    }

    const estimated = Math.floor(data.previousCount * (1 - overlapRatio) + data.currentCount);

    if (estimated < this.limit) {
      data.currentCount += 1;
      memoryStorage.set(memKey, data, this.windowSeconds * 2);
      return {
        allowed: true,
        limit: this.limit,
        remaining: Math.max(0, this.limit - estimated - 1),
        reset: windowReset,
        retryAfter: 0,
      };
    } else {
      return {
        allowed: false,
        limit: this.limit,
        remaining: 0,
        reset: windowReset,
        retryAfter: Math.max(1, windowReset - now),
      };
    }
  }
}
