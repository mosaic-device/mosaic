# @mosaic/admin

Static Vite/React admin shell. Run from the future repository workspace with `pnpm --filter @mosaic/admin dev`, `lint`, `typecheck`, or `build`. `dist/` is intended for the backend's `wwwroot/` copy step, owned by the parent scaffold. No API or auth behavior is implemented.

Local Vite proxy: `/api` and WebSocket `/ws` target `http://localhost:5000`. Set the backend's local HTTP port to 5000 or update this app's proxy to match it.

**Parent workspace contract:** provide `@mosaic/tokens` CSS exports `./css/tailwind-theme.css`, `./css/light.css`, and `./css/dark.css` backed by generated files; `@mosaic/typescript-config/base.json`; and a default flat-config export at `@mosaic/eslint-config/react`. Include this app in `pnpm-workspace.yaml`, generate/commit token CSS, then run one root `pnpm install` to create the single lockfile. Do not install from this directory.
