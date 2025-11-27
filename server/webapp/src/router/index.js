import { createRouter, createWebHistory } from 'vue-router';
import Home from '../views/Home.vue';
import ClusterView from '../views/ClusterView.vue';

const routes = [
  {
    path: '/',
    name: 'home',
    component: Home
  },
  {
    path: '/cluster',
    name: 'cluster',
    component: ClusterView
  }
];

const router = createRouter({
  history: createWebHistory(),
  routes
});

export default router;
