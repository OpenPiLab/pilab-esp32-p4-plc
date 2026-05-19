import { defineConfig } from 'vite';
import vue from '@vitejs/plugin-vue';

// Change this to your ESP32-P4 IP when running npm run dev.
const P4_TARGET = process.env.P4_TARGET || 'http://192.168.5.210';

export default defineConfig({
  plugins: [vue()],

  server: {
    proxy: {
      '/api': {
        target: P4_TARGET,
        changeOrigin: true,
      },
    },
  },

  build: {
    outDir: 'dist',
    assetsDir: 'assets',

    // Emit a single CSS file for the firmware web bundle.
    cssCodeSplit: false,

    rollupOptions: {
      output: {
        // Rollup does not support the old `codeSplitting: false` option here.
        // This is the correct setting that forces dynamic imports into one JS bundle.
        inlineDynamicImports: true,

        // Stable firmware-friendly output names.
        entryFileNames: 'assets/app.js',
        chunkFileNames: 'assets/app.js',

        assetFileNames: (assetInfo) => {
          if (assetInfo.name && assetInfo.name.endsWith('.css')) {
            return 'assets/index.css';
          }

          return 'assets/[name][extname]';
        },
      },
    },
  },
});
