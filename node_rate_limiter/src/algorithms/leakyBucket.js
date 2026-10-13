import { BaseRateLimiter } from './base.js';
import { redisManager } from '../storage/redis.js';
import { memoryStorage } from '../storage/memory.js';

const LEAKY_BUCKET_LUA = `
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
`;

export class LeakyBucketRateLimiter extends BaseRateLimiter {
  constructor(capacity = 10, leakRate = 1.0) {
    super('LeakyBucket');
    this.capacity = capacity;
    this.leakRate = leakRate; // units leaked per second
  }

  async consume(key) {
    const nowSec = Date.now() / 1000;
    const redisKey = `rl:node:lb:${key}`;

    if (redisManager.isConnected) {
      try {
        const [allowed, remaining, retryAfter] = await redisManager.eval(
          LEAKY_BUCKET_LUA,
          1,
          redisKey,
          this.capacity,
          this.leakRate,
          nowSec
        );
        return {
          allowed: allowed === 1,
          limit: this.capacity,
          remaining: Math.max(0, remaining),
          reset: Math.ceil(nowSec + ((this.capacity - remaining) / this.leakRate)),
          retryAfter: allowed === 1 ? 0 : Math.max(1, retryAfter),
        };
      } catch (err) {
        console.warn(`[LeakyBucket] Redis error, falling back to memory: ${err.message}`);
      }
    }

    // In-memory fallback
    const memKey = `lb:${key}`;
    let bucket = memoryStorage.get(memKey);
    if (!bucket) {
      bucket = {
        water: 0,
        lastLeak: nowSec,
      };
    } else {
      const delta = Math.max(0, nowSec - bucket.lastLeak);
      bucket.water = Math.max(0, bucket.water - delta * this.leakRate);
      bucket.lastLeak = nowSec;
    }

    const ttl = Math.ceil(this.capacity / this.leakRate) + 10;
    if (bucket.water + 1 <= this.capacity) {
      bucket.water += 1;
      memoryStorage.set(memKey, bucket, ttl);
      return {
        allowed: true,
        limit: this.capacity,
        remaining: Math.floor(this.capacity - bucket.water),
        reset: Math.ceil(nowSec + (bucket.water / this.leakRate)),
        retryAfter: 0,
      };
    } else {
      memoryStorage.set(memKey, bucket, ttl);
      const retryAfter = Math.max(1, Math.ceil((bucket.water + 1 - this.capacity) / this.leakRate));
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
