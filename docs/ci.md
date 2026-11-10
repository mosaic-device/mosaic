# Pull request checks

[PR checks](../.github/workflows/checks.yml) runs on every pull request, on pushes to `main`, and on manual dispatch. Its seven jobs run independently and report failures separately. A new push cancels older runs for the same PR. Jobs have read-only repository permissions and never commit formatting fixes; run the [formatters](../README.md#check-your-changes) yourself.

Run commands from the repository root. [Setup](setup.md) covers installing each toolchain.

## Jobs

| Job                      | Checks                                                                                                    | Run it locally                                                                  |
| ------------------------ | --------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------- |
| **Web**                  | Prettier formatting; lint, types and builds of every web app; asset, token and admin tests                | `pnpm web:format:check`, `pnpm web:check`, `pnpm design:check`                  |
| **.NET**                 | C# formatting, Release build, server tests, Linux Native AOT publish and a smoke test of the binary       | `pnpm server:format:check`, `pnpm server:test`; [AOT publish](#net-aot-publish) |
| **Rust**                 | Companion rustfmt, Clippy with warnings as errors, Rust tests                                             | [Rust commands](#rust)                                                          |
| **Generated code**       | Protobuf lint, committed bindings match the schema, token generator tests                                 | [Generated code commands](#generated-code)                                      |
| **Firmware host checks** | Firmware formatting and every firmware host test                                                          | `pnpm fw:format:check`, `pnpm fw:test`                                          |
| **Firmware preview**     | The preview compiles on Linux, macOS and Windows                                                          | `pnpm fw:preview --build-only`                                                  |
| **Firmware build**       | A clean ESP-IDF build, the committed dependency lock unchanged, no debug-trigger code in the normal image | [Firmware build commands](#firmware-build)                                      |

`pnpm fw:test` finds every tracked `tests/run.py` and `tests/*.test.mjs` under `firmware/`, plus `scripts/firmware_test.py`, so a new runner needs no CI or documentation change. Each runner's output starts with `== <path>`.

The golden protocol frames in `platform/packages/protocols/testdata/frames.txt` are decoded by both the firmware host tests (nanopb bindings) and the .NET tests (C# bindings, `DeviceProtocolGoldenFrameTests`), so both sides must agree on every byte.

## Where CI differs from the pnpm scripts

These jobs run steps the everyday scripts don't. Use these commands to reproduce a failure exactly.

### .NET AOT publish

CI builds and tests in Release, then publishes the server as a native Linux x64 binary with AOT and trimming warnings as errors, and smoke-tests that binary:

```sh
dotnet restore platform/Mosaic.slnx
dotnet format platform/Mosaic.slnx --verify-no-changes --no-restore
dotnet build platform/Mosaic.slnx --configuration Release --no-restore
dotnet test --solution platform/Mosaic.slnx --configuration Release --no-build --no-restore
pnpm build:admin-assets
dotnet publish platform/apps/server/Mosaic.Server.csproj --configuration Release --runtime linux-x64 -p:PublishAot=true -p:ILLinkTreatWarningsAsErrors=true -p:IlcTreatWarningsAsErrors=true --output publish/ci
python3 scripts/smoke-server.py publish/ci/Mosaic.Server
```

The publish and smoke steps work as written only on Linux x64, with `clang` and `zlib1g-dev` installed; elsewhere, use the [verify container](../platform/apps/server/README.md#automated-publish-and-startup-verification). Publish into a fresh output directory. The smoke test checks static assets, SPA routing, missing API and WebSocket routes, first-run setup, login and sessions, an unpaired device handshake, claiming its code through the API, a paired handshake, that the pairing and other data survive a restart without migrations running twice, and listing and removing the device, whose reconnect is then rejected, also after another restart.

### Rust

```sh
pnpm --filter @mosaic/companion build
cargo fmt --check --manifest-path platform/apps/companion/src-tauri/Cargo.toml
cargo clippy --locked --all-targets --manifest-path platform/apps/companion/src-tauri/Cargo.toml -- -D warnings
cargo test --locked --all-targets --manifest-path platform/apps/companion/src-tauri/Cargo.toml
```

Tauri needs the webview built before Rust compiles, hence the first command. `pnpm companion:check` runs the format and Clippy steps without `--locked` or `--all-targets`. There are no Rust tests yet, so `cargo test` only proves the tests compile. CI does not build desktop installers.

### Generated code

```sh
pnpm --filter @mosaic/protocols lint
pnpm generate-protos
git status --short -- platform/packages/protocols firmware/components/device_protos/generated platform/apps/server/Generated/DeviceProtocol
pnpm --filter @mosaic/tokens test
```

Protocol generation needs access to `buf.build`. CI deletes `firmware/components/device_protos/generated` and `platform/apps/server/Generated/DeviceProtocol` before generating, so obsolete files show up too, then fails on any modified, deleted or untracked binding. To reproduce that, delete the same two directories first, once you're sure you have no work there. Fix a failure by regenerating and committing the bindings. Token outputs are ignored by Git; the generator tests check them instead.

### Firmware build

In an activated ESP-IDF environment with Node on `PATH`:

```sh
python3 scripts/firmware.py -B build/ci -D SDKCONFIG=build/ci/sdkconfig build
git diff --exit-code -- firmware/dependencies.lock
```

The build uses its own directory and `sdkconfig`, so your local configuration can't affect it. It uses the committed protocol bindings and assets and regenerates tokens offline. A change to `dependencies.lock` means dependency resolution moved a component; commit the updated lock. CI then checks that the image contains no debug-trigger code.

## Tool versions

| Tool                                | Source / version                                                        |
| ----------------------------------- | ----------------------------------------------------------------------- |
| Runner                              | GitHub-hosted Ubuntu 24.04, Linux x64                                   |
| Node                                | `.nvmrc`                                                                |
| pnpm                                | `package.json` `packageManager`                                         |
| Web formatters, linters, generators | `pnpm-lock.yaml`, installed with `--frozen-lockfile`                    |
| .NET SDK                            | `global.json`                                                           |
| Rust, rustfmt, Clippy               | `rust-toolchain.toml`                                                   |
| Firmware host compiler              | GCC 14 (`g++-14` from Ubuntu packages, selected through `CXX`)          |
| Firmware formatter                  | clang-format 23.1.2, installed from PyPI                                |
| Host-test Python                    | Python 3.12 via `actions/setup-python`                                  |
| ESP-IDF                             | Official `espressif/idf:v5.5.4` image, pinned by digest in the workflow |
| IDF component manager               | 2.5.2, installed in the image's activated Python environment            |
| Protocol generation                 | Buf 1.71.0; nanopb 0.4.9 and C# 33.2 remote plugins                     |

Action revisions and the firmware image are pinned to immutable SHAs and digests. Ubuntu packages and Python patch releases still update, so the environment is not fully hermetic; each job's setup logs show its installed tools. Update a tool's declaration, this table and [setup](setup.md) together, and when changing firmware tooling verify both the host tests and a clean firmware build.

The firmware build uses the official image rather than installing ESP-IDF, but a fresh runner still pulls the image and downloads managed components. Firmware build state is deliberately not cached; measure job timings before optimizing. Other jobs cache packages and Cargo output, but none depend on a cache hit.

## Not covered by CI

Anything needing the physical board stays manual: flashing, display and touch, QR scanning, real Wi-Fi and the captive portal, saving credentials and rejoining after a reboot, the LEDs, speaker and light sensor. [M2 board verification](hardware/m2-board-verification.md) has the procedure and the latest results.

## Required checks

The `main` branch ruleset decides which jobs must pass; the workflow does not configure merge protection. Renaming a job breaks any rule that requires it, so update the ruleset in the same change.
