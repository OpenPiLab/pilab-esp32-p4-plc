import { defineConfig } from 'vite';
import vue from '@vitejs/plugin-vue';
import { viteSingleFile } from 'vite-plugin-singlefile';

export default defineConfig({
  plugins: [
    vue(),
    viteSingleFile(),
  ],

  // Keep paths relative so the normal app can still be opened from filesystems,
  // GitHub Pages subfolders, or ESP32-hosted paths if needed.
  base: './',

  build: {
    // Put everything into one HTML file instead of separate CSS/JS asset files.
    cssCodeSplit: false,
    assetsInlineLimit: 100000000,

    rollupOptions: {
      output: {
        // Prevent Rollup/Vite from creating separate dynamic-import chunks.
        inlineDynamicImports: true,
      },
    },
  },
});
