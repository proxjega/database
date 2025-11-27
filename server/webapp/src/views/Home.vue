<template>
  <div class="container mt-4">
    <div class="text-center mb-4">
      <h1 class="display-5">Paskirstyta Raktų-Reikšmių Duomenų Bazė</h1>
    </div>

    <!-- Error/Success Messages -->
    <div v-if="message" class="alert alert-success alert-dismissible fade show" role="alert">
      {{ message }}
      <button type="button" class="btn-close" @click="message = null"></button>
    </div>

    <div v-if="error" class="alert alert-danger alert-dismissible fade show" role="alert">
      {{ error }}
      <button type="button" class="btn-close" @click="error = null"></button>
    </div>

    <!-- Row 1: GET and SET -->
    <div class="row mb-4">
      <!-- GET Card -->
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
                <strong>Reikšmė:</strong> {{ retrievedValue }}
              </div>
            </div>
          </div>
        </div>
      </div>

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
    </div>

    <!-- Row 2: DELETE -->
    <div class="row mb-4">
      <div class="col-md-12">
        <div class="card">
          <div class="card-header bg-danger text-white">
            <h5 class="mb-0">Ištrinti Raktą (DELETE)</h5>
          </div>
          <div class="card-body">
            <form @submit.prevent="handleDelete">
              <div class="row">
                <div class="col-md-8">
                  <label class="form-label">Raktas</label>
                  <input
                    v-model="deleteKey"
                    class="form-control"
                    placeholder="Įveskite raktą, kurį norite ištrinti"
                    required
                  />
                </div>
                <div class="col-md-4 d-flex align-items-end">
                  <button type="submit" class="btn btn-danger w-100">Ištrinti</button>
                </div>
              </div>
            </form>
          </div>
        </div>
      </div>
    </div>

    <!-- Row 3: GETFF (Forward) -->
    <div class="row mb-4">
      <div class="col-md-12">
        <div class="card">
          <div class="card-header bg-info text-white">
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
                  <button type="submit" class="btn btn-info w-100">Vykdyti GETFF</button>
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
                      <td>{{ item.value }}</td>
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

    <!-- Row 4: GETFB (Backward) -->
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
                      <td>{{ item.value }}</td>
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
</template>

<script>
import api from '@/services/api';

export default {
  name: 'Home',
  data() {
    return {
      // GET
      getKey: '',
      retrievedValue: null,

      // SET
      setForm: {
        key: '',
        value: ''
      },

      // DELETE
      deleteKey: '',

      // GETFF
      getffForm: {
        key: '',
        count: 10
      },
      getffResults: null,
      getffLastKey: null,

      // GETFB
      getfbForm: {
        key: '',
        count: 10
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
        this.setForm.key = '';
        this.setForm.value = '';
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
        this.deleteKey = '';
      } catch (err) {
        if (err.response?.status === 404) {
          this.error = `Raktas "${this.deleteKey}" nerastas.`;
        } else {
          this.error = err.response?.data?.error || err.message;
        }
      }
    },

    async handleGetFF() {
      this.error = null;
      this.message = null;
      this.getffResults = null;
      this.getffLastKey = null;

      try {
        const result = await api.getForward(this.getffForm.key, this.getffForm.count);

        // Check if results array exists
        if (!result || !result.results || !Array.isArray(result.results)) {
          this.error = 'Netinkamas atsakymo formatas iš serverio';
          console.error('Invalid GETFF response:', result);
          return;
        }

        this.getffResults = result.results.map(r => ({ key: r.key, value: r.value }));

        // Save last key for pagination
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
        // Start from the next key after the last one
        const result = await api.getForward(this.getffLastKey, this.getffForm.count + 1);

        // Check if results array exists
        if (!result || !result.results || !Array.isArray(result.results)) {
          this.error = 'Netinkamas atsakymo formatas iš serverio';
          console.error('Invalid GETFF response:', result);
          return;
        }

        // Skip the first result (it's the last key from previous page)
        const newResults = result.results.slice(1).map(r => ({ key: r.key, value: r.value }));

        if (newResults.length > 0) {
          this.getffResults = newResults;
          this.getffLastKey = newResults[newResults.length - 1].key;
          this.message = `Kitame puslapyje: rasta ${newResults.length} įrašų.`;
        } else {
          this.message = 'Daugiau įrašų nėra.';
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
        const result = await api.getBackward(this.getfbForm.key, this.getfbForm.count);

        // Check if results array exists
        if (!result || !result.results || !Array.isArray(result.results)) {
          this.error = 'Netinkamas atsakymo formatas iš serverio';
          console.error('Invalid GETFB response:', result);
          return;
        }

        this.getfbResults = result.results.map(r => ({ key: r.key, value: r.value }));

        // Save last key for pagination
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
        // Start from the next key before the last one (going backwards)
        const result = await api.getBackward(this.getfbLastKey, this.getfbForm.count + 1);

        // Check if results array exists
        if (!result || !result.results || !Array.isArray(result.results)) {
          this.error = 'Netinkamas atsakymo formatas iš serverio';
          console.error('Invalid GETFB response:', result);
          return;
        }

        // Skip the first result (it's the last key from previous page)
        const newResults = result.results.slice(1).map(r => ({ key: r.key, value: r.value }));

        if (newResults.length > 0) {
          this.getfbResults = newResults;
          this.getfbLastKey = newResults[newResults.length - 1].key;
          this.message = `Kitame puslapyje: rasta ${newResults.length} įrašų.`;
        } else {
          this.message = 'Daugiau įrašų nėra.';
        }
      } catch (err) {
        this.error = err.response?.data?.error || err.message;
      }
    }
  }
};
</script>

<style scoped lang="scss">
.container {
  max-width: 1200px;
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
