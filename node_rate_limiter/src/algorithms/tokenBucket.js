import { BaseRateLimiter } from './base.js';
import { redisManager } from '../storage/redis.js';
import { memoryStorage } from '../storage/memory.js';

const TOKEN_BUCKET_LUA = `
local key = KEYS[1]
local capacity = tonumber(ARGV[1])
local refillRate = tonumber(ARGV[2])
local now = tonumber(ARGV[3])

local data = redis.call('HMGET', key, 'tokens', 'lastRefill')
local tokens = tonumber(data[1])
local lastRefill = tonumber(data[2])

if tokens == nil then
    tokens = capacity
    lastRefill = now
else
    local delta = math.max(0, now - lastRefill)
    tokens = math.min(capacity, tokens + delta * refillRate)
    lastRefill = now
end

if tokens >= 1 then
    tokens = tokens - 1
    redis.call('HMSET', key, 'tokens', tokens, 'lastRefill', lastRefill)
    redis.call('EXPIRE', key, math.ceil(capacity / refillRate) + 10)
    return {1, math.floor(tokens), 0}
else
    redis.call('HMSET', key, 'tokens', tokens, 'lastRefill', lastRefill)
    redis.call('EXPIRE', key, math.ceil(capacity / refillRate) + 10)
    local retryAfter = math.ceil((1 - tokens) / refillRate)
    return {0, 0, math.max(1, retryAfter)}
end
`;

export class TokenBucketRateLimiter extends BaseRateLimiter {
  constructor(capacity = 10, refillRate = 1.0) {
    super('TokenBucket');
    this.capacity = capacity;
    this.refillRate = refillRate; // tokens per second
  }

  async consume(key) {
    const nowSec = Date.now() / 1000;
    const redisKey = `rl:node:tb:${key}`;

    if (redisManager.isConnected) {
      try {
        const [allowed, remaining, retryAfter] = await redisManager.eval(
          TOKEN_BUCKET_LUA,
          1,
          redisKey,
          this.capacity,
          this.refillRate,
          nowSec
        );
        return {
          allowed: allowed === 1,
          limit: this.capacity,
          remaining: Math.max(0, remaining),
          reset: Math.ceil(nowSec + ((this.capacity - remaining) / this.refillRate)),
          retryAfter: allowed === 1 ? 0 : Math.max(1, retryAfter),
        };
      } catch (err) {
        console.warn(`[TokenBucket] Redis error, falling back to memory: ${err.message}`);
      }
    }

    // In-memory fallback
    const memKey = `tb:${key}`;
    let bucket = memoryStorage.get(memKey);
    if (!bucket) {
      bucket = {
        tokens: this.capacity,
        lastRefill: nowSec,
      };
    } else {
      const delta = Math.max(0, nowSec - bucket.lastRefill);
      bucket.tokens = Math.min(this.capacity, bucket.tokens + delta * this.refillRate);
      bucket.lastRefill = nowSec;
    }

    const ttl = Math.ceil(this.capacity / this.refillRate) + 10;
    if (bucket.tokens >= 1) {
      bucket.tokens -= 1;
      memoryStorage.set(memKey, bucket, ttl);
      return {
        allowed: true,
        limit: this.capacity,
        remaining: Math.floor(bucket.tokens),
        reset: Math.ceil(nowSec + ((this.capacity - bucket.tokens) / this.refillRate)),
        retryAfter: 0,
      };
    } else {
      memoryStorage.set(memKey, bucket, ttl);
      const retryAfter = Math.max(1, Math.ceil((1 - bucket.tokens) / this.refillRate));
      return {
        allowed: false,
        limit: this.capacity,
        remaining: 0,
        reset: Math.ceil(nowSec + retryAfter),
        retryAfter,
      };
    }
  }
}
