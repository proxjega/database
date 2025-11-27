<template>
  <div class="container mt-4">
    <h2>Database CRUD Operations</h2>

    <div class="row">
      <div class="col-md-6">
        <div class="card mb-3">
          <div class="card-header">
            {{ isEdit ? 'Update' : 'Create' }} Key-Value Pair
          </div>
          <div class="card-body">
            <form @submit.prevent="handleSubmit">
              <div class="mb-3">
                <label class="form-label">Key</label>
                <input
                  v-model="form.key"
                  class="form-control"
                  placeholder="Enter key"
                  required
                  :disabled="isEdit"
                />
              </div>
              <div class="mb-3">
                <label class="form-label">Value</label>
                <textarea
                  v-model="form.value"
                  class="form-control"
                  rows="3"
                  placeholder="Enter value"
                  required
                ></textarea>
              </div>
              <button type="submit" class="btn btn-primary">
                {{ isEdit ? 'Update' : 'Create' }}
              </button>
              <button
                v-if="isEdit"
                type="button"
                class="btn btn-secondary ms-2"
                @click="resetForm"
              >
                Cancel
              </button>
            </form>
          </div>
        </div>

        <div class="card">
          <div class="card-header">
            Get Value by Key
          </div>
          <div class="card-body">
            <form @submit.prevent="handleGet">
              <div class="mb-3">
                <label class="form-label">Key</label>
                <div class="input-group">
                  <input
                    v-model="getKey"
                    class="form-control"
                    placeholder="Enter key to retrieve"
                    required
                  />
                  <button type="submit" class="btn btn-outline-primary">
                    Get
                  </button>
                </div>
              </div>
            </form>

            <div v-if="retrievedValue" class="alert alert-success">
              <strong>Value:</strong> {{ retrievedValue }}
            </div>
          </div>
        </div>
      </div>

      <div class="col-md-6">
        <div v-if="message" class="alert" :class="messageClass">
          {{ message }}
        </div>

        <div v-if="error" class="alert alert-danger">
          {{ error }}
        </div>
      </div>
    </div>
  </div>
</template>

<script>
import api from '@/services/api';

export default {
  name: 'CrudView',
  data() {
    return {
      form: {
        key: '',
        value: ''
      },
      getKey: '',
      retrievedValue: null,
      isEdit: false,
      message: null,
      error: null,
      messageClass: 'alert-success'
    };
  },
  methods: {
    async handleSubmit() {
      this.error = null;
      this.message = null;

      try {
        await api.set(this.form.key, this.form.value);
        this.message = `Key "${this.form.key}" ${this.isEdit ? 'updated' : 'created'} successfully!`;
        this.messageClass = 'alert-success';
        this.resetForm();
      } catch (err) {
        this.error = err.response?.data?.error || err.message;
      }
    },

    async handleGet() {
      this.error = null;
      this.retrievedValue = null;

      try {
        const result = await api.get(this.getKey);
        this.retrievedValue = result.value;
      } catch (err) {
        if (err.response?.status === 404) {
          this.error = `Key "${this.getKey}" not found`;
        } else {
          this.error = err.response?.data?.error || err.message;
        }
      }
    },

    resetForm() {
      this.form.key = '';
      this.form.value = '';
      this.isEdit = false;
    }
  },
  mounted() {
    // Check if editing from query params
    if (this.$route.query.key) {
      this.form.key = this.$route.query.key;
      this.form.value = this.$route.query.value || '';
      this.isEdit = true;
    }
  }
};
</script>

<style scoped lang="scss">
.container {
  max-width: 1200px;
}
</style>
