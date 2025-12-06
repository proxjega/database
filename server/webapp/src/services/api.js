import axios from "axios";

class DatabaseAPI {
    constructor() {
        this.baseURL = "/api";
        this.leaderHost = null;
        this.leaderPort = null;
        this.leaderDiscoveryCache = null;
        this.leaderDiscoveryCacheTime = 0;
        this.CACHE_TTL = 5000; // 5 seconds
        this.selectedNodeId = 0; // Will be set to leader ID on first use
        this.availableNodes = [];
    }

    /**
     * Discover current leader from cluster
     * Caches result for CACHE_TTL milliseconds
     */
    async discoverLeader() {
        const now = Date.now();

        // Return cached result if fresh
        if (
            this.leaderDiscoveryCache &&
            now - this.leaderDiscoveryCacheTime < this.CACHE_TTL
        ) {
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
            console.error("Leader discovery failed:", error);
            throw new Error("Cannot discover cluster leader");
        }
    }

    /**
     * Select target node for operations
     * @param {number|null} nodeId - Node ID (1-4) or null for auto-discovery
     */
    selectNode(nodeId) {
        this.selectedNodeId = nodeId;
    }

    /**
     * Get available nodes
     */
    async getAvailableNodes() {
        const response = await axios.get(`${this.baseURL}/cluster/nodes`);
        this.availableNodes = response.data.nodes;
        return this.availableNodes;
    }

    /**
     * Build URL with optional nodeId parameter
     */
    _buildUrl(path, nodeId = null) {
        const targetNode = nodeId !== null ? nodeId : this.selectedNodeId;
        const nodeParam = targetNode ? `?nodeId=${targetNode}` : "";
        return `${this.baseURL}${path}${nodeParam}`;
    }

    /**
     * GET key-value pair
     * @param {string} key - Key to retrieve
     * @param {number|null} nodeId - Optional node ID to target
     */
    async get(key, nodeId = null) {
        const response = await axios.get(this._buildUrl(`/get/${key}`, nodeId));
        return response.data;
    }

    /**
     * SET key-value pair
     * @param {string} key - Key to set
     * @param {string} value - Value to set
     * @param {number|null} nodeId - Optional node ID to target (must be leader)
     */
    async set(key, value, nodeId = null) {
        const response = await axios.post(
            this._buildUrl(`/set/${key}`, nodeId),
            { value }
        );
        return response.data;
    }

    /**
     * DELETE key
     * @param {string} key - Key to delete
     * @param {number|null} nodeId - Optional node ID to target (must be leader)
     */
    async delete(key, nodeId = null) {
        const response = await axios.post(
            this._buildUrl(`/del/${key}`, nodeId)
        );
        return response.data;
    }

    /**
     * GETFF - Forward range query with pagination
     * @param {string} startKey - Starting key for pagination
     * @param {number} count - Number of results
     * @param {number|null} nodeId - Optional node ID to target
     */
    async getForward(startKey, count = 10, nodeId = null) {
        const targetNode = nodeId !== null ? nodeId : this.selectedNodeId;
        const nodeParam = targetNode ? `&nodeId=${targetNode}` : "";
        const response = await axios.get(
            `${this.baseURL}/getff/${startKey}?count=${count}${nodeParam}`
        );
        return response.data;
    }

    /**
     * GETFB - Backward range query with pagination
     * @param {string} endKey - Ending key for pagination
     * @param {number} count - Number of results
     * @param {number|null} nodeId - Optional node ID to target
     */
    async getBackward(endKey, count = 10, nodeId = null) {
        const targetNode = nodeId !== null ? nodeId : this.selectedNodeId;
        const nodeParam = targetNode ? `&nodeId=${targetNode}` : "";
        const response = await axios.get(
            `${this.baseURL}/getfb/${endKey}?count=${count}${nodeParam}`
        );
        return response.data;
    }

    /**
     * Get keys with prefix
     * @param {string} prefix - Prefix to search for (empty string for all keys)
     * @param {number|null} nodeId - Optional node ID to target
     */
    async getKeysPrefix(prefix = "", nodeId = null) {
        const response = await axios.get(
            this._buildUrl(`/keys/prefix/${encodeURIComponent(prefix)}`, nodeId)
        );
        return response.data;
    }

    /**
     * Get keys with paging
     * @param {number} pageSize - Number of keys per page
     * @param {number} pageNum - Page number (1-indexed)
     * @param {number|null} nodeId - Optional node ID to target
     */
    async getKeysPaging(pageSize, pageNum, nodeId = null) {
        const targetNode = nodeId !== null ? nodeId : this.selectedNodeId;
        const nodeParam = targetNode ? `&nodeId=${targetNode}` : "";
        const response = await axios.get(
            `${this.baseURL}/keys/paging?pageSize=${pageSize}&pageNum=${pageNum}${nodeParam}`
        );
        return response.data;
    }

    /**
     * OPTIMIZE database
     * @param {number|null} nodeId - Optional node ID to target (must be leader)
     */
    async optimize(nodeId = null) {
        const response = await axios.post(this._buildUrl(`/optimize`, nodeId));
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
