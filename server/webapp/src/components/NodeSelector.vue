<template>
  <div class="node-selector-compact">
    <div class="d-flex align-items-center justify-content-between">
      <div class="flex-grow-1 me-3">
        <select
          v-model="selectedNodeId"
          @change="onNodeChange"
          class="form-select form-select-sm"
        >
          <option
            v-for="node in nodes"
            :key="node.id"
            :value="node.id"
            :disabled="!isNodeAvailable(node.id)"
          >
            {{ node.name }}
            <span v-if="!isNodeAvailable(node.id)"> - OFFLINE</span>
            <span v-if="node.id === leaderId"> - LYDERIS</span>
          </option>
        </select>
      </div>

      <div v-if="selectedNode" class="node-info-compact">
        <span class="badge" :class="getStatusBadgeClass(selectedNode.id)">
          {{ getNodeStatus(selectedNode.id) }}
        </span>
        <small class="text-muted ms-2">Prievadas: {{ selectedNode.clientPort }}</small>
      </div>
    </div>

    <!-- Compact alerts below node selector (managed via props/emits) -->
    <transition name="fade">
      <div
        v-if="message"
        class="alert alert-success alert-compact alert-dismissible fade show mb-0 mt-2"
        role="alert"
      >
        {{ message }}
        <button
          type="button"
          class="btn-close btn-close-sm"
          @click="clearMessage"
          aria-label="Close"
        ></button>
      </div>
    </transition>

    <transition name="fade">
      <div
        v-if="error"
        class="alert alert-danger alert-compact alert-dismissible fade show mb-0 mt-2"
        role="alert"
      >
        {{ error }}
        <button
          type="button"
          class="btn-close btn-close-sm"
          @click="clearError"
          aria-label="Close"
        ></button>
      </div>
    </transition>
  </div>
</template>

<script>
import api from '@/services/api';

