# @mosaic/surface

Standalone Vite/React device UI prototype shell. Run from the future repository workspace with `pnpm --filter @mosaic/surface dev`, `lint`, `typecheck`, or `build`. No device behavior or adapter is invented yet; introduce a surface-local `PlatformPort` when the first screen needs it.

**Parent workspace contract:** provide `@mosaic/tokens` CSS exports `./css/tailwind-theme.css`, `./css/light.css`, `./css/dark.css`; `@mosaic/typescript-config/base.json`; and a default flat-config export at `@mosaic/eslint-config/react`. Include this app in the root workspace and generate/commit token CSS before the root `pnpm install` and builds.
