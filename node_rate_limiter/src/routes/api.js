import { Router } from 'express';
import { FixedWindowRateLimiter } from '../algorithms/fixedWindow.js';
import { SlidingWindowLogRateLimiter } from '../algorithms/slidingWindowLog.js';
import { SlidingWindowCounterRateLimiter } from '../algorithms/slidingWindowCounter.js';
import { TokenBucketRateLimiter } from '../algorithms/tokenBucket.js';
import { LeakyBucketRateLimiter } from '../algorithms/leakyBucket.js';
import { createRateLimiterMiddleware } from '../middleware/rateLimiter.js';

const router = Router();

// Instantiate limiters (configured for 10 requests per window / capacity 10)
const fixedWindowLimiter = new FixedWindowRateLimiter(10, 10);
const slidingLogLimiter = new SlidingWindowLogRateLimiter(10, 10);
const slidingCounterLimiter = new SlidingWindowCounterRateLimiter(10, 10);
const tokenBucketLimiter = new TokenBucketRateLimiter(10, 2.0); // 10 capacity, 2 tokens/sec
const leakyBucketLimiter = new LeakyBucketRateLimiter(10, 2.0); // 10 capacity, 2 units/sec

const successHandler = (algorithmName) => (req, res) => {
  res.status(200).json({
    status: 'success',
    code: 200,
    message: `Request successfully processed under '${algorithmName}' rate limiter.`,
    algorithm: algorithmName,
    rateLimit: req.rateLimitInfo,
    server: 'Node.js Express',
    timestamp: new Date().toISOString(),
  });
};

// Fixed Window
router.all(
  '/rate-limit/fixed-window',
  createRateLimiterMiddleware(fixedWindowLimiter),
  successHandler('FixedWindow')
);

// Sliding Window Log
router.all(
  '/rate-limit/sliding-window-log',
  createRateLimiterMiddleware(slidingLogLimiter),
  successHandler('SlidingWindowLog')
);

// Sliding Window Counter
router.all(
  '/rate-limit/sliding-window-counter',
  createRateLimiterMiddleware(slidingCounterLimiter),
  successHandler('SlidingWindowCounter')
);

// Token Bucket
router.all(
  '/rate-limit/token-bucket',
  createRateLimiterMiddleware(tokenBucketLimiter),
  successHandler('TokenBucket')
);

// Leaky Bucket
router.all(
  '/rate-limit/leaky-bucket',
  createRateLimiterMiddleware(leakyBucketLimiter),
  successHandler('LeakyBucket')
);

export default router;
