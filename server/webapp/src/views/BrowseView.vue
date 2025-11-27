<template>
  <div class="container mt-4">
    <h2>Browse Database</h2>

    <div class="row mb-3">
      <div class="col-md-6">
        <label class="form-label">Start browsing from key:</label>
        <div class="input-group">
          <input
            v-model="startKey"
            class="form-control"
            placeholder="Enter starting key (or leave empty for 'a')"
          />
          <button class="btn btn-primary" @click="loadFirstPage">
            Browse
          </button>
        </div>
      </div>
      <div class="col-md-3">
        <label class="form-label">Page Size:</label>
        <select v-model.number="pageSize" class="form-select">
          <option :value="5">5 items</option>
          <option :value="10">10 items</option>
          <option :value="20">20 items</option>
          <option :value="50">50 items</option>
        </select>
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

    <div v-else-if="items.length > 0">
      <table class="table table-striped table-hover">
        <thead>
          <tr>
            <th>Key</th>
            <th>Value</th>
            <th>Actions</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="item in items" :key="item.key">
            <td><code>{{ item.key }}</code></td>
            <td>{{ item.value }}</td>
            <td>
              <button
                class="btn btn-sm btn-outline-primary me-1"
                @click="editItem(item)"
              >
                Edit
              </button>
              <button
                class="btn btn-sm btn-outline-danger"
                @click="deleteItem(item.key)"
              >
                Delete
              </button>
            </td>
          </tr>
        </tbody>
      </table>

      <Pagination
        :items="items"
        :page-size="pageSize"
        @page-change="handlePageChange"
        ref="pagination"
      />
    </div>

    <div v-else class="alert alert-info">
      No items found. Start browsing from a key or create some data first.
    </div>
  </div>
</template>

<script>
import api from '@/services/api';
import Pagination from '@/components/Pagination.vue';

export default {
  name: 'BrowseView',
  components: { Pagination },
  data() {
    return {
      items: [],
      startKey: '',
      pageSize: 10,
      loading: false,
      error: null,
      currentDirection: 'forward'
    };
  },
  methods: {
    async loadFirstPage() {
      this.loading = true;
      this.error = null;

      try {
        // Reset pagination
        if (this.$refs.pagination) {
          this.$refs.pagination.reset();
        }

        const key = this.startKey || 'a'; // Default to 'a' if empty
        const result = await api.getForward(key, this.pageSize);
        this.items = result.results.map(r => ({ key: r.key, value: r.value }));
        this.currentDirection = 'forward';
      } catch (err) {
        this.error = err.response?.data?.error || err.message;
      } finally {
        this.loading = false;
      }
    },

    async handlePageChange({ direction, startKey, count }) {
      this.loading = true;
      this.error = null;

      try {
        let result;
        if (direction === 'forward') {
          result = await api.getForward(startKey, count);
        } else {
          result = await api.getBackward(startKey, count);
        }

        this.items = result.results.map(r => ({ key: r.key, value: r.value }));
        this.currentDirection = direction;
      } catch (err) {
        this.error = err.response?.data?.error || err.message;
      } finally {
        this.loading = false;
      }
    },

    editItem(item) {
      this.$router.push({
        name: 'crud',
        query: { key: item.key, value: item.value }
      });
    },

    async deleteItem(key) {
      if (!confirm(`Delete key "${key}"?`)) return;

      try {
        await api.delete(key);
        // Reload current page
        await this.loadFirstPage();
      } catch (err) {
        this.error = err.response?.data?.error || err.message;
      }
    }
  },
  mounted() {
    // Auto-load first page with default key
    this.loadFirstPage();
  }
};
</script>

<style scoped lang="scss">
.container {
  max-width: 1200px;
}

table {
  code {
    background-color: #f8f9fa;
    padding: 2px 6px;
    border-radius: 3px;
    font-size: 0.9em;
  }
}
</style>
