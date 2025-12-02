<template>
  <div class="container mt-4">
    <h2>Klasterio Būsena</h2>

    <div class="row mb-4">
      <div class="col-md-12">
        <button class="btn btn-primary me-2" @click="refreshStatus">
          Atnaujinti Būseną
        </button>
        <button class="btn btn-warning" @click="optimizeDatabase">
          Optimizuoti Duomenų Bazę
        </button>
      </div>
    </div>

    <!-- Success/Error Messages -->
    <div v-if="message" class="alert alert-success alert-dismissible fade show" role="alert">
      {{ message }}
      <button type="button" class="btn-close" @click="message = null"></button>
    </div>

    <div v-if="optimizeError" class="alert alert-danger alert-dismissible fade show" role="alert">
      {{ optimizeError }}
      <button type="button" class="btn-close" @click="optimizeError = null"></button>
    </div>

    <!-- Split Brain Warning -->
    <div v-if="clusterStatus && clusterStatus.splitBrain" class="alert alert-danger" role="alert">
      <strong>ĮSPĖJIMAS: Aptiktas Split Brain!</strong> Daugiau nei vienas mazgas raportoja kaip lyderis. Klasteris yra nestabilioje būsenoje.
    </div>

    <div v-if="loading" class="text-center loading-spinner">
      <div class="spinner-border" role="status">
        <span class="visually-hidden">Kraunama...</span>
      </div>
    </div>

    <div v-else-if="error" class="alert alert-danger">
      {{ error }}
    </div>

    <div v-else-if="clusterStatus">
      <!-- Leader Info Card -->
      <div class="row mb-4">
        <div class="col-md-12">
          <div class="card">
            <div class="card-header" :class="clusterStatus.leader.available ? 'bg-success text-white' : 'bg-danger text-white'">
              <h5 class="mb-0">Dabartinis Lyderis</h5>
            </div>
            <div class="card-body">
              <div v-if="clusterStatus.leader.available">
                <p class="mb-2">
                  <strong>Mazgas ID:</strong> <code>{{ clusterStatus.leader.id }}</code>
                </p>
                <p class="mb-2">
                  <strong>Adresas:</strong> <code>{{ clusterStatus.leader.host }}</code>
                </p>
                <p class="mb-0">
                  <strong>Būsena:</strong>
                  <span class="badge bg-success">Aktyvus</span>
                </p>
              </div>
              <div v-else class="text-muted">
                Lyderis šiuo metu nepasiekiamas arba vyksta rinkimai
              </div>
            </div>
          </div>
        </div>
      </div>

      <!-- Cluster Nodes Table -->
      <div class="row">
        <div class="col-md-12">
          <div class="card">
            <div class="card-header">
              <h5 class="mb-0">Klasterio Mazgai</h5>
            </div>
            <div class="card-body">
              <div class="table-responsive">
                <table class="table table-hover">
                  <thead>
                    <tr>
                      <th>ID</th>
                      <th>Adresas</th>
                      <th>Rolė</th>
                      <th>Būsena</th>
                      <th>Term</th>
                      <th>LSN</th>
                    </tr>
                  </thead>
                  <tbody>
                    <tr v-for="node in clusterStatus.nodes" :key="node.id"
                        :class="getNodeRowClass(node)">
                      <td><strong>{{ node.id }}</strong></td>
                      <td><code>{{ node.host }}</code></td>
                      <td>
                        <span class="badge" :class="getRoleBadgeClass(node.role)">
                          {{ getRoleText(node.role) }}
                        </span>
                      </td>
                      <td>
                        <span class="badge" :class="getStatusBadgeClass(node.status)">
                          {{ getStatusText(node.status) }}
                        </span>
                      </td>
                      <td>{{ node.term || '-' }}</td>
                      <td><code>{{ node.lsn || '-' }}</code></td>
                    </tr>
                  </tbody>
                </table>
              </div>
              <div class="mt-3 text-muted small">
                <p class="mb-1"><strong>Legenda:</strong></p>
                <ul class="mb-0">
                  <li><strong>LSN:</strong> Log Sequence Number (replikacijos sekos numeris)</li>
                  <li><strong>Term:</strong> Rinkimų kadencijos numeris</li>
                  <li><strong>HB:</strong> Heartbeat (širdies plakimo amžius sekundėmis)</li>
                  <li><strong>Sekėjai:</strong> Follower statusai matomi tik lyderio eilutėje</li>
                </ul>
              </div>
            </div>
          </div>
        </div>
      </div>

      <!-- Metadata -->
      <div class="row mt-3">
        <div class="col-md-12">
          <div class="text-muted small text-end">
            Paskutinis atnaujinimas: {{ formatTimestamp(clusterStatus.timestamp) }}
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
      message: null,
      optimizeError: null,
      clusterStatus: null
    };
  },
  methods: {
    async refreshStatus() {
      this.loading = true;
      this.error = null;

      try {
        const response = await api.getClusterStatus();
        this.clusterStatus = response;
      } catch (err) {
        this.error = err.message || 'Nepavyko gauti klasterio būsenos';
      } finally {
        this.loading = false;
      }
    },

    async optimizeDatabase() {
      this.optimizeError = null;
      this.message = null;

      if (!confirm('Ar tikrai norite optimizuoti duomenų bazę? Tai gali užtrukti.')) {
        return;
      }

      try {
        await api.optimize();
        this.message = 'Duomenų bazė sėkmingai optimizuota!';
      } catch (err) {
        this.optimizeError = err.response?.data?.error || err.message || 'Optimizavimas nepavyko';
      }
    },

    getNodeRowClass(node) {
      if (node.role === 'LEADER') return 'table-success';
      if (node.status === 'OFFLINE') return 'table-danger';
      return '';
    },

    getRoleBadgeClass(role) {
      switch (role) {
        case 'LEADER': return 'bg-success';
        case 'FOLLOWER': return 'bg-primary';
        case 'CANDIDATE': return 'bg-warning';
        default: return 'bg-secondary';
      }
    },

    getRoleText(role) {
      switch (role) {
        case 'LEADER': return 'Lyderis';
        case 'FOLLOWER': return 'Sekėjas';
        case 'CANDIDATE': return 'Kandidatas';
        default: return role || 'Nežinoma';
      }
    },

    getStatusBadgeClass(status) {
      switch (status) {
        case 'ONLINE': return 'bg-success';
        case 'OFFLINE': return 'bg-danger';
        default: return 'bg-secondary';
      }
    },

    getStatusText(status) {
      switch (status) {
        case 'ONLINE': return 'Veikia';
        case 'OFFLINE': return 'Nepasiekiamas';
        default: return status || 'Nežinoma';
      }
    },

    getFollowerBadgeClass(status) {
      switch (status) {
        case 'ALIVE': return 'bg-success';
        case 'RECENT': return 'bg-info';
        case 'DEAD': return 'bg-danger';
        default: return 'bg-secondary';
      }
    },

    formatHeartbeatAge(ageMs) {
      if (ageMs === undefined || ageMs === null) return '-';
      const seconds = Math.floor(ageMs / 1000);
      if (seconds < 60) return `${seconds}s`;
      const minutes = Math.floor(seconds / 60);
      return `${minutes}m ${seconds % 60}s`;
    },

    formatTimestamp(timestamp) {
      if (!timestamp) return 'Nėra duomenų';
      return new Date(timestamp).toLocaleString('lt-LT');
    }
  },
  mounted() {
    this.refreshStatus();
  }
};
</script>

<style scoped lang="scss">
.container {
  max-width: 1400px;
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

.table-responsive {
  overflow-x: auto;
}

.badge {
  font-size: 0.85em;
}

.table-success {
  background-color: rgba(25, 135, 84, 0.1);
}

.table-danger {
  background-color: rgba(220, 53, 69, 0.1);
}
</style>
