# @mosaic/surface

Vite/React device UI prototype shell. No device behavior or adapter is implemented;
add a surface-local `PlatformPort` when a screen first needs device access.

Run from the repository root after [setup](../../../docs/setup.md#node-and-pnpm):

```sh
pnpm surface:dev
pnpm --filter @mosaic/surface lint
pnpm --filter @mosaic/surface typecheck
pnpm --filter @mosaic/surface build
```

Shared tokens, TypeScript configuration, and ESLint configuration come from workspace
packages. Token outputs are ignored and generated automatically; do not commit them.
