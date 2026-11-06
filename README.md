# Mosaic

Mosaic is a connected desk display: an ESP32-P4 device, a .NET server it connects to, a web admin app for its owner, and a desktop companion.

Run every command from the repository root. Install the tools for the pieces you work on first: [setup](docs/setup.md) lists what each piece needs.

## Pieces

| Piece                                          | What it is                                                                   | Needs               | Run it                                        |
| ---------------------------------------------- | ---------------------------------------------------------------------------- | ------------------- | --------------------------------------------- |
| [Server](platform/apps/server/README.md)       | ASP.NET Core server: admin API, device WebSocket, serves the built admin app | .NET                | `pnpm server:dev`                             |
| [Admin](platform/apps/admin/README.md)         | React web app for the device owner                                           | Node, server        | `pnpm admin:dev`                              |
| [Companion](platform/apps/companion/README.md) | Tauri desktop app                                                            | Node, Rust/Tauri    | `pnpm companion:dev`                          |
| [Device preview](firmware/README.md#preview)   | The real firmware running in a window on your computer                       | Python, CMake, SDL2 | `pnpm fw:preview`                             |
| [Firmware](firmware/README.md#build-and-flash) | ESP32-P4 firmware for the Waveshare ESP32-P4-WIFI6-Touch-LCD-5               | ESP-IDF             | `pnpm fw:build`, then `pnpm fw:flash monitor` |

The admin app expects the server on port 5000, so start `pnpm server:dev` in another terminal first. On first start the server logs a setup code; enter it on the admin app's setup page. Open the URL Vite prints.

Shared packages under `platform/packages/` hold the [device protocol](platform/packages/protocols/README.md), [design tokens](platform/packages/tokens/README.md) and [fonts, icons and sounds](platform/packages/assets/README.md).

## Check your changes

Pull requests run these checks; [CI](docs/ci.md) has the full list and how to reproduce each job.

| Area      | Fix formatting          | Check                                                               |
| --------- | ----------------------- | ------------------------------------------------------------------- |
| Web       | `pnpm web:format`       | `pnpm web:format:check`, `pnpm web:check`, `pnpm design:check`      |
| Server    | `pnpm server:format`    | `pnpm server:format:check`, `pnpm server:build`, `pnpm server:test` |
| Companion | `pnpm companion:format` | `pnpm companion:check`                                              |
| Firmware  | `pnpm fw:format`        | `pnpm fw:format:check`, `pnpm fw:test`, `pnpm fw:build`             |

`pnpm fw:test` runs every firmware host test on your computer, without ESP-IDF or a board. Pass part of a path to run only some: `pnpm fw:test provisioning clock`.

## Generated files

Do not hand-edit generated files.

- **Design tokens** are generated automatically by web builds and firmware builds. To generate them by hand, run `pnpm generate-tokens`.
- **Device protocol bindings** (C for the firmware, C# for the server) are committed, so builds don't need to generate them. After changing the schema, run `pnpm generate-protos` (needs access to `buf.build`) and commit the result. See [protocols](platform/packages/protocols/README.md).
- **Fonts, icons and sounds** are committed. Regenerate them only when their sources change; see [assets](platform/packages/assets/README.md#regenerate).
