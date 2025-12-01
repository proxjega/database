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

    <div v-if="loading" class="text-center loading-spinner">
      <div class="spinner-border" role="status">
        <span class="visually-hidden">Kraunama...</span>
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
              <h5 class="mb-0">Dabartinis Lyderis</h5>
            </div>
            <div class="card-body">
              <div v-if="leaderInfo">
                <p class="mb-2">
                  <strong>Adresas:</strong> <code>{{ leaderInfo.host }}</code>
                </p>
                <p class="mb-2">
                  <strong>Portas:</strong> <code>{{ leaderInfo.port }}</code>
                </p>
                <p class="mb-2">
                  <strong>Būsena:</strong>
                  <span class="badge bg-success">{{ leaderInfo.status === 'active' ? 'Aktyvus' : leaderInfo.status }}</span>
                </p>
                <p class="mb-0">
                  <strong>Paskutinis Atnaujinimas:</strong> {{ formatTimestamp(leaderInfo.timestamp) }}
                </p>
              </div>
              <div v-else class="text-muted">
                Lyderio informacija nepasiekiama
              </div>
            </div>
          </div>
        </div>

        <div class="col-md-6 mb-4">
          <div class="card">
            <div class="card-header bg-info text-white">
              <h5 class="mb-0">HTTP Serverio Būsena</h5>
            </div>
            <div class="card-body">
              <div v-if="healthInfo">
                <p class="mb-2">
                  <strong>Būsena:</strong>
                  <span class="badge bg-success">{{ healthInfo.status === 'ok' ? 'Veikia' : healthInfo.status }}</span>
                </p>
                <p class="mb-2">
                  <strong>Prijungtas prie Lyderio:</strong> <code>{{ healthInfo.leader_host }}</code>
                </p>
                <p class="mb-0">
                  <strong>Lyderio Portas:</strong> <code>{{ healthInfo.leader_port }}</code>
                </p>
              </div>
              <div v-else class="text-muted">
                Būsenos informacija nepasiekiama
              </div>
            </div>
          </div>
        </div>
      </div>

      <div class="row">
        <div class="col-md-12">
          <div class="card">
            <div class="card-header">
              <h5 class="mb-0">Klasterio Konfigūracija</h5>
            </div>
            <div class="card-body">
              <table class="table table-sm">
                <thead>
                  <tr>
                    <th>Mazgas</th>
                    <th>Kliento Portas (Lyderis)</th>
                    <th>Skaitymo Portas (Sekėjas)</th>
                    <th>Replikacijos Portas</th>
                    <th>Valdymo Portas</th>
                  </tr>
                </thead>
                <tbody>
                  <tr v-for="node in clusterNodes" :key="node.id">
                    <td><strong>Mazgas {{ node.id }}</strong></td>
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
      message: null,
      optimizeError: null,
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

      // Clear the leader discovery cache to force fresh lookup
      api.leaderDiscoveryCache = null;

      try {
        // Fetch leader info and health in parallel
        const [leaderResponse, healthResponse] = await Promise.all([
          api.discoverLeader(),
          api.health()
        ]);
        this.leaderInfo = leaderResponse;
        this.healthInfo = healthResponse;
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
