<template>
  <div class="container">
    <node-selector @node-changed="onNodeChanged" v-model:message="message" v-model:error="error"/>

    <!-- Main Content -->
    <div class="main-content">
      <div class="text-center mb-4">
        <h1 class="display-5">Paskirstyta Raktų-Reikšmių Duomenų Bazė</h1>
      </div>

    <!-- Section 1: SET and DELETE -->
    <div class="row mb-4">
      <!-- SET Card -->
      <div class="col-md-6">
        <div class="card h-100">
          <div class="card-header bg-success text-white">
            <h5 class="mb-0">Nustatyti Reikšmę (SET)</h5>
          </div>
          <div class="card-body">
            <form @submit.prevent="handleSet">
              <div class="mb-3">
                <label class="form-label">Raktas</label>
                <input
                  v-model="setForm.key"
                  class="form-control"
                  placeholder="Įveskite raktą"
                  required
                />
              </div>
              <div class="mb-3">
                <label class="form-label">Reikšmė</label>
                <textarea
                  v-model="setForm.value"
                  class="form-control"
                  rows="3"
                  placeholder="Įveskite reikšmę"
                  required
                ></textarea>
              </div>
              <button type="submit" class="btn btn-success">Nustatyti</button>
            </form>
          </div>
        </div>
      </div>

      <!-- DELETE Card -->
      <div class="col-md-6">
        <div class="card h-100">
          <div class="card-header bg-danger text-white">
            <h5 class="mb-0">Ištrinti Raktą (DELETE)</h5>
          </div>
          <div class="card-body">
            <form @submit.prevent="handleDelete">
              <div class="mb-3">
                <label class="form-label">Raktas</label>
                <input
                  v-model="deleteKey"
                  class="form-control"
                  placeholder="Įveskite raktą, kurį norite ištrinti"
                  required
                />
              </div>
              <button type="submit" class="btn btn-danger w-100">Ištrinti</button>
            </form>
          </div>
        </div>
      </div>
    </div>

    <!-- Section 2: GET Operations -->
    <div class="row mb-4">
      <!-- GET Single Value -->
      <div class="col-md-6">
        <div class="card h-100">
          <div class="card-header bg-primary text-white">
            <h5 class="mb-0">Gauti Reikšmę (GET)</h5>
          </div>
          <div class="card-body">
            <form @submit.prevent="handleGet">
              <div class="mb-3">
                <label class="form-label">Raktas</label>
                <input
                  v-model="getKey"
                  class="form-control"
                  placeholder="Įveskite raktą"
                  required
                />
              </div>
              <button type="submit" class="btn btn-primary">Gauti</button>
            </form>

            <div v-if="retrievedValue" class="mt-3">
              <div class="alert alert-success">
                <strong>Reikšmė:</strong> <span style="white-space: pre-wrap;">{{ retrievedValue }}</span>
              </div>
            </div>
          </div>
        </div>
      </div>

      <!-- GET Keys with Prefix -->
      <div class="col-md-6">
        <div class="card h-100">
          <div class="card-header bg-info text-white">
            <h5 class="mb-0">Gauti Raktus su Prefiksu</h5>
          </div>
          <div class="card-body">
            <form @submit.prevent="handleGetKeysPrefix">
              <div class="mb-3">
                <label class="form-label">Prefiksas</label>
                <input
                  v-model="prefixForm.prefix"
                  class="form-control"
                  placeholder="Įveskite prefiksą (palikite tuščią visiems)"
                />
              </div>
              <button type="submit" class="btn btn-info">Ieškoti Raktų</button>
            </form>

            <div v-if="prefixResults && prefixResults.length > 0" class="mt-3">
              <h6 class="mb-2">Rasti raktai ({{ prefixResults.length }}):</h6>
              <div class="table-responsive">
                <table class="table table-sm table-striped">
                  <thead>
                    <tr>
                      <th>Raktas</th>
                    </tr>
                  </thead>
                  <tbody>
                    <tr v-for="key in prefixResults" :key="key">
                      <td><code>{{ key }}</code></td>
                    </tr>
                  </tbody>
                </table>
              </div>
            </div>
            <div v-else-if="prefixResults && prefixResults.length === 0" class="mt-3 alert alert-info">
              Nerasta raktų.
            </div>
          </div>
        </div>
      </div>
    </div>

    <!-- GET Keys with Paging -->
    <div class="row mb-4">
      <div class="col-md-12">
        <div class="card">
          <div class="card-header bg-secondary text-white">
            <h5 class="mb-0">Gauti Raktus su Puslapiavimu</h5>
          </div>
          <div class="card-body">
            <form @submit.prevent="handleGetKeysPaging">
              <div class="row">
                <div class="col-md-4">
                  <label class="form-label">Puslapio dydis</label>
                  <input
                    v-model.number="pagingForm.pageSize"
                    type="number"
                    class="form-control"
                    min="1"
                    max="100"
                    required
                  />
                </div>
                <div class="col-md-4">
                  <label class="form-label">Puslapio numeris</label>
                  <input
                    v-model.number="pagingForm.pageNum"
                    type="number"
                    class="form-control"
                    min="1"
                    required
                  />
                </div>
                <div class="col-md-4 d-flex align-items-end">
                  <button type="submit" class="btn btn-secondary w-100">Gauti Puslapį</button>
                </div>
              </div>
            </form>

            <div v-if="pagingResults && pagingResults.keys.length > 0" class="mt-3">
              <div class="d-flex justify-content-between align-items-center mb-2">
                <h6 class="mb-0">
                  Puslapis {{ pagingForm.pageNum }} ({{ pagingResults.keys.length }} raktų iš {{ pagingResults.totalCount }})
                </h6>
                <div>
                  <button
                    v-if="pagingForm.pageNum > 1"
                    class="btn btn-sm btn-outline-secondary me-2"
                    @click="goToPreviousPage"
                  >
                    ← Ankstesnis
                  </button>
                  <button
                    v-if="pagingResults.keys.length === pagingForm.pageSize"
                    class="btn btn-sm btn-outline-secondary"
                    @click="goToNextPage"
                  >
                    Kitas →
                  </button>
                </div>
              </div>
              <div class="table-responsive">
                <table class="table table-sm table-striped">
                  <thead>
                    <tr>
                      <th>Raktas</th>
                    </tr>
                  </thead>
                  <tbody>
                    <tr v-for="key in pagingResults.keys" :key="key">
                      <td><code>{{ key }}</code></td>
                    </tr>
                  </tbody>
                </table>
              </div>
            </div>
            <div v-else-if="pagingResults && pagingResults.keys.length === 0" class="mt-3 alert alert-info">
              Nerasta raktų šiame puslapyje.
            </div>
          </div>
        </div>
      </div>
    </div>

    <!-- GETFF (Forward Range Query) -->
    <div class="row mb-4">
      <div class="col-md-12">
        <div class="card">
          <div class="card-header" style="background-color: #17a2b8; color: white;">
            <h5 class="mb-0">Sąrašas į priekį (GETFF)</h5>
          </div>
          <div class="card-body">
            <form @submit.prevent="handleGetFF">
              <div class="row">
                <div class="col-md-5">
                  <label class="form-label">Pradžios Raktas</label>
                  <input
                    v-model="getffForm.key"
                    class="form-control"
                    placeholder="Raktas, nuo kurio pradėti"
                    required
                  />
                </div>
                <div class="col-md-3">
                  <label class="form-label">Kiekis</label>
                  <input
                    v-model.number="getffForm.count"
                    type="number"
                    class="form-control"
                    min="1"
                    max="100"
                    required
                  />
                </div>
                <div class="col-md-4 d-flex align-items-end">
                  <button type="submit" class="btn w-100" style="background-color: #17a2b8; color: white;">
                    Vykdyti GETFF
                  </button>
                </div>
              </div>
            </form>

            <div v-if="getffResults && getffResults.length > 0" class="mt-3">
              <div class="d-flex justify-content-between align-items-center mb-2">
                <h6 class="mb-0">Rezultatai ({{ getffResults.length }}):</h6>
                <button
                  v-if="getffResults.length === getffForm.count"
                  class="btn btn-sm btn-outline-info"
                  @click="handleGetFFNextPage"
                >
                  Kitas Puslapis →
                </button>
              </div>
              <div class="table-responsive">
                <table class="table table-sm table-striped">
                  <thead>
                    <tr>
                      <th>Raktas</th>
                      <th>Reikšmė</th>
                    </tr>
                  </thead>
                  <tbody>
                    <tr v-for="item in getffResults" :key="item.key">
                      <td><code>{{ item.key }}</code></td>
                      <td style="white-space: pre-wrap;">{{ item.value }}</td>
                    </tr>
                  </tbody>
                </table>
              </div>
            </div>
            <div v-else-if="getffResults && getffResults.length === 0" class="mt-3 alert alert-info">
              Nerasta įrašų.
            </div>
          </div>
        </div>
      </div>
    </div>

    <!-- GETFB (Backward Range Query) -->
    <div class="row mb-4">
      <div class="col-md-12">
        <div class="card">
          <div class="card-header bg-warning text-dark">
            <h5 class="mb-0">Sąrašas atgal (GETFB)</h5>
          </div>
          <div class="card-body">
            <form @submit.prevent="handleGetFB">
              <div class="row">
                <div class="col-md-5">
                  <label class="form-label">Pabaigos Raktas</label>
                  <input
                    v-model="getfbForm.key"
                    class="form-control"
                    placeholder="Raktas, iki kurio imti"
                    required
                  />
                </div>
                <div class="col-md-3">
                  <label class="form-label">Kiekis</label>
                  <input
                    v-model.number="getfbForm.count"
                    type="number"
                    class="form-control"
                    min="1"
                    max="100"
                    required
                  />
                </div>
                <div class="col-md-4 d-flex align-items-end">
                  <button type="submit" class="btn btn-warning w-100">Vykdyti GETFB</button>
                </div>
              </div>
            </form>

            <div v-if="getfbResults && getfbResults.length > 0" class="mt-3">
              <div class="d-flex justify-content-between align-items-center mb-2">
                <h6 class="mb-0">Rezultatai ({{ getfbResults.length }}):</h6>
                <button
                  v-if="getfbResults.length === getfbForm.count"
                  class="btn btn-sm btn-outline-warning"
                  @click="handleGetFBNextPage"
                >
                  ← Kitas Puslapis
                </button>
              </div>
              <div class="table-responsive">
                <table class="table table-sm table-striped">
                  <thead>
                    <tr>
                      <th>Raktas</th>
                      <th>Reikšmė</th>
                    </tr>
                  </thead>
                  <tbody>
                    <tr v-for="item in getfbResults" :key="item.key">
                      <td><code>{{ item.key }}</code></td>
                      <td style="white-space: pre-wrap;">{{ item.value }}</td>
                    </tr>
                  </tbody>
                </table>
              </div>
            </div>
            <div v-else-if="getfbResults && getfbResults.length === 0" class="mt-3 alert alert-info">
              Nerasta įrašų.
            </div>
          </div>
        </div>
      </div>
    </div>
    </div>
  </div>
