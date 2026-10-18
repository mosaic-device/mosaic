# Device protocol bindings

`events.proto` is the device-frame source of truth. Vendor `nanopb.proto` defines
inline field-size options; keep it unmodified. Owned messages use `mosaic.device.v1`.
Preserve field numbers and Nanopb limits for wire compatibility.

After `pnpm install`, run from the repository root:

```sh
pnpm --filter @mosaic/protocols lint
pnpm generate-protos
```

Generation uses pinned remote Buf plugins and requires `buf.build`. Ordinary .NET
and firmware builds use committed `Events.cs`, `Nanopb.cs`, and `events.pb.c/.h`
without running Buf. Keep `Nanopb.cs`: `Events.cs` references its options descriptor.

Check regenerated outputs, including untracked files:

```sh
git status --short -- platform/packages/protocols firmware/components/device_protos/generated platform/apps/backend/Generated/DeviceProtocol
git diff --exit-code -- platform/packages/protocols firmware/components/device_protos/generated platform/apps/backend/Generated/DeviceProtocol
```

Review and commit schema, configuration, and generated bindings together.
Do not hand-edit generated bindings.
