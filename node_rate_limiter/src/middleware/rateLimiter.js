export function createRateLimiterMiddleware(limiterInstance) {
  return async (req, res, next) => {
    try {
      const clientId = req.headers['x-client-id'] || 
                       req.query.clientId || 
                       req.ip || 
                       req.socket.remoteAddress || 
                       'default-client';

      const result = await limiterInstance.consume(clientId);

      // Standard Rate Limiting Headers
      res.setHeader('X-RateLimit-Limit', result.limit);
      res.setHeader('X-RateLimit-Remaining', result.remaining);
      res.setHeader('X-RateLimit-Reset', result.reset);

      if (!result.allowed) {
        res.setHeader('Retry-After', result.retryAfter);
        return res.status(429).json({
          status: 'error',
          code: 429,
          message: `Too Many Requests: Rate limit exceeded for algorithm '${limiterInstance.name}'.`,
          algorithm: limiterInstance.name,
          retryAfter: result.retryAfter,
          reset: result.reset,
          server: 'Node.js Express',
        });
      }

      req.rateLimitInfo = result;
      next();
    } catch (err) {
      console.error(`[RateLimiter Middleware Error]:`, err);
      // In case of internal middleware error, allow traffic gracefully or return 500
      next(err);
    }
  };
}
