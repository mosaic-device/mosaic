# Project rules

- This repository is the standalone Mosaic implementation; do not depend on sibling projects.
- `platform/apps/backend` is .NET, not a pnpm workspace package. `firmware/` is ESP-IDF. The companion webview is pnpm; its native shell is Cargo/Tauri.
- Generate device C/C# bindings from `@mosaic/protocols` and tokens from `@mosaic/tokens`. Do not hand-edit generated files. Ordinary builds consume committed outputs without remote generation.
- Keep the server Native-AOT-compatible. Add tests and CI with real application behavior, not empty suites.
