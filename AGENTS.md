# Project rules

- This repository is the standalone Mosaic implementation; do not depend on sibling projects.
- `platform/apps/backend` is .NET, not a pnpm workspace package. `firmware/` is ESP-IDF. The companion webview is pnpm; its native shell is Cargo/Tauri.
- Generate device C/C# bindings from `@mosaic/protocols` and tokens from `@mosaic/tokens`. Do not hand-edit generated files. Protocol bindings remain committed and ordinary builds consume them without remote generation. Token JSON sources and the offline generator remain committed; token outputs are ignored and regenerated automatically by web and firmware builds. Finished font/icon assets and licenses remain committed; their sources and pinned generation tooling live in `platform/packages/assets`; regeneration is an explicit maintenance command, not part of normal builds.
- Keep the server Native-AOT-compatible. Add tests and CI with real application behavior, not empty suites.
- In firmware, keep decision logic in pure modules with no IDF or LVGL includes, covered by host tests, and keep hardware calls in thin adapters. Give every new host test runner a CI step and a line in `docs/ci.md`.
- Build firmware variants with `scripts/firmware.py -B build/<name>`, each in its own directory, and use the `MOSAIC_*` CMake options for diagnostics and traces instead of editing `sdkconfig` or raising global settings. Only one `scripts/firmware.py` command can run at a time.

# Issues

- Use the planned-work template (`.github/ISSUE_TEMPLATE/planned-work.md`): Goal, checkable Done when items, and optional Notes.
- Describe the outcome and why it matters, not how to build it. Leave libraries, file layout, algorithms and other implementation choices to whoever does the work. Constraints that really are fixed (pins, voltages, protocols, compatibility) belong in Notes.
- Treat an issue's Done when items as goals. If a better approach meets them in a different way than the wording assumes, use it and say so in the PR.

# Branches, commits and pull requests

- Branch from `main` as `<type>/<issue>-<slug>`, for example `feat/82-rotary-encoder`.
- Commit messages are a single Conventional Commits line, `type(scope): summary` or `type: summary`, with no body or trailers. Common types: `feat`, `fix`, `docs`, `build`, `ci`, `refactor`, `test`, `chore`.
- Keep each commit focused, and make sure it builds and passes its tests on its own.
- PRs are squash-merged, so the PR title becomes the commit on `main`. Write it in the same Conventional Commits format, with no issue or PR numbers; GitHub appends the PR number. Link the issue in the description with `Closes #<issue>`.
