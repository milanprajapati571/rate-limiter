# 03. Distributed State Management & Redis Lua Scripts

In distributed systems, rate limiting across multiple stateless application servers introduces critical concurrency hazards. This document explains why naive caching fails, how **atomic Redis Lua scripts** solve the Check-Then-Act race condition, and details the native protocol implementations.

---

## 1. The Distributed Concurrency Problem: Check-Then-Act Race Condition

In modern microservice architectures, incoming requests are balanced across multiple server nodes.

```
       Worker Node 1                                Worker Node 2
             │                                            │
             │─── 1. GET key (Count = 9) ────────────────>│ (Redis)
             │                                            │
             │                  2. GET key (Count = 9) ──>│
             │                                            │
   [Limit is 10: OK!]                           [Limit is 10: OK!]
             │                                            │
             │─── 3. SET key 10 (Allow request) ─────────>│
             │                                            │
             │                  4. SET key 10 (Allow!) ──>│
             ▼                                            ▼
      Request Admitted                             Request Admitted
             \                                            /
              \── 11 requests served for limit of 10! ───/
```

### Why Naive Approaches Fail
1. **Network Roundtrips**: A multi-step check (`GET` $\to$ compare $\to$ `INCR` $\to$ `EXPIRE`) requires 3 to 4 network hops across the datacenter.
2. **Race Conditions**: Two requests arriving simultaneously read the old counter before either writes the new counter, allowing clients to breach their rate limit without detection.
3. **Distributed Locks (Redlock)**: Implementing distributed locks incurs severe latency penalties (10x-50x slower) and introduces deadlocks or timeout complexities.

---

## 2. Why Redis Lua Scripts Solve This in 1 RTT

Redis executes Lua scripts **atomically on its primary execution thread**. 

### Three Key Guarantees
1. **Absolute Atomicity**: No other Redis command can interleave or execute while a Lua script runs. The entire "read, compare, increment, set TTL" lifecycle runs as a single uninterruptible atomic step.
2. **1 Network Roundtrip (RTT)**: The client sends a single `EVAL` command with the keys and arguments. All calculation happens directly inside Redis memory.
3. **Automated Memory Cleanup**: Every script sets an explicit TTL (`PEXPIRE` / `EXPIRE`), ensuring inactive client keys are automatically evicted from memory.

---

## 3. Concrete Lua Implementations Across Algorithms

### 1. Fixed Window Counter Lua Script
```lua
local key = KEYS[1]
local limit = tonumber(ARGV[1])
local window = tonumber(ARGV[2])

local current = redis.call('INCR', key)
if current == 1 then
    redis.call('EXPIRE', key, window)
end

local ttl = redis.call('TTL', key)
if current <= limit then
    return {1, limit, limit - current, ttl, 0}
else
    return {0, limit, 0, ttl, ttl}
end
```
- **Returns**: `[allowed, limit, remaining, reset_ttl, retry_after]`

---

### 2. Sliding Window Log Lua Script
```lua
local key = KEYS[1]
local limit = tonumber(ARGV[1])
local window = tonumber(ARGV[2])
local now = tonumber(ARGV[3])
local clearBefore = now - window

-- 1. Purge entries older than the current sliding window
redis.call('ZREMRANGEBYSCORE', key, 0, clearBefore)

-- 2. Count current elements in window
local currentRequests = redis.call('ZCARD', key)

if currentRequests < limit then
    -- 3. Log current request timestamp
    redis.call('ZADD', key, now, now .. '-' .. currentRequests)
    redis.call('EXPIRE', key, window)
    return {1, limit, limit - currentRequests - 1, window, 0}
else
    local oldest = redis.call('ZRANGE', key, 0, 0, 'WITHSCORES')
    local retryAfter = window
    if oldest and #oldest >= 2 then
        retryAfter = math.max(1, math.ceil((tonumber(oldest[2]) + window - now)))
    end
    return {0, limit, 0, window, retryAfter}
end
```

---

