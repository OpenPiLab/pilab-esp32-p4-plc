import { createRouter, createWebHistory } from 'vue-router';
import CommandCenter from './pages/CommandCenter.vue';
import ScriptConsole from './pages/ScriptConsole.vue';
import HmiDesigner from './pages/HmiDesigner.vue';
import TagRegistry from './pages/TagRegistry.vue';
import FileManager from './pages/FileManager.vue';
import Settings from './pages/Settings.vue';

export const router = createRouter({
  history: createWebHistory(),
  routes: [
    { path: '/', component: CommandCenter },
    { path: '/script', component: ScriptConsole },
    { path: '/editor', redirect: '/script' },
    { path: '/hmi', component: HmiDesigner },
    { path: '/tags', component: TagRegistry },
    { path: '/files', component: FileManager },
    { path: '/settings', component: Settings },
  ],
});