export default {
  name: 'NodeSelector',
  props: {
    // Use v-model:message / v-model:error from parent
    message: { type: String, default: null },
    error: { type: String, default: null }
  },
  emits: ['update:message', 'update:error', 'node-changed'],
  data() {
    return {
      selectedNodeId: 0,  // will be set to leader on mount
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

    // Auto-select leader on initial load
    if (this.leaderId && this.selectedNodeId === 0) {
      this.selectedNodeId = this.leaderId;
      api.selectNode(this.leaderId);

      const nodeStatus = this.getNodeStatusInfo(this.leaderId);
      this.$emit('node-changed', {
        nodeId: this.leaderId,
        status: nodeStatus.status,
        role: nodeStatus.role,
        isOnline: nodeStatus.isOnline
      });
    }

    // Refresh cluster status every 5 seconds
    this._statusInterval = setInterval(() => this.loadClusterStatus(), 5000);
  },
  beforeUnmount() {
    if (this._statusInterval) clearInterval(this._statusInterval);
  },
  methods: {
    // Emit helper to set message in parent
    setMessage(text) {
      this.$emit('update:message', text);
    },
    setError(text) {
      this.$emit('update:error', text);
    },
    clearMessage() {
      this.$emit('update:message', null);
    },
    clearError() {
      this.$emit('update:error', null);
    },

    async loadNodes() {
      try {
        this.nodes = await api.getAvailableNodes();
      } catch (err) {
        console.error('Failed to load nodes:', err);
        // report to parent
        this.setError('Failed to load nodes');
      }
    },

    async loadClusterStatus() {
      try {
        this.clusterStatus = await api.getClusterStatus();
        const leaderNode = this.clusterStatus.nodes.find(n => n.role === 'LEADER');
        const newLeaderId = leaderNode?.id;

        // If leader changed, update selection
        if (newLeaderId && newLeaderId !== this.leaderId) {
          console.log(`Leader changed: ${this.leaderId} -> ${newLeaderId}`);
          const previousLeader = this.leaderId;
          this.leaderId = newLeaderId;

          // If currently selected node is no longer leader or was default, switch to new leader
          if (this.selectedNodeId === previousLeader || this.selectedNodeId === 0) {
            this.selectedNodeId = newLeaderId;
            api.selectNode(newLeaderId);

            const nodeStatus = this.getNodeStatusInfo(newLeaderId);
            this.$emit('node-changed', {
              nodeId: newLeaderId,
              status: nodeStatus.status,
              role: nodeStatus.role,
              isOnline: nodeStatus.isOnline
            });

            // notify parent user-facing success message
            this.setMessage(`Pasirinktas lyderio mazgas: ${newLeaderId}`);
          }
        } else if (newLeaderId) {
          this.leaderId = newLeaderId;
        }
      } catch (err) {
        console.error('Failed to load cluster status:', err);
        this.setError('Failed to load cluster status');
      }
    },

    async onNodeChange() {
      // Refresh cluster status to validate selection
      await this.loadClusterStatus();

      // Validate the selected node
      const nodeStatus = this.getNodeStatusInfo(this.selectedNodeId);

      // If node is offline, show error and do not select
      if (!nodeStatus.isOnline) {
        this.setError(`Mazgas ${this.selectedNodeId} yra OFFLINE. Pasirinkite kitą mazgą.`);
        // If you want to revert the selection to leader:
        // this.selectedNodeId = this.leaderId || 0;
        return;
      }

      // Select node and notify parent
      api.selectNode(this.selectedNodeId);
      this.$emit('node-changed', {
        nodeId: this.selectedNodeId,
        status: nodeStatus.status,
        role: nodeStatus.role,
        isOnline: nodeStatus.isOnline
      });

      // Provide user feedback via parent
      this.setMessage(`Pasirinktas ${nodeStatus.role == 'LEADER'? 'lyderio ': ''}mazgas: ${this.selectedNodeId}`);
      // Optionally auto-dismiss:
      // setTimeout(() => this.clearMessage(), 4000);
    },

    isNodeAvailable(nodeId) {
      if (!this.clusterStatus) return true;
      const node = this.clusterStatus.nodes.find(n => n.id === nodeId);
      return node?.status === 'ONLINE';
    },

    getNodeStatus(nodeId) {
      if (!this.clusterStatus) return 'UNKNOWN';
      const node = this.clusterStatus.nodes.find(n => n.id === nodeId);
      return node?.role || 'UNKNOWN';
    },

    getNodeStatusInfo(nodeId) {
      if (!this.clusterStatus) {
        return { status: 'UNKNOWN', role: 'UNKNOWN', isOnline: false };
      }
      const node = this.clusterStatus.nodes.find(n => n.id === nodeId);
      return {
        status: node?.status || 'OFFLINE',
        role: node?.role || 'UNKNOWN',
        isOnline: node?.status === 'ONLINE'
      };
    },

    getStatusClass(nodeId) {
      const status = this.getNodeStatus(nodeId);
      return {
        'text-success': status === 'LEADER',
        'text-info': status === 'FOLLOWER',
        'text-warning': status === 'CANDIDATE',
        'text-danger': status === 'OFFLINE'
      };
    },

    getStatusBadgeClass(nodeId) {
      const status = this.getNodeStatus(nodeId);
      return {
        'bg-success': status === 'LEADER',
        'bg-info': status === 'FOLLOWER',
        'bg-warning': status === 'CANDIDATE',
        'bg-danger': status === 'OFFLINE' || status === 'UNREACHABLE'
      };
    }
  }
};
</script>

<style scoped>
.node-selector-compact {
  position: sticky;
  top: 0;
  z-index: 100;
  background-color: white;
  padding: 0.75rem 0;
  border-bottom: 1px solid #dee2e6;
  margin-bottom: 1rem;
}

.btn-close-sm {
  padding: 5px;
}

/* ensure compact alerts wrap/responsive on small screens */
.alert-compact {
  display: flex;
  justify-content: space-between;
  align-items: center;
  width: 100%;
  box-sizing: border-box;
  padding: 0.4rem 0.75rem;
  font-size: 0.85rem;
}

.node-info-compact {
  display: flex;
  align-items: center;
  white-space: nowrap;
}

.node-info-compact .badge {
  font-size: 0.75rem;
}

.form-select option:disabled {
  color: #6c757d;
}

.alert-dismissible .btn-close{
  padding: 10px;
}

/* small fade transition */
.fade-enter-active, .fade-leave-active { transition: opacity .18s; }
.fade-enter-from, .fade-leave-to { opacity: 0; }
</style>