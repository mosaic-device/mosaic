# Mosaic

This is the standalone Mosaic project scaffold. The apps build, but authentication, persistence, device transport, and real firmware UI are not implemented yet.

Run the commands below from **this directory** (`mosaic/`) unless noted otherwise.

## Install

You need:

- Node 24 LTS (24.19.0, see `.nvmrc`; `engines` in `package.json` accepts 24.11+) and pnpm 12.8.1. With fnm or nvm, run `fnm use` / `nvm use` in the repo root.
- .NET SDK 10.0.401 (see `global.json`)
- For the desktop shell: Rust/Cargo and the [Tauri prerequisites](https://v2.tauri.app/start/prerequisites/) for your OS
- For firmware: ESP-IDF 5.5.4 with the ESP32-P4 toolchain, activated in your shell; see [`firmware/README.md`](firmware/README.md)

```sh
pnpm install --frozen-lockfile
dotnet restore platform/Mosaic.slnx
```

The root pnpm workspace installs the web apps under `platform/`. .NET and ESP-IDF use their own toolchains; root pnpm scripts for them are shortcuts, not replacements for those toolchains.

## Run locally

Start the server in one terminal:

```sh
dotnet run --project platform/apps/backend/Mosaic.Server.csproj -- --contentRoot platform/apps/backend --urls http://localhost:5000
```

Start whichever web app you need in another terminal:

```sh
pnpm --filter @mosaic/admin dev       # Vite; proxies /api and /ws to localhost:5000
pnpm --filter @mosaic/surface dev     # browser prototype, not firmware UI
pnpm --filter @mosaic/companion tauri dev  # desktop app; requires Rust/Tauri setup
```

Run these separately; use the URL printed by Vite. The server currently serves static assets but has no API or WebSocket routes, so the apps do not have end-to-end behavior yet.

## Check and build

| Area                  | Format (changes files)                                                   | Check / build                                                                           |
| --------------------- | ------------------------------------------------------------------------ | --------------------------------------------------------------------------------------- |
| Web apps and tooling  | `pnpm web:format`                                                        | `pnpm web:format:check` and `pnpm web:check` (lint, types, build)                       |
| .NET server           | `pnpm server:format`                                                     | `pnpm server:format:check` and `pnpm server:build`                                      |
| Tauri Rust shell      | `cargo fmt --manifest-path platform/apps/companion/src-tauri/Cargo.toml` | `pnpm companion:check` (rustfmt, Clippy); `pnpm --filter @mosaic/companion tauri build` |
| Firmware-owned source | `pnpm fw:format`                                                         | `pnpm fw:format:check` and `pnpm fw:build` (activated ESP-IDF shell)                    |

`web:format` also covers root docs/config and excludes .NET, firmware, and Rust via `.prettierignore`. Firmware formatting covers owned source under `firmware/main/`, not vendor or generated files. For hardware, first run `idf.py -C firmware set-target esp32p4` in an activated ESP-IDF shell; `pnpm fw:flash` and `pnpm fw:monitor` are separate, opt-in commands. Do not flash without a connected device and reviewed configuration.

## Generate files only when inputs change

```sh
pnpm generate-tokens              # token CSS, lint restrictions, firmware header
pnpm --filter @mosaic/protocols lint  # check Protobuf schema
pnpm generate-protos              # firmware C and server C# bindings; needs buf.build
```

Generated outputs are committed, so normal builds do **not** need regeneration or access to `buf.build`. Do not hand-edit them. Review source and generated changes together; after regenerating, check both `git diff` and `git status --short` (new files do not appear in `git diff`).

## Optional: test server publishing

```sh
pnpm build:admin-assets
dotnet publish platform/apps/backend/Mosaic.Server.csproj -c Release -o publish/local
python3 scripts/smoke-server.py publish/local/Mosaic.Server
```

Build admin assets **before** publishing: the server serves the copied SPA without Node at runtime. For a clean republish, remove stale files from `publish/local/` first. Linux x64 Native AOT publishing remains **unverified**; a local macOS publish does not prove that target.
