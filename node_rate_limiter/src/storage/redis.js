import Redis from 'ioredis';

class RedisManager {
  constructor() {
    this.host = process.env.REDIS_HOST || '127.0.0.1';
    this.port = parseInt(process.env.REDIS_PORT || '6379', 10);
    this.isAvailable = false;
    this.client = null;
    this._initClient();
  }

  _initClient() {
    try {
      this.client = new Redis({
        host: this.host,
        port: this.port,
        enableOfflineQueue: false,
        maxRetriesPerRequest: 1,
        retryStrategy: (times) => {
          // Retry connection after 5 seconds up to limit
          if (times > 5) {
            return null;
          }
          return Math.min(times * 1000, 3000);
        },
        lazyConnect: false,
      });

      this.client.on('connect', () => {
        this.isAvailable = true;
        console.log(`[Redis] Connected successfully to ${this.host}:${this.port}`);
      });

      this.client.on('ready', () => {
        this.isAvailable = true;
      });

      this.client.on('error', (err) => {
        if (this.isAvailable) {
          console.warn(`[Redis] Connection warning: ${err.message}. Falling back to in-memory store.`);
        }
        this.isAvailable = false;
      });

      this.client.on('close', () => {
        this.isAvailable = false;
      });
    } catch (e) {
      this.isAvailable = false;
      console.warn(`[Redis] Initialization failed: ${e.message}. Using in-memory fallback.`);
    }
  }

  get isConnected() {
    return this.isAvailable && this.client && this.client.status === 'ready';
  }

  async eval(script, numKeys, ...args) {
    if (!this.isConnected) {
      throw new Error('Redis not connected');
    }
    return await this.client.eval(script, numKeys, ...args);
  }
}

export const redisManager = new RedisManager();
