# Project rules

- This repository is the standalone Mosaic implementation; do not depend on sibling projects.
- `platform/apps/backend` is .NET, not a pnpm workspace package. `firmware/` is ESP-IDF. The companion webview is pnpm; its native shell is Cargo/Tauri.
- Generate device C/C# bindings from `@mosaic/protocols` and tokens from `@mosaic/tokens`. Do not hand-edit generated files. Protocol bindings remain committed and ordinary builds consume them without remote generation. Token JSON sources and the offline generator remain committed; token outputs are ignored and regenerated automatically by web and firmware builds. Finished font/icon assets and licenses remain committed; their sources and pinned generation tooling live in `platform/packages/assets`; regeneration is an explicit maintenance command, not part of normal builds.
- Keep the server Native-AOT-compatible. Add tests and CI with real application behavior, not empty suites.
