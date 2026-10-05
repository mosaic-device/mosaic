# Device protocol bindings

`events.proto` is the device-frame source of truth. The copied `nanopb.proto` defines the inline field-size options; there is no `events.options` file. The `mosaic.device.v1` package separates owned messages from the unmodified vendor import. Field numbers and Nanopb limits are preserved; renaming the package changes generated identifiers and descriptor names, not the field encoding.

After the root workspace installs `@bufbuild/buf` from the pinned package dependency, run from the repository root:

```sh
pnpm --filter @mosaic/protocols lint
pnpm generate-protos
```

The root `generate-protos` script should run `pnpm --filter @mosaic/protocols generate`. Generation uses pinned remote Buf plugins and requires access to `buf.build`; ordinary .NET and firmware builds use the committed `Events.cs`, `Nanopb.cs`, and `events.pb.c/.h` without running Buf. The generated C# `Nanopb.cs` is required because `Events.cs` references its imported custom-options descriptor.

To check drift after regeneration, inspect `git status --short` and `git diff --exit-code -- platform/packages/protocols firmware/components/device_protos/generated platform/apps/backend/Generated/DeviceProtocol`. Also check for new/untracked files in those paths; `git diff` alone does not detect them. Review and commit schema, config, and generated files together. Do not hand-edit generated bindings.
