<template>
  <div id="app">
    <nav class="navbar navbar-expand-lg navbar-dark bg-dark">
      <div class="container-fluid">
        <router-link class="navbar-brand" to="/">Database Web App</router-link>
        <button class="navbar-toggler" type="button" data-bs-toggle="collapse" data-bs-target="#navbarNav">
          <span class="navbar-toggler-icon"></span>
        </button>
        <div class="collapse navbar-collapse" id="navbarNav">
          <ul class="navbar-nav">
            <li class="nav-item">
              <router-link class="nav-link" to="/">Home</router-link>
            </li>
            <li class="nav-item">
              <router-link class="nav-link" to="/crud">CRUD</router-link>
            </li>
            <li class="nav-item">
              <router-link class="nav-link" to="/browse">Browse</router-link>
            </li>
            <li class="nav-item">
              <router-link class="nav-link" to="/cluster">Cluster</router-link>
            </li>
          </ul>
        </div>
      </div>
    </nav>

    <main class="container mt-4">
      <router-view />
    </main>

    <footer class="mt-5 py-3 bg-light text-center">
      <div class="container">
        <span class="text-muted">Distributed Key-Value Database Web Application</span>
      </div>
    </footer>
  </div>
</template>

<script>
export default {
  name: 'App'
};
</script>

<style scoped>
#app {
  min-height: 100vh;
  display: flex;
  flex-direction: column;
}

main {
  flex: 1;
}

.router-link-active {
  font-weight: bold;
}
</style>
