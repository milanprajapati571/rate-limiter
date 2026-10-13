import { BaseRateLimiter } from './base.js';
import { redisManager } from '../storage/redis.js';
import { memoryStorage } from '../storage/memory.js';

const FIXED_WINDOW_LUA = `
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
`;

export class FixedWindowRateLimiter extends BaseRateLimiter {
  constructor(limit = 10, windowSeconds = 10) {
    super('FixedWindow');
    this.limit = limit;
    this.windowSeconds = windowSeconds;
  }

  async consume(key) {
    const now = Math.floor(Date.now() / 1000);
    const windowId = Math.floor(now / this.windowSeconds);
    const redisKey = `rl:node:fw:${key}:${windowId}`;
    const windowReset = (windowId + 1) * this.windowSeconds;

    if (redisManager.isConnected) {
      try {
        const [allowed, remaining, retryAfter] = await redisManager.eval(
          FIXED_WINDOW_LUA,
          1,
          redisKey,
          this.limit,
          this.windowSeconds
        );
        return {
          allowed: allowed === 1,
          limit: this.limit,
          remaining: Math.max(0, remaining),
          reset: windowReset,
          retryAfter: allowed === 1 ? 0 : Math.max(1, retryAfter || (windowReset - now)),
        };
      } catch (err) {
        console.warn(`[FixedWindow] Redis error, falling back to memory: ${err.message}`);
      }
    }

    // In-memory fallback
    const memKey = `fw:${key}:${windowId}`;
    let data = memoryStorage.get(memKey);
    if (!data) {
      data = { count: 0 };
    }
    data.count += 1;
    memoryStorage.set(memKey, data, this.windowSeconds + 2);

    if (data.count <= this.limit) {
      return {
        allowed: true,
        limit: this.limit,
        remaining: this.limit - data.count,
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
