import { createRouter, createWebHistory } from 'vue-router';
import Home from '../views/Home.vue';
import CrudView from '../views/CrudView.vue';
import BrowseView from '../views/BrowseView.vue';
import ClusterView from '../views/ClusterView.vue';

const routes = [
  {
    path: '/',
    name: 'home',
    component: Home
  },
  {
    path: '/crud',
    name: 'crud',
    component: CrudView
  },
  {
    path: '/browse',
    name: 'browse',
    component: BrowseView
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
