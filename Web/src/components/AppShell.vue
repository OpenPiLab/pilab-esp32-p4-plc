<template>
  <div class="app-shell">
    <header class="app-topbar">
      <div class="app-topbar-inner">
        <div class="app-brand-block">
          <RouterLink to="/" class="app-brand" style="text-decoration:none">
            <span class="app-brand-title">PiLab PLC</span>
            <span class="app-brand-sub">ESP32-P4 industrial controller</span>
          </RouterLink>
          <UnifiedStatusBar />
        </div>
        <nav class="app-nav">
          <RouterLink to="/">Command</RouterLink>
          <RouterLink to="/script">Script</RouterLink>
          <RouterLink to="/hmi">HMI</RouterLink>
          <RouterLink to="/ladder">Ladder</RouterLink>
          <RouterLink to="/tags">Tags</RouterLink>
          <RouterLink to="/files">Files</RouterLink>
          <RouterLink to="/settings" class="app-settings-link" title="Settings" aria-label="Settings">⚙</RouterLink>
        </nav>
      </div>
    </header>
    <main class="app-content"><RouterView /></main>
  </div>
</template>

<script setup>
import { onBeforeUnmount, onMounted } from 'vue';
import { usePlcStore } from '../stores/plcStore';
import { ensureTagStoreLoaded, useTagStore } from '../stores/tagStore';
import UnifiedStatusBar from './UnifiedStatusBar.vue';
const plcStore = usePlcStore();
const tagStore = useTagStore();
function beforeUnloadTagGuard(e) {
  if (!tagStore.dirty) return;
  e.preventDefault();
  e.returnValue = '';
}
onMounted(() => {
  plcStore.start();
  // Prime the app-wide tag registry once. This keeps Tags/Ladder/Script/HMI
  // from each owning separate first-load behavior, while explicit Reload on
  // the Tags page can still force a fresh /api/tags read.
  ensureTagStoreLoaded().catch(() => {});
  window.addEventListener('beforeunload', beforeUnloadTagGuard);
});
onBeforeUnmount(() => {
  window.removeEventListener('beforeunload', beforeUnloadTagGuard);
});
</script>
