export class BaseRateLimiter {
  constructor(name) {
    this.name = name;
  }

  /**
   * Consume 1 unit for the given client identifier.
   * @param {string} key 
   * @returns {Promise<{ allowed: boolean, limit: number, remaining: number, reset: number, retryAfter: number }>}
   */
  async consume(key) {
    throw new Error('Method consume() must be implemented');
  }
}
