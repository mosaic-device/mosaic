# @mosaic/companion

Tauri v2 shell with a Vite/React/TypeScript webview. No installer or privileged
system actions are implemented; bundling is disabled (`bundle.active: false`).
Native builds require Rust and the platform-specific Tauri prerequisites.

Run from the repository root after `pnpm install`:

```sh
pnpm --filter @mosaic/companion tauri dev
pnpm --filter @mosaic/companion tauri build
pnpm companion:check
```

Web-only commands: `pnpm --filter @mosaic/companion dev`, `lint`, `typecheck`,
or `build`. Token outputs generate automatically. Shared TypeScript and ESLint
configuration comes from workspace packages. Keep `src-tauri/Cargo.lock` committed
for repeatable native builds.
