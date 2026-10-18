# Provisioning state and passive presentation

`state_machine.hpp` / `state_machine.cpp` are IDF-free C++23. The service executes their ordered effects through feature-local settings, Wi-Fi, portal and display adapters. The state machine does not perform NVS, HTTP, radio or LVGL operations itself. Settings use the pure-C++ `settings_validation.hpp` layout; clear submission/effect copies when no longer needed.

`presentation/presenter.cpp` maps the authoritative snapshot to bounded, owned, nonsecret `ViewState` text and flags. The screens under `ui/` draw those values and emit `UiAction::SetupTapped`; they have no confirmation timers, policy, or adapter calls. `ui/renderer.cpp` owns screen selection, semantic render caching, the latest-state mailbox, and the transient credential presentation boundary. Shared `main/ui/runtime` and `main/drivers/display` have no provisioning dependency. Application startup binds the renderer explicitly.

The selected approach and alternatives are recorded in the [provisioning ADR](../../../../docs/adr/0001-softap-provisioning.md).

## Current flow

- Without valid saved settings, or with persistent `setup_next` set, boot enters setup. Setup is AP-only: no candidate STA connection or connection test runs while the portal is open.
- The existing display acknowledgement gate remains mandatory before opening the AP. Failure to acknowledge the setup credentials fails closed; this change does not weaken or replace the gate.
- An accepted validated submission immediately persists settings. Publish `PortalStatus::saving` while this happens. A successful settings commit also clears `setup_next`; publish `saved` for six seconds, then reboot. Only the next boot tries saved Wi-Fi in STA mode.
- `storage_error` is a retryable persistence failure, not a connection result. Portal statuses are `idle`, `ready`, `saving`, `saved`, and `storage_error`; there are no `testing` or connection-`failed` statuses. HTTP admission temporarily reports `saving` until the owner publishes its state, and rejects submissions while saving/saved.
- Wrong credentials are saved too. Subsequent STA failure shows offline/reconnecting and retries with backoff; it never automatically opens setup.
- Explicit two-tap recovery on the device persists `setup_next`, then reboots into setup. Recovery retains saved settings until a replacement is committed, but that replacement is not connectivity-tested before overwriting them. Successful persistence clears the flag in the same commit.

`has_saved` describes stored settings, not connectivity or pairing. WSS validation checks syntax only; setup does not contact the server. The service owns the queue, SDK lifecycle, effect execution and status publication. HTTP validation, bounded body reads, throttling and AP-only checks remain in the portal adapter.

## Tests and validation

From `mosaic/firmware`:

```sh
python3 main/features/provisioning/tests/run.py
python3 main/support/tests/run.py
python3 main/ui/tests/run.py
```

From `mosaic`:

```sh
node --test firmware/main/features/provisioning/tests/setup_ui.test.mjs
```

Host tests are not hardware validation. The browser tests cover saving/saved/storage-error feedback, open-network submission, bounded fetch and JSON decoding, lost-response ambiguity polling, busy responses and stale initial polls.

Earlier display/QR/phone-join/captive-portal/station-IP and wrong-SSID retry results were obtained with the previous AP+STA flow and are **historical**. The withheld-display-acknowledgement test historically kept the AP closed; it did not prove physical pixel completion. The new AP-only save-and-reboot flow remains **unverified on board/phone**. Verify reboot persistence, the six-second saved window, wrong credentials with no automatic AP, two-tap recovery/flag persistence, same-commit flag clearing, storage failures, display gating, HTTP/DNS shutdown and LAN isolation. See [integration notes](INTEGRATION.md).
