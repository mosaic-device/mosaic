# @mosaic/companion

Tauri v2 shell with a Vite/React/TypeScript webview. No installer or privileged
system actions are implemented; bundling is disabled (`bundle.active: false`).
Native builds need Rust and the Tauri prerequisites ([setup](../../../docs/setup.md#rust-and-tauri)).

Run from the repository root:

```sh
pnpm companion:dev                          # desktop app with live reload
pnpm --filter @mosaic/companion tauri build
pnpm companion:format                       # rustfmt
pnpm companion:check                        # rustfmt check and Clippy
```

Web-only commands: `pnpm --filter @mosaic/companion dev`, `lint`, `typecheck`,
or `build`. Token outputs generate automatically. Shared TypeScript and ESLint
configuration comes from workspace packages. Keep `src-tauri/Cargo.lock` committed
for repeatable native builds.
