import express from 'express';
import apiRouter from './routes/api.js';
import { redisManager } from './storage/redis.js';

const app = express();
const PORT = parseInt(process.env.PORT || '3000', 10);

app.use(express.json());
app.use(express.urlencoded({ extended: true }));

// Request logging middleware
app.use((req, res, next) => {
  const start = Date.now();
  res.on('finish', () => {
    const duration = Date.now() - start;
    if (process.env.NODE_ENV !== 'test') {
      // concise log
    }
  });
  next();
});

// Health check endpoint
app.get('/health', (req, res) => {
  res.status(200).json({
    status: 'healthy',
    server: 'node-rate-limiter',
    redisConnected: redisManager.isConnected,
    storageMode: redisManager.isConnected ? 'distributed-redis' : 'in-memory-fallback',
    timestamp: new Date().toISOString(),
  });
});

// Mount rate limiting routes
app.use('/api', apiRouter);

// Fallback 404
app.use((req, res) => {
  res.status(404).json({
    status: 'error',
    code: 404,
    message: `Endpoint ${req.method} ${req.url} not found.`,
  });
});

// Global error handler
app.use((err, req, res, next) => {
  console.error('[Unhandled Error]:', err);
  res.status(500).json({
    status: 'error',
    code: 500,
    message: 'Internal server error',
    error: err.message,
  });
});

const server = app.listen(PORT, () => {
  console.log(`====================================================`);
  console.log(` Node.js Express Rate Limiter running on port ${PORT}`);
  console.log(` Storage mode: ${redisManager.isConnected ? 'Redis Distributed' : 'In-Memory Fallback'}`);
  console.log(` Health check: http://localhost:${PORT}/health`);
  console.log(`====================================================`);
});

process.on('SIGTERM', () => {
  server.close(() => {
    console.log('Node.js Rate Limiter process terminated.');
  });
});

export default app;
