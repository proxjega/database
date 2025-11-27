<template>
  <div class="pagination-container">
    <nav aria-label="Database pagination">
      <ul class="pagination justify-content-center">
        <li class="page-item" :class="{ disabled: !hasPrevious }">
          <button class="page-link" @click="loadPrevious" :disabled="!hasPrevious">
            &laquo; Previous
          </button>
        </li>

        <li class="page-item active">
          <span class="page-link">
            Page {{ currentPage }}
          </span>
        </li>

        <li class="page-item" :class="{ disabled: !hasNext }">
          <button class="page-link" @click="loadNext" :disabled="!hasNext">
            Next &raquo;
          </button>
        </li>
      </ul>
    </nav>

    <div class="pagination-info text-center">
      <small class="text-muted">
        Showing {{ items.length }} items
        <span v-if="lastKey"> (last key: <code>{{ lastKey }}</code>)</span>
      </small>
    </div>
  </div>
</template>

<script>
export default {
  name: 'Pagination',
  props: {
    items: {
      type: Array,
      required: true
    },
    pageSize: {
      type: Number,
      default: 10
    }
  },
  data() {
    return {
      currentPage: 1,
      keyStack: [], // Track navigation history
      lastKey: null,
      direction: 'forward' // 'forward' or 'backward'
    };
  },
  computed: {
    hasPrevious() {
      return this.keyStack.length > 0;
    },
    hasNext() {
      return this.items.length === this.pageSize;
    }
  },
  methods: {
    loadNext() {
      if (!this.hasNext) return;

      // Track last key of current page
      const lastItem = this.items[this.items.length - 1];
      this.keyStack.push(lastItem.key);
      this.lastKey = lastItem.key;
      this.direction = 'forward';
      this.currentPage++;

      this.$emit('page-change', {
        direction: 'forward',
        startKey: lastItem.key,
        count: this.pageSize
      });
    },

    loadPrevious() {
      if (!this.hasPrevious) return;

      // Pop previous key from stack
      const previousKey = this.keyStack.pop();
      this.lastKey = previousKey;
      this.direction = 'backward';
      this.currentPage--;

      this.$emit('page-change', {
        direction: 'backward',
        startKey: previousKey,
        count: this.pageSize
      });
    },

    reset() {
      this.keyStack = [];
      this.currentPage = 1;
      this.lastKey = null;
      this.direction = 'forward';
    }
  }
};
</script>

<style scoped lang="scss">
.pagination-container {
  margin-top: 1rem;

  .pagination-info {
    text-align: center;
    margin-top: 0.5rem;
  }
}
</style>
