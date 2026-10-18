# Provisioning

Phone-based Wi-Fi setup and on-device recovery. See the [setup guide](../../../README.md#phone-setup) and [design decision](../../../../docs/adr/0001-softap-provisioning.md).

## Layout

- `state_machine.*`: IDF-free C++23 policy and ordered effects.
- `service`: owner task, event queue, adapter lifecycle and effect execution.
- `presentation/`: bounded, owned, nonsecret view state.
- `ui/`: passive screens and renderer; callbacks emit typed actions.
- Settings, Wi-Fi and portal adapters handle persistence, radio and HTTP operations.

Shared `main/ui/runtime` and `main/drivers/display` have no provisioning dependency. Application startup binds the feature renderer explicitly.

## Design constraints

- The service is the single owner of state and effects; screens only render state and emit actions. Routine views may coalesce; actions and credential presentation must not.
- Keep credentials out of routine view snapshots and logs. Wipe sensitive copies after use, cancellation or failure.
- Keep AP/HTTP/DNS closed until the display waiter commits matching credential-render acknowledgement. Queue acceptance or provisional acknowledgement is not completion.
- Save settings and clear the recovery flag in one storage commit. Recovery sets the flag without erasing saved settings; storage read errors must not erase NVS.
- Restrict portal requests to the setup AP destination and peer subnet. Validate POST Host and Origin when present; retain bounded reads, input validation and throttling.

Test commands are in the [firmware guide](../../../README.md#tests-and-tooling).
