# PiLab Ladder Editor single-file Vite build

## Files changed

1. `package.json`
   - Adds this script:
     `"single": "vite build --config vite.single.config.js"`
   - Adds this dev dependency:
     `"vite-plugin-singlefile": "^2.3.0"`

2. `vite.single.config.js`
   - New Vite config used only for the single-file build.

## Install/update dependencies

From the Ladder Editor project folder:

```powershell
npm install
```

Or, if you only want to add the new plugin manually:

```powershell
npm install -D vite-plugin-singlefile
```

## Build the normal multi-file Vite app

```powershell
npm run build
```

## Build the single-file app

```powershell
npm run single
```

After it finishes, check:

```text
dist/index.html
```

That file should contain the app JavaScript and CSS inline. In most cases it will be the only file you need to copy, archive, or host.

## Test the single-file output

Because it is a browser app, the safest quick test is:

```powershell
npm run single
npm run preview
```

Then open the preview URL.

You can also try opening `dist/index.html` directly, but browser security rules can vary depending on what APIs the app uses.
