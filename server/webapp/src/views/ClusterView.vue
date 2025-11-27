<template>
  <div class="container mt-4">
    <h2>Cluster Status</h2>

    <div class="row mb-4">
      <div class="col-md-12">
        <button class="btn btn-primary" @click="refreshStatus">
          Refresh Status
        </button>
      </div>
    </div>

    <div v-if="loading" class="text-center loading-spinner">
      <div class="spinner-border" role="status">
        <span class="visually-hidden">Loading...</span>
      </div>
    </div>

    <div v-else-if="error" class="alert alert-danger">
      {{ error }}
    </div>

    <div v-else>
      <div class="row">
        <div class="col-md-6 mb-4">
          <div class="card">
            <div class="card-header bg-success text-white">
              <h5 class="mb-0">Current Leader</h5>
            </div>
            <div class="card-body">
              <div v-if="leaderInfo">
                <p class="mb-2">
                  <strong>Host:</strong> <code>{{ leaderInfo.host }}</code>
                </p>
                <p class="mb-2">
                  <strong>Port:</strong> <code>{{ leaderInfo.port }}</code>
                </p>
                <p class="mb-2">
                  <strong>Status:</strong>
                  <span class="badge bg-success">{{ leaderInfo.status || 'Active' }}</span>
                </p>
                <p class="mb-0">
                  <strong>Last Updated:</strong> {{ formatTimestamp(leaderInfo.timestamp) }}
                </p>
              </div>
              <div v-else class="text-muted">
                No leader information available
              </div>
            </div>
          </div>
        </div>

        <div class="col-md-6 mb-4">
          <div class="card">
            <div class="card-header bg-info text-white">
              <h5 class="mb-0">HTTP Server Health</h5>
            </div>
            <div class="card-body">
              <div v-if="healthInfo">
                <p class="mb-2">
                  <strong>Status:</strong>
                  <span class="badge bg-success">{{ healthInfo.status || 'OK' }}</span>
                </p>
                <p class="mb-2">
                  <strong>Connected to Leader:</strong> <code>{{ healthInfo.leader_host }}</code>
                </p>
                <p class="mb-0">
                  <strong>Leader Port:</strong> <code>{{ healthInfo.leader_port }}</code>
                </p>
              </div>
              <div v-else class="text-muted">
                No health information available
              </div>
            </div>
          </div>
        </div>
      </div>

      <div class="row">
        <div class="col-md-12">
          <div class="card">
            <div class="card-header">
              <h5 class="mb-0">Cluster Configuration</h5>
            </div>
            <div class="card-body">
              <table class="table table-sm">
                <thead>
                  <tr>
                    <th>Node</th>
                    <th>Client Port (Leader)</th>
                    <th>Read Port (Follower)</th>
                    <th>Replication Port</th>
                    <th>Control Port</th>
                  </tr>
                </thead>
                <tbody>
                  <tr v-for="node in clusterNodes" :key="node.id">
                    <td><strong>Node {{ node.id }}</strong></td>
                    <td><code>{{ node.clientPort }}</code></td>
                    <td><code>{{ node.readPort }}</code></td>
                    <td><code>{{ node.replicationPort }}</code></td>
                    <td><code>{{ node.controlPort }}</code></td>
                  </tr>
                </tbody>
              </table>
            </div>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script>
import api from '@/services/api';

export default {
  name: 'ClusterView',
  data() {
    return {
      loading: false,
      error: null,
      leaderInfo: null,
      healthInfo: null,
      clusterNodes: [
        { id: 1, clientPort: 7001, readPort: 7101, replicationPort: 7002, controlPort: 8001 },
        { id: 2, clientPort: 7003, readPort: 7102, replicationPort: 7004, controlPort: 8002 },
        { id: 3, clientPort: 7005, readPort: 7103, replicationPort: 7006, controlPort: 8003 },
        { id: 4, clientPort: 7007, readPort: 7104, replicationPort: 7008, controlPort: 8004 }
      ]
    };
  },
  methods: {
    async refreshStatus() {
      this.loading = true;
      this.error = null;

      try {
        // Fetch leader info and health in parallel
        const [leaderResponse, healthResponse] = await Promise.all([
          api.discoverLeader(),
          api.health()
        ]);

        this.leaderInfo = leaderResponse;
        this.healthInfo = healthResponse;
      } catch (err) {
        this.error = err.message || 'Failed to fetch cluster status';
      } finally {
        this.loading = false;
      }
    },

    formatTimestamp(timestamp) {
      if (!timestamp) return 'N/A';
      return new Date(timestamp).toLocaleString();
    }
  },
  mounted() {
    this.refreshStatus();
  }
};
</script>

<style scoped lang="scss">
.container {
  max-width: 1200px;
}

.card-header {
  font-weight: bold;
}

code {
  background-color: #f8f9fa;
  padding: 2px 6px;
  border-radius: 3px;
  font-size: 0.9em;
}
</style>
