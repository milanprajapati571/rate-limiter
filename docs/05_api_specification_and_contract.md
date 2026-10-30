# 05. API Specification & Header Contract

Both the C++ and Node.js rate-limiting engines implement identical REST endpoint contracts, header specifications, and JSON payload schemas conforming to standard RFC draft guidelines for HTTP Rate Limiting.

---

## 1. Endpoints Catalog

All rate-limited endpoints support both `GET` and `POST` methods.

| Endpoint | Method | Algorithm | Default Rate Limit Configuration |
| :--- | :--- | :--- | :--- |
| `/health` | `GET` | Health Check | Returns server name, Redis connectivity, and storage mode |
| `/api/rate-limit/fixed-window` | `GET` / `POST` | Fixed Window Counter | 10 requests / 10-second window |
| `/api/rate-limit/sliding-window-log` | `GET` / `POST` | Sliding Window Log | 10 requests / 10-second window |
| `/api/rate-limit/sliding-window-counter` | `GET` / `POST` | Sliding Window Counter | 10 requests / 10-second window |
| `/api/rate-limit/token-bucket` | `GET` / `POST` | Token Bucket | Capacity: 10 tokens, Refill: 2.0 tokens/sec |
| `/api/rate-limit/leaky-bucket` | `GET` / `POST` | Leaky Bucket | Capacity: 10 units, Leak: 2.0 units/sec |

---

## 2. Client Identification Strategy

Clients can pass identity through three mechanisms, evaluated in priority order:

1. **Custom Header**: `X-Client-Id: user_9821`
2. **Query Parameter**: `?clientId=user_9821`
3. **Remote Socket IP**: Fallback to remote IP address if no explicit identifier is supplied.

---

## 3. Standard HTTP Rate-Limiting Headers

Every response returned by the rate limiters appends standard governance headers:

| Header | Description | Example |
| :--- | :--- | :--- |
| `X-RateLimit-Limit` | Maximum requests permitted within the current window/bucket | `10` |
| `X-RateLimit-Remaining` | Remaining request quota for the current client | `7` |
| `X-RateLimit-Reset` | Unix timestamp (seconds) when the current quota refreshes | `1791895200` |
| `Retry-After` | Returned on `429 Too Many Requests`. Seconds client must wait | `3` |

---

## 4. Response Schemas & Examples

### 1. Successful Request (`200 OK`)
Returned when the incoming request is permitted within the quota:

```json
{
  "status": "success",
  "code": 200,
  "message": "Request successfully processed under 'TokenBucket' rate limiter.",
  "algorithm": "TokenBucket",
  "rateLimit": {
    "allowed": true,
    "limit": 10,
    "remaining": 8,
    "reset": 1791895200,
    "retryAfter": 0
  },
  "server": "C++ httplib"
}
```

**Associated Headers:**
```http
HTTP/1.1 200 OK
Content-Type: application/json
X-RateLimit-Limit: 10
X-RateLimit-Remaining: 8
X-RateLimit-Reset: 1791895200
```

---

### 2. Rate-Limited Request (`429 Too Many Requests`)
Returned when quota is exhausted or bucket overflows:

```json
{
  "status": "error",
  "code": 429,
  "message": "Too Many Requests: Rate limit exceeded for algorithm 'TokenBucket'",
  "algorithm": "TokenBucket",
  "retryAfter": 2,
  "reset": 1791895202,
  "server": "C++ httplib"
}
```

**Associated Headers:**
```http
HTTP/1.1 429 Too Many Requests
Content-Type: application/json
X-RateLimit-Limit: 10
X-RateLimit-Remaining: 0
X-RateLimit-Reset: 1791895202
Retry-After: 2
```

---

## 5. cURL & CLI Testing Examples

### 1. Test Fixed Window Rate Limiting (C++ Server)
```bash
curl -i -X POST http://localhost:8080/api/rate-limit/fixed-window \
  -H "X-Client-Id: test_client_1"
```

### 2. Test Token Bucket Rate Limiting (Node.js Server)
```bash
curl -i -X GET "http://localhost:3000/api/rate-limit/token-bucket?clientId=mobile_app_1"
```

### 3. Rapid Throttling Test (Windows PowerShell)
Fire 15 requests in a rapid loop to trigger 429 responses:
```powershell
1..15 | ForEach-Object {
    Invoke-RestMethod -Uri "http://localhost:8080/api/rate-limit/token-bucket" `
                      -Headers @{"X-Client-Id"="burst_tester"}
}
```
Requests 1–10 return `200 OK`, while requests 11–15 immediately return `429 Too Many Requests`.