</template>

<script>
import api from "@/services/api";
import NodeSelector from "@/components/NodeSelector.vue";

export default {
  name: "Home",
  components: {
    NodeSelector
  },
  data() {
    return {
      // GET single value
      getKey: "",
      retrievedValue: null,

      // SET
      setForm: {
        key: "",
        value: "",
      },

      // DELETE
      deleteKey: "",

      // GET Keys with Prefix
      prefixForm: {
        prefix: "",
      },
      prefixResults: null,

      // GET Keys with Paging
      pagingForm: {
        pageSize: 10,
        pageNum: 1,
      },
      pagingResults: null,

      // GETFF
      getffForm: {
        key: "",
        count: 10,
      },
      getffResults: null,
      getffLastKey: null,

      // GETFB
      getfbForm: {
        key: "",
        count: 10,
      },
      getfbResults: null,
      getfbLastKey: null,

      // Messages
      message: null,
      error: null
    };
  },
  methods: {
    async handleGet() {
      this.error = null;
      this.retrievedValue = null;

      try {
        const result = await api.get(this.getKey);
        this.retrievedValue = result.value;
        this.message = `Raktas "${this.getKey}" sėkmingai gautas.`;
      } catch (err) {
        if (err.response?.status === 404) {
          this.error = `Raktas "${this.getKey}" nerastas.`;
        } else {
          this.error = err.response?.data?.error || err.message;
        }
      }
    },

    async handleSet() {
      this.error = null;
      this.message = null;

      try {
        await api.set(this.setForm.key, this.setForm.value);
        this.message = `Raktas "${this.setForm.key}" sėkmingai nustatytas.`;
        this.setForm.key = "";
        this.setForm.value = "";
      } catch (err) {
        this.error = err.response?.data?.error || err.message;
      }
    },

    async handleDelete() {
      this.error = null;
      this.message = null;

      if (!confirm(`Ar tikrai norite ištrinti raktą "${this.deleteKey}"?`)) {
        return;
      }

      try {
        await api.delete(this.deleteKey);
        this.message = `Raktas "${this.deleteKey}" sėkmingai ištrintas.`;
        this.deleteKey = "";
      } catch (err) {
        if (err.response?.status === 404) {
          this.error = `Raktas "${this.deleteKey}" nerastas.`;
        } else {
          this.error = err.response?.data?.error || err.message;
        }
      }
    },

    async handleGetKeysPrefix() {
      this.error = null;
      this.message = null;
      this.prefixResults = null;

      try {
        const result = await api.getKeysPrefix(this.prefixForm.prefix);
        this.prefixResults = result.keys || [];
        this.message = `Rasta ${this.prefixResults.length} raktų su prefiksu "${this.prefixForm.prefix || '(tuščias)'}"`;
      } catch (err) {
        this.error = err.response?.data?.error || err.message;
      }
    },

    async handleGetKeysPaging() {
      this.error = null;
      this.message = null;
      this.pagingResults = null;

      try {
        const result = await api.getKeysPaging(this.pagingForm.pageSize, this.pagingForm.pageNum);
        this.pagingResults = result;
        this.message = `Puslapis ${this.pagingForm.pageNum} įkeltas sėkmingai.`;
      } catch (err) {
        this.error = err.response?.data?.error || err.message;
      }
    },

    goToNextPage() {
      this.pagingForm.pageNum++;
      this.handleGetKeysPaging();
    },

    goToPreviousPage() {
      if (this.pagingForm.pageNum > 1) {
        this.pagingForm.pageNum--;
        this.handleGetKeysPaging();
      }
    },

    async handleGetFF() {
      this.error = null;
      this.message = null;
      this.getffResults = null;
      this.getffLastKey = null;

      try {
        const result = await api.getForward(
          this.getffForm.key,
          this.getffForm.count
        );

        if (!result || !result.results || !Array.isArray(result.results)) {
          this.error = "Netinkamas atsakymo formatas iš serverio";
          console.error("Invalid GETFF response:", result);
          return;
        }

        this.getffResults = result.results.map((r) => ({
          key: r.key,
          value: r.value,
        }));

        if (this.getffResults.length > 0) {
          this.getffLastKey = this.getffResults[this.getffResults.length - 1].key;
        }

        this.message = `GETFF sėkmingai įvykdyta: rasta ${this.getffResults.length} įrašų.`;
      } catch (err) {
        this.error = err.response?.data?.error || err.message;
      }
    },

    async handleGetFFNextPage() {
      if (!this.getffLastKey) return;

      this.error = null;
      this.message = null;

      try {
        const result = await api.getForward(
          this.getffLastKey,
          this.getffForm.count + 1
        );

        if (!result || !result.results || !Array.isArray(result.results)) {
          this.error = "Netinkamas atsakymo formatas iš serverio";
          return;
        }

        const newResults = result.results
          .slice(1)
          .map((r) => ({ key: r.key, value: r.value }));

        if (newResults.length > 0) {
          this.getffResults = newResults;
          this.getffLastKey = newResults[newResults.length - 1].key;
          this.message = `Kitame puslapyje: rasta ${newResults.length} įrašų.`;
        } else {
          this.message = "Daugiau įrašų nėra.";
        }
      } catch (err) {
        this.error = err.response?.data?.error || err.message;
      }
    },

    async handleGetFB() {
      this.error = null;
      this.message = null;
      this.getfbResults = null;
      this.getfbLastKey = null;

      try {
        const result = await api.getBackward(
          this.getfbForm.key,
          this.getfbForm.count
        );

        if (!result || !result.results || !Array.isArray(result.results)) {
          this.error = "Netinkamas atsakymo formatas iš serverio";
          return;
        }

        this.getfbResults = result.results.map((r) => ({
          key: r.key,
          value: r.value,
        }));

        if (this.getfbResults.length > 0) {
          this.getfbLastKey = this.getfbResults[this.getfbResults.length - 1].key;
        }

        this.message = `GETFB sėkmingai įvykdyta: rasta ${this.getfbResults.length} įrašų.`;
      } catch (err) {
        this.error = err.response?.data?.error || err.message;
      }
    },

    async handleGetFBNextPage() {
      if (!this.getfbLastKey) return;

      this.error = null;
      this.message = null;

      try {
        const result = await api.getBackward(
          this.getfbLastKey,
          this.getfbForm.count + 1
        );

        if (!result || !result.results || !Array.isArray(result.results)) {
          this.error = "Netinkamas atsakymo formatas iš serverio";
          return;
        }

        const newResults = result.results
          .slice(1)
          .map((r) => ({ key: r.key, value: r.value }));

        if (newResults.length > 0) {
          this.getfbResults = newResults;
          this.getfbLastKey = newResults[newResults.length - 1].key;
          this.message = `Kitame puslapyje: rasta ${newResults.length} įrašų.`;
        } else {
          this.message = "Daugiau įrašų nėra.";
        }
      } catch (err) {
        this.error = err.response?.data?.error || err.message;
      }
    },

    onNodeChanged(nodeData) {
      // nodeData contains: { nodeId, status, role, isOnline }
      const nodeId = nodeData.nodeId;
      const status = nodeData.status;
      const role = nodeData.role;
      const isOnline = nodeData.isOnline;

      // Clear previous messages
      this.message = null;
      this.error = null;

      // Display appropriate message based on node status
      if (!isOnline || status === 'OFFLINE') {
        this.error = `Mazgas ${nodeId} yra OFFLINE. Pasirinkite kitą mazgą.`;
      } else if (status === 'UNREACHABLE') {
        this.error = `Mazgas ${nodeId} yra NEPASIEKIAMAS. Pasirinkite kitą mazgą.`;
      } else {
        // Node is online - show role info
        const roleText = role === 'LEADER' ? 'LYDERIS' : role === 'FOLLOWER' ? 'SEKĖJAS' : role;
        this.message = `Pasirinktas Mazgas ${nodeId} (${roleText})`;
      }

      // Clear previous results to avoid confusion
      this.retrievedValue = null;
      this.prefixResults = null;
      this.pagingResults = null;
      this.getffResults = null;
      this.getfbResults = null;
    }
  },
};
</script>

<style scoped lang="scss">
.main-content {
  max-width: 1200px;
}

.container {
  max-width: 1200px;
  padding-top: 1rem;
}

.card {
  box-shadow: 0 2px 4px rgba(0, 0, 0, 0.1);

  &:hover {
    box-shadow: 0 4px 8px rgba(0, 0, 0, 0.15);
  }
}

code {
  background-color: #f8f9fa;
  padding: 2px 6px;
  border-radius: 3px;
  font-size: 0.9em;
}

.table-responsive {
  max-height: 400px;
  overflow-y: auto;
}
</style>