### 3. Sliding Window Counter Lua Script
```lua
local key = KEYS[1]
local limit = tonumber(ARGV[1])
local window = tonumber(ARGV[2])
local now = tonumber(ARGV[3])

local currentWindow = math.floor(now / window)
local prevWindow = currentWindow - 1

local currKey = key .. ':' .. currentWindow
local prevKey = key .. ':' .. prevWindow

local currCount = tonumber(redis.call('GET', currKey) or '0')
local prevCount = tonumber(redis.call('GET', prevKey) or '0')

local timeIntoCurrWindow = now % window
local weight = (window - timeIntoCurrWindow) / window
local estimatedCount = math.floor(prevCount * weight + currCount)

if estimatedCount < limit then
    redis.call('INCR', currKey)
    redis.call('EXPIRE', currKey, window * 2)
    return {1, limit, limit - estimatedCount - 1, window - timeIntoCurrWindow, 0}
else
    return {0, limit, 0, window - timeIntoCurrWindow, window - timeIntoCurrWindow}
end
```

---

### 4. Token Bucket Lua Script
```lua
local key = KEYS[1]
local capacity = tonumber(ARGV[1])
local refillRate = tonumber(ARGV[2])
local now = tonumber(ARGV[3])
local requested = tonumber(ARGV[4] or '1')

local data = redis.call('HMGET', key, 'tokens', 'lastRefill')
local tokens = tonumber(data[1])
local lastRefill = tonumber(data[2])

if not tokens then
    tokens = capacity
    lastRefill = now
else
    local delta = math.max(0, now - lastRefill)
    tokens = math.min(capacity, tokens + delta * refillRate)
    lastRefill = now
end

if tokens >= requested then
    tokens = tokens - requested
    redis.call('HMSET', key, 'tokens', tokens, 'lastRefill', lastRefill)
    redis.call('EXPIRE', key, math.ceil(capacity / refillRate) * 2)
    return {1, capacity, math.floor(tokens), 0, 0}
else
    redis.call('HMSET', key, 'tokens', tokens, 'lastRefill', lastRefill)
    redis.call('EXPIRE', key, math.ceil(capacity / refillRate) * 2)
    local retryAfter = math.ceil((requested - tokens) / refillRate)
    return {0, capacity, 0, retryAfter, retryAfter}
end
```

---

### 5. Leaky Bucket Lua Script
```lua
local key = KEYS[1]
local capacity = tonumber(ARGV[1])
local leakRate = tonumber(ARGV[2])
local now = tonumber(ARGV[3])

local data = redis.call('HMGET', key, 'water', 'lastLeak')
local water = tonumber(data[1]) or 0
local lastLeak = tonumber(data[2]) or now

local delta = math.max(0, now - lastLeak)
water = math.max(0, water - delta * leakRate)
lastLeak = now

if water + 1 <= capacity then
    water = water + 1
    redis.call('HMSET', key, 'water', water, 'lastLeak', lastLeak)
    redis.call('EXPIRE', key, math.ceil(capacity / leakRate) * 2)
    return {1, capacity, math.floor(capacity - water), 0, 0}
else
    redis.call('HMSET', key, 'water', water, 'lastLeak', lastLeak)
    redis.call('EXPIRE', key, math.ceil(capacity / leakRate) * 2)
    local retryAfter = math.ceil((water + 1 - capacity) / leakRate)
    return {0, capacity, 0, retryAfter, retryAfter}
end
```

---

## 4. Native C++ Winsock RESP Protocol Client

Rather than pulling heavy external C++ SDKs, the C++ engine features a custom, lightweight **RESP (REdis Serialization Protocol)** client implemented over Windows Sockets (`ws2_32`):

```cpp
// Array Serialization: *<count>\r\n$<len>\r\n<arg>\r\n...
std::string command = "*3\r\n$4\r\nEVAL\r\n$" + std::to_string(luaScript.length()) + 
                      "\r\n" + luaScript + "\r\n$1\r\n1\r\n";
send(sock, command.c_str(), command.length(), 0);
```

### RESP Parsing Benefits
- **Zero dynamic library dependencies**: Links statically with Winsock.
- **Fast buffer parsing**: Evaluates integers (`:<int>\r\n`), arrays (`*<count>\r\n`), and bulk strings (`$<len>\r\n<str>\r\n`) in-place.
- **Microsecond reconnection**: Transparently falls back to `std::mutex` memory store if socket disconnection occurs.
