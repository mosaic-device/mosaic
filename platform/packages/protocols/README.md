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

## Golden frames

`testdata/frames.txt` holds hex-encoded envelopes that both sides must decode
identically: the C# bindings in `DeviceProtocolGoldenFrameTests` (.NET job) and
the nanopb bindings in `firmware/components/device_protos/tests/run.py`
(Firmware host checks job). When a schema change adds or changes a frame, add
the frame here and its expected fields to both tests in the same change; each
test fails if the file and its expectations cover different frames. Message
ordering and field rules are on the wiki's Device Connection Protocol page.
