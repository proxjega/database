<template>
  <div class="node-selector-card card mb-4">
    <div class="card-header bg-dark text-white">
      <h6 class="mb-0">Target Node Selection</h6>
    </div>
    <div class="card-body">
      <div class="row align-items-center">
        <div class="col-md-6">
          <label class="form-label">Select Target Node:</label>
          <select
            v-model="selectedNodeId"
            @change="onNodeChange"
            class="form-select"
          >
            <!-- Requirement #2: Default to leader discovery -->
            <option :value="null">
              Auto (Leader Discovery)
            </option>
            <option
              v-for="node in nodes"
              :key="node.id"
              :value="node.id"
              :disabled="!isNodeAvailable(node.id)"
            >
              <!-- Requirement #3: Show "Node # (IP)" format -->
              {{ node.name }}
              <span v-if="!isNodeAvailable(node.id)"> - OFFLINE</span>
              <span v-if="node.id === leaderId"> ⭐ LEADER</span>
            </option>
          </select>
        </div>

        <div class="col-md-6" v-if="selectedNode">
          <div class="alert alert-info mb-0">
            <strong>Selected:</strong> Node {{ selectedNode.id }}<br>
            <strong>Status:</strong>
            <span :class="getStatusClass(selectedNode.id)">
              {{ getNodeStatus(selectedNode.id) }}
            </span><br>
            <strong>Port:</strong> {{ selectedNode.clientPort }}
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script>
import api from '@/services/api';

export default {
  name: 'NodeSelector',
  data() {
    return {
      selectedNodeId: null,
      nodes: [],
      clusterStatus: null,
      leaderId: null
    };
  },
  computed: {
    selectedNode() {
      return this.nodes.find(n => n.id === this.selectedNodeId);
    }
  },
  async mounted() {
    await this.loadNodes();
    await this.loadClusterStatus();

    // Refresh cluster status every 5 seconds
    setInterval(() => this.loadClusterStatus(), 5000);
  },
  methods: {
    async loadNodes() {
      try {
        this.nodes = await api.getAvailableNodes();
      } catch (err) {
        console.error('Failed to load nodes:', err);
      }
    },

    async loadClusterStatus() {
      try {
        this.clusterStatus = await api.getClusterStatus();
        const leaderNode = this.clusterStatus.nodes.find(n => n.role === 'LEADER');
        this.leaderId = leaderNode?.id;
      } catch (err) {
        console.error('Failed to load cluster status:', err);
      }
    },

    onNodeChange() {
      api.selectNode(this.selectedNodeId);
      this.$emit('node-changed', this.selectedNodeId);
    },

    isNodeAvailable(nodeId) {
      if (!this.clusterStatus) return true;
      const node = this.clusterStatus.nodes.find(n => n.id === nodeId);
      return node?.status === 'ONLINE';
    },

    // Requirement #5: Show error for offline nodes, no auto-fallback
    validateNodeSelection() {
      if (this.selectedNodeId && !this.isNodeAvailable(this.selectedNodeId)) {
        alert(`Node ${this.selectedNodeId} is OFFLINE. Please select a different node.`);
        this.selectedNodeId = null; // Reset to Auto
        return false;
      }
      return true;
    },

    getNodeStatus(nodeId) {
      if (!this.clusterStatus) return 'UNKNOWN';
      const node = this.clusterStatus.nodes.find(n => n.id === nodeId);
      return node?.role || 'UNKNOWN';
    },

    getStatusClass(nodeId) {
      const status = this.getNodeStatus(nodeId);
      return {
        'text-success': status === 'LEADER',
        'text-info': status === 'FOLLOWER',
        'text-warning': status === 'CANDIDATE',
        'text-danger': status === 'OFFLINE'
      };
    }
  }
};
</script>

<style scoped>
.node-selector-card {
  border: 1px solid #dee2e6;
}

.node-selector-card .card-header {
  background-color: #343a40 !important;
}

.form-select option:disabled {
  color: #6c757d;
}
</style>
