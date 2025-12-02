import axios from 'axios';

class DatabaseAPI {
  constructor() {
    this.baseURL = '/api';
    this.leaderHost = null;
    this.leaderPort = null;
    this.leaderDiscoveryCache = null;
    this.leaderDiscoveryCacheTime = 0;
    this.CACHE_TTL = 5000; // 5 seconds
  }

  /**
   * Discover current leader from cluster
   * Caches result for CACHE_TTL milliseconds
   */
  async discoverLeader() {
    const now = Date.now();

    // Return cached result if fresh
    if (this.leaderDiscoveryCache &&
        (now - this.leaderDiscoveryCacheTime) < this.CACHE_TTL) {
      return this.leaderDiscoveryCache;
    }

    try {
      const response = await axios.get(`${this.baseURL}/leader`);
      this.leaderHost = response.data.host;
      this.leaderPort = response.data.port;
      this.leaderDiscoveryCache = response.data;
      this.leaderDiscoveryCacheTime = now;
      return response.data;
    } catch (error) {
      console.error('Leader discovery failed:', error);
      throw new Error('Cannot discover cluster leader');
    }
  }

  /**
   * GET key-value pair
   */
  async get(key) {
    await this.discoverLeader(); // Ensure we know the leader
    const response = await axios.get(`${this.baseURL}/get/${key}`);
    return response.data;
  }

  /**
   * SET key-value pair
   */
  async set(key, value) {
    await this.discoverLeader();
    const response = await axios.post(`${this.baseURL}/set/${key}`, { value });
    return response.data;
  }

  /**
   * DELETE key
   */
  async delete(key) {
    await this.discoverLeader();
    const response = await axios.post(`${this.baseURL}/del/${key}`);
    return response.data;
  }

  /**
   * GETFF - Forward range query with pagination
   * @param {string} startKey - Starting key for pagination
   * @param {number} count - Number of results
   */
  async getForward(startKey, count = 10) {
    await this.discoverLeader();
    const response = await axios.get(
      `${this.baseURL}/getff/${startKey}?count=${count}`
    );
    return response.data;
  }

  /**
   * GETFB - Backward range query with pagination
   */
  async getBackward(endKey, count = 10) {
    await this.discoverLeader();
    const response = await axios.get(
      `${this.baseURL}/getfb/${endKey}?count=${count}`
    );
    return response.data;
  }

  /**
   * Get keys with prefix
   * @param {string} prefix - Prefix to search for (empty string for all keys)
   */
  async getKeysPrefix(prefix = "") {
    await this.discoverLeader();
    const response = await axios.get(`${this.baseURL}/keys/prefix/${encodeURIComponent(prefix)}`);
    return response.data;
  }

  /**
   * Get keys with paging
   * @param {number} pageSize - Number of keys per page
   * @param {number} pageNum - Page number (1-indexed)
   */
  async getKeysPaging(pageSize, pageNum) {
    await this.discoverLeader();
    const response = await axios.get(
      `${this.baseURL}/keys/paging?pageSize=${pageSize}&pageNum=${pageNum}`
    );
    return response.data;
  }

  /**
   * OPTIMIZE database
   */
  async optimize() {
    await this.discoverLeader();
    const response = await axios.post(`${this.baseURL}/optimize`);
    return response.data;
  }

  /**
   * Get comprehensive cluster status
   * Returns detailed information about all nodes, leader, followers, and split brain detection
   */
  async getClusterStatus() {
    const response = await axios.get(`${this.baseURL}/cluster/status`);
    return response.data;
  }
}

export default new DatabaseAPI();
