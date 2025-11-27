<template>
  <div class="container">
    <div class="text-center mb-5">
      <h1 class="display-4">Distributed Key-Value Database</h1>
      <p class="lead">Web interface for managing your distributed database cluster</p>
    </div>

    <div class="row">
      <div class="col-md-4 mb-4">
        <div class="card h-100">
          <div class="card-body">
            <h5 class="card-title">CRUD Operations</h5>
            <p class="card-text">Create, read, update, and delete key-value pairs in the database.</p>
            <router-link to="/crud" class="btn btn-primary">Go to CRUD</router-link>
          </div>
        </div>
      </div>

      <div class="col-md-4 mb-4">
        <div class="card h-100">
          <div class="card-body">
            <h5 class="card-title">Browse Database</h5>
            <p class="card-text">Browse through database entries with pagination using GETFF/GETFB.</p>
            <router-link to="/browse" class="btn btn-primary">Go to Browse</router-link>
          </div>
        </div>
      </div>

      <div class="col-md-4 mb-4">
        <div class="card h-100">
          <div class="card-body">
            <h5 class="card-title">Cluster Status</h5>
            <p class="card-text">View cluster health and current leader information.</p>
            <router-link to="/cluster" class="btn btn-primary">Go to Cluster</router-link>
          </div>
        </div>
      </div>
    </div>

    <div class="mt-5">
      <h3>Features</h3>
      <ul class="list-group">
        <li class="list-group-item">
          <strong>Leader Discovery:</strong> Automatic detection of current cluster leader
        </li>
        <li class="list-group-item">
          <strong>Pagination:</strong> Efficient browsing with forward/backward range queries
        </li>
        <li class="list-group-item">
          <strong>Failover Support:</strong> Automatic reconnection to new leader during failover
        </li>
        <li class="list-group-item">
          <strong>Real-time Operations:</strong> Direct interaction with the replication cluster
        </li>
      </ul>
    </div>
  </div>
</template>

<script>
export default {
  name: 'Home'
};
</script>

<style scoped lang="scss">
.container {
  max-width: 1200px;
}

.card {
  transition: transform 0.2s, box-shadow 0.2s;

  &:hover {
    transform: translateY(-4px);
    box-shadow: 0 6px 12px rgba(0, 0, 0, 0.15);
  }
}
</style>
