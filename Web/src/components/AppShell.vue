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
import { onMounted } from 'vue';
import { usePlcStore } from '../stores/plcStore';
import UnifiedStatusBar from './UnifiedStatusBar.vue';
const plcStore = usePlcStore();
onMounted(() => plcStore.start());
</script>
