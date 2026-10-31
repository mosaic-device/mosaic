# Pull request checks

[PR checks](../.github/workflows/checks.yml) runs on every pull request, pushes to
`main`, and manual dispatch. Six independent jobs report failures separately.
New pushes cancel older runs for the same PR. Jobs have read-only repository
permissions and never commit formatting fixes.

## Tool versions

| Tool                                | Source / version                                                        |
| ----------------------------------- | ----------------------------------------------------------------------- |
| Runner                              | GitHub-hosted Ubuntu 24.04, Linux x64                                   |
| Node                                | `.nvmrc` (24.19.0)                                                      |
| pnpm                                | `package.json` `packageManager` (12.8.1)                                |
| Web formatters, linters, generators | `pnpm-lock.yaml`, installed with `--frozen-lockfile`                    |
| .NET SDK                            | `global.json` (10.0.401)                                                |
| Rust, rustfmt, Clippy               | `rust-toolchain.toml` (1.97.0)                                          |
| Firmware host compiler              | GCC 14 (`g++-14` from Ubuntu packages)                                  |
| Firmware formatter                  | clang-format 23.1.2, installed from PyPI                                |
| Host-test Python                    | Python 3.12 via `actions/setup-python`                                  |
| ESP-IDF                             | Official `espressif/idf:v5.5.4` image, pinned by digest in the workflow |
| IDF component manager               | 2.5.2, installed in the image's activated Python environment            |
| Protocol generation                 | Buf 1.71.0; nanopb 0.4.9 and C# 33.2 remote plugins                     |

Action revisions and the firmware image are pinned to immutable SHAs/digests.
Ubuntu system packages and Python patch releases receive updates; this is not a
fully hermetic environment. Each job's setup/build logs identify its installed
tools. Update tool declarations and this table together; verify both host tests
and a clean firmware build when changing firmware tooling.

## Checks and local commands

Run from the repository root. Install the Node/pnpm versions above, then run
`pnpm install --frozen-lockfile`. Native checks also require their own toolchains.

### Web

```sh
pnpm web:format:check
pnpm web:check
pnpm design:check
```

These cover formatting, lint, types, all web builds, asset/token tests, and the
admin production tests. Generated asset CSS and coverage JSON retain their
generator-owned formatting; ordinary CI does not regenerate fonts or icons.

### .NET

```sh
dotnet restore platform/Mosaic.slnx
dotnet format platform/Mosaic.slnx --verify-no-changes --no-restore
dotnet build platform/Mosaic.slnx --configuration Release --no-restore
dotnet test platform/Mosaic.slnx --configuration Release --no-build --no-restore
pnpm build:admin-assets
dotnet publish platform/apps/backend/Mosaic.Server.csproj --configuration Release --runtime linux-x64 -p:PublishAot=true -p:ILLinkTreatWarningsAsErrors=true -p:IlcTreatWarningsAsErrors=true --output publish/ci
python3 scripts/smoke-server.py publish/ci/Mosaic.Server
```

The publish/smoke commands above target Linux x64 and need Linux AOT prerequisites
(`clang` and `zlib1g-dev`). Use a fresh output directory when reproducing locally.
CI builds the admin assets before publishing and makes AOT/trimming warnings fatal.

`dotnet test` runs `platform/tests/Mosaic.Server.Tests`, which covers SQLite schema
migrations and persistence. The published-server smoke test covers static assets,
SPA routing, missing API/WebSocket routes, and, for the AOT binary, database
creation, restart persistence and migrations not re-running.

### Rust

Install the [Tauri Linux prerequisites](https://v2.tauri.app/start/prerequisites/)
when running on Linux. Rustup selects the committed toolchain automatically.

```sh
pnpm --filter @mosaic/companion build
cargo fmt --check --manifest-path platform/apps/companion/src-tauri/Cargo.toml
cargo clippy --locked --all-targets --manifest-path platform/apps/companion/src-tauri/Cargo.toml -- -D warnings
cargo test --locked --all-targets --manifest-path platform/apps/companion/src-tauri/Cargo.toml
```

The shell is currently a scaffold with no Rust tests. These commands check its
native compilation and linting and will discover future tests. CI does not
produce desktop installers or run a cross-platform packaging matrix.

### Generated code

```sh
pnpm --filter @mosaic/protocols lint
pnpm generate-protos
pnpm --filter @mosaic/tokens test
```

Protocol generation needs access to `buf.build`. CI removes the two generated
binding directories before generation so obsolete files cannot survive, then
rejects modified, deleted, or new uncommitted bindings. For a local reproduction
of that exact check, remove only
`firmware/components/device_protos/generated` and
`platform/apps/backend/Generated/DeviceProtocol` before generation, after ensuring
you have no work there to preserve. Regenerate and commit bindings rather than
editing them manually. Token outputs are ignored; generator tests check them
instead of a Git diff.

### Firmware host checks

Put clang-format 23.1.2 on `PATH` (for example, `pipx install clang-format==23.1.2`).
CI selects `g++-14` through `CXX`; locally use a native C++23-capable compiler.

```sh
node scripts/format-firmware.mjs --check
python3 scripts/firmware_test.py
python3 firmware/main/features/provisioning/tests/run.py
node --test firmware/main/features/provisioning/tests/setup_ui.test.mjs
python3 firmware/main/support/tests/run.py
python3 firmware/main/ui/tests/run.py
python3 firmware/main/events/tests/run.py
python3 firmware/main/drivers/tests/run.py
python3 firmware/main/features/led_feedback/tests/run.py
python3 firmware/main/features/audio_cues/tests/run.py
python3 firmware/main/features/brightness/tests/run.py
```

### Firmware build

In an activated ESP-IDF 5.5.4 environment with Node on `PATH`:

```sh
python3 scripts/firmware.py -B build/ci -D SDKCONFIG=build/ci/sdkconfig build
git diff --exit-code -- firmware/dependencies.lock
```

CI uses the official image with preinstalled IDF/toolchains rather than installing
IDF from scratch. A fresh hosted runner still pulls the image and downloads
managed components. The first version deliberately does not cache firmware build
state or create custom images; measure job timings before optimizing it. Package
and Cargo caches are used by other jobs, but correctness does not depend on a hit.

The build uses a fresh `firmware/build/ci` directory and isolated `sdkconfig`.
It consumes committed protocol bindings and assets, regenerates tokens offline,
and verifies that dependency resolution did not change the committed lock.

## Fix formatting locally

Use the same formatter versions as CI, then review and commit the results:

```sh
pnpm web:format
pnpm server:format
pnpm fw:format
cargo fmt --manifest-path platform/apps/companion/src-tauri/Cargo.toml
```

## Coverage requiring hardware

Physical-board checks remain manual: flashing, display/touch behavior, QR scanning,
real Wi-Fi/captive-portal behavior, credential persistence/reboot/rejoin, and LED output.
See the [firmware guide](../firmware/README.md) for the board and procedures.

## Verify and require the checks

Before closing #55:

1. Push a PR and confirm all six jobs pass.
2. Temporarily introduce a formatting error in an owned web source file and push.
3. Confirm **Web** fails at its formatting step.
4. Remove the error, push again, and confirm all jobs pass.

Keep the deliberate failure out of the final changes. Local validation alone does
not verify GitHub runner setup or the visible failed-check behavior.

After the jobs have reported on GitHub, a repository administrator can require
**Web**, **.NET**, **Rust**, **Generated code**, **Firmware host checks**, and
**Firmware build** in the `main` branch ruleset. The workflow itself does not
configure merge protection. Avoid renaming these checks without updating rules.
