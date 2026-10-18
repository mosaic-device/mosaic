# Provisioning integration boundary

## Ownership and startup

`mosaic::features::provisioning::start()` starts one owner task and waits for its initial result. Call it from an application task after NVS initialization, `esp_netif_init()`, the default event loop, the hosted P4/C6 transport prerequisites and successful `mosaic::ui::start(renderer_callbacks())`. Do not call concurrently or from an event callback. The owner initializes Wi-Fi, owns AP/STA netifs and event handlers, and executes state-machine effects. The UI callback enqueues typed taps without blocking; there is no public shutdown API. On failure, `start()` publishes the unavailable view with the service's current setup session, including failures before state-machine initialization. `app_main` logs the error without replacing that publication with a session-zero fallback.

The firmware pins `esp_wifi_remote` 0.14.5 / `esp_hosted` 1.4.7 with ESP-IDF 5.5.4. Use the configured C6 hosted transport, not a guessed P4 radio implementation. `main/CMakeLists.txt` embeds `assets/setup.html` and `assets/setup.js` without a filesystem image.

## Passive UI ownership and ordering

- `state_machine` is pure C++23 and owns recovery confirmation, monotonic deadlines, visible connection reasons, persistent recovery failure, and admission feedback. `service` is the sole feature owner and executes its ordered effects. It publishes a complete presenter-derived view before effects and again through each completion transition. Ordinary rendering has no effects or historical `show_*` commands.
- `presentation/presenter` owns outcome-to-copy mapping. `ViewState` contains owned bounded text, flags, screen kind, and a nonsecret setup session ID; no borrowed strings, credentials, widget pointers, or IDF/LVGL dependencies. Equality compares text values/fields, not padding. Unchanged Tick snapshots neither publish a new revision nor repaint.
- `ui/renderer` owns a single latest-state slot protected by a short critical section. Publication copies the value; the existing 10 ms LVGL timer observes it. The worker copies value/revision under the lock, renders outside it, then consumes only that revision. Concurrent newer publication remains pending. Renderer creation resets the consumption cursor so routine state can be reconstructed without command replay.
- Screen callbacks emit only `UiAction::SetupTapped`, with no optimistic pending/busy labels. Accepted taps stay ordered in the eight-entry owner queue. The bridge assigns monotonic admission IDs and records rejection in a separate bounded observation latch even if the queue is full. The owner drains it before queued actions and after Tick. IDs prevent old rejection from replacing newer accepted recovery; old accepted actions are not discarded merely because a newer rejection arrived first. An authoritative action-context generation invalidates taps/rejections across hidden/pending/retry contexts. Wi-Fi updates during valid confirmation preserve that context.
- A first eligible tap confirms for ten seconds of owner monotonic time. A second tap at the exact deadline is accepted, even if Tick ran first; Tick expires only strictly after the deadline. A late second tap starts a new window. Offline presentation stays recoverable during reconnect; initial saved-STA startup is not recoverable. Successful Wi-Fi association/IP clears recovery failure and obsolete admission feedback. Persistence failure remains retryable through immediate reconnect, until an accepted retry or a successful connection changes the context.
- Runtime callbacks and the static renderer/mailbox live until process reset; there is no runtime teardown/rebind API or dangling per-task renderer context. Service cleanup first detaches the action callback under the display/LVGL lock, then unregisters SDK callbacks and frees queues/netifs. Publication rejects before runtime readiness and after reboot shutdown begins. Renderer recreation tests cover routine state only: secrets are intentionally not reconstructible from routine snapshots.

## Credential and display boundary

`render_setup_credentials()` validates/encodes credentials in feature presentation, then sends one bounded opaque payload through `main/ui/runtime`. The runtime retains one sensitive slot and one synchronous application-task waiter, with a two-second token/deadline gate. It wipes the slot on worker consumption, completion, timeout/cancellation, and unavailable/shutdown outcomes; the feature erases its local credential and QR copies. Request/token zero is never used; deadline subtraction handles millisecond wrap. Consecutive attempts cannot use an old flush/refresh, and tokens skip zero on wrap. A rejected credential-render callback cancels its token and cannot be acknowledged.

The LVGL worker applies current credentials first and blocks routine rendering until the waiter commits that token's matching flush, refresh, and successful backlight acknowledgement before its deadline, or explicitly cancels it. An acknowledgement alone is provisional: `RenderAck::completed()` and `pending()` keep the displayed token/barrier alive while the waiter is descheduled, including past its deadline. If the waiter resumes too late, it cancels and the next worker tick clears credential widgets/QR. The worker rechecks completion after observing a released active token so concurrent commit is not mistaken for cancellation. Missing or expired acknowledgement keeps AP/HTTP/DNS closed. Queue/slot acceptance is not completion, and LVGL acknowledgement is not proof of physical pixels. After committed completion, routine state must match the active setup session; stale startup/session snapshots cannot hide setup. Authoritative startup failure retains that session ID, so it deliberately retires the failed setup and clears credentials even if AP/HTTP startup failed after rendering succeeded. Current-session saved/reboot state also deliberately leaves setup and clears widgets. Ordinary saving/storage-error state reuses the active credential presentation, with no QR recreation and no unnecessary second handshake. There is no required credential re-render on storage failure now; `SetupDisplayFailed` still stops portal/Wi-Fi if an active required presentation fails.

`main/drivers/display` alone owns BSP configuration, startup settling/redraw, backlight readiness, and single-run shutdown blank/GPIO hold. Runtime asks the driver for refresh readiness before acknowledging. Diagnostic builds retain wrapper linkage by calling the diagnostic logger on credential-render timeout; normal builds do not link those wrappers. Shared driver/runtime contain no feature headers.

### Phase-to-view policy

| Authoritative state | Visible state |
| --- | --- |
| Unstarted/startup context | Mosaic / starting setup; no recovery button |
| Initial saved STA start/wait | Connecting to saved Wi-Fi; no recovery button |
| Opening portal / ready / saving | Existing setup credentials/QR; no recovery button |
| Storage error | Same setup session/QR plus retryable storage message |
| Saved visible | Settings saved / restarting; no connectivity claim |
| Online | Wi-Fi connected / waiting for pairing; recovery available |
| Offline or reconnect after timeout/disconnect | Offline / retrying or unavailable reason; recovery available |
| Recovery persistence failure, including immediate reconnect | Durable setup-unavailable message; recovery enabled |
| Confirming | Current status plus supplied second-tap prompt |
| Requesting recovery | Opening setup; disabled recovery control |
| Rebooting | Restarting; no recovery control |
| Startup failed/unavailable | Explicit unavailable view; no active recovery callback |

All current dispatch branches emit at most one consequential effect. The existing three-slot result bound is retained; regression assertions verify the maximum of one. Tick's saved-reboot branch cannot also enter a saved-STA/reconnect branch after it changes phase to rebooting.

## AP-only save-and-reboot contract

1. Boot with missing/invalid/unsupported settings or persistent `mosaic_net/setup_next` enters setup. Real storage read errors stop startup without erasing NVS.
2. Render the setup SSID, temporary password, Wi-Fi QR and direct HTTP URL. **Retain the existing display acknowledgement gate:** the AP must not open before credentials are acknowledged by the display path. Missing acknowledgement fails closed. This is not proof of physical pixel completion.
3. Setup runs AP-only, with no STA credential test. Validate the submitted SSID/password/WSS syntax, then immediately persist settings. The owner, not the HTTP handler, performs persistence and reboot.
4. Persist `mosaic_net/settings` and clear `setup_next` in the same commit. On success show `saved` for six seconds, then reboot. The subsequent boot attempts saved STA. Storage failure reports `storage_error` and permits retry without claiming connectivity.
5. Wrong credentials still replace saved settings. After reboot show offline/reconnecting and retry STA with backoff, never automatically expose an AP.
6. Two explicit taps on **Enter setup mode** within 10 seconds request recovery: persist `setup_next`, then reboot into setup. Recovery itself does not erase the saved settings. A successful replacement save clears the flag in the same NVS commit as settings, without testing the replacement connection first.

Portal statuses are `idle`, `ready`, `saving`, `saved`, `storage_error`; no `testing` or connection-`failed` statuses. Transport admission reports `saving` while a submission awaits owner consumption. Reject saves while saving/saved. A `202` response means accepted, not persisted or connected.

The browser retains bounded POST/status fetches and JSON decoding, generation checks and bounded ambiguity polling. An interrupted POST may already have been accepted: poll status rather than immediately resubmit. If the result cannot be confirmed before timeout, direct the user to the display. On reopening the page, resume saving progress. Without JavaScript, the response offers a readable `/status` link. `saved` is storage success only, not a verified network/server or pairing result.

## Security and adapter boundaries

Wi-Fi driver storage is RAM-only. Settings are an application-owned NVS blob. Setup is plain HTTP on a temporary WPA2 AP; knowing its displayed key is not proof of physical presence. Requests require the AP destination and peer subnet; POST additionally requires the exact displayed Host and matching Origin if present. IPv4-mapped IPv6 socket addresses are supported; native IPv6/non-AP requests are rejected. Preserve body bounds, full receive handling, input validation, throttling, DNS packet checks and secret erasure. Never log or reflect credentials.

An empty submitted password selects an open home network, not an open setup AP. Public Wi-Fi requiring browser sign-in is unsupported. The WSS URL is syntax-checked only: no reachability, TLS or pairing test occurs. Flash/NVS encryption and intentional factory reset remain production-policy work. Radio/HTTP/DNS teardown and partial failure handling remain service/adapter responsibilities.

## Validation status

**The new AP-only save-and-reboot flow is not yet board/phone verified.** Earlier results are historical: the owner observed rendered credentials, QR phone join, automatic captive opening on one phone, station IP and wrong-SSID retry in the old AP+STA flow. A temporary withheld-setup-acknowledgement image kept the AP closed on two attempts; saved STA later reconnected. This validates that historical timeout path, not normal recovery, physical display completion or the new reboot flow.

Run host tests with `python3 main/features/provisioning/tests/run.py`, `python3 main/ui/tests/run.py`, and `python3 main/support/tests/run.py` from `firmware/`, and browser tests with `node --test firmware/main/features/provisioning/tests/setup_ui.test.mjs` from `mosaic/`. These do not prove SDK event order or hardware behavior. Before shipping, test power-cycle persistence, six-second saved/reboot behavior, wrong credentials without automatic AP, recovery flag persistence and clearing, storage failure, open Wi-Fi, display acknowledgement failure, repeated recovery, DNS/HTTP shutdown, normal-LAN isolation and additional iOS/Android captive behavior. Do not reuse previous build or smoke-test results as validation of this changed flow.

### Refactor validation — 2026-10-12

Provisioning host suites (seven), UI host suites (five plus ownership checks), secure-erasure tests at `-O0`/`-O2`, all 15 browser tests, and `pnpm fw:format:check` passed. Host code uses C++23, disabled exceptions/RTTI, and warnings as errors. UI seams compile the production runtime, renderer, display driver, acknowledgement, and latest-state code; they do not imitate widget layout or establish physical panel behavior.

ESP-IDF 5.5.4 normal `idf.py build` and fresh isolated diagnostic `idf.py -B build/passive-ui-diagnostics -D SDKCONFIG=build/passive-ui-diagnostics/sdkconfig -D MOSAIC_DISPLAY_DIAGNOSTICS=ON build` passed. Existing `build/display-diagnostics/sdkconfig` disables QR support and that historical-config attempt failed; it was not overwritten to make validation pass. Use a fresh build/config path when defaults have changed. A diagnostic archive-link failure was fixed by retaining the timeout logger reference, which pulls in the wrapper object. No flashing or commits were performed. Sandbox host runs emitted macOS `xcrun` cache-write messages but the compilers and test executables completed successfully.

### Independent-review regression rerun — 2026-10-12

Reproduced and fixed (1) startup AP/portal failure leaving acknowledged setup credentials visible due to a lost session ID and (2) provisional acknowledgement releasing the displayed token before the waiter committed completion. Renderer tests now reject stale session-zero/wrong-session error views but clear credentials for authoritative current-session failure. Runtime tests pause the waiter through acknowledgement just before deadline and worker ticks on both sides of expiry; the waiter times out, the next worker clears credentials, routine work stays blocked until cancellation, and the next request succeeds. Both new regressions failed before the corresponding fix and pass afterward.

Reran all seven provisioning suites, five UI suites plus boundary checks, secure-erasure at `-O0`/`-O2`, 15 browser tests, firmware format check, and Git whitespace check: all passed. Normal and isolated `build/passive-ui-diagnostics` ESP-IDF 5.5.4 builds passed again with 240-second bounds. Sandbox `xcrun` cache-write warnings are nonfatal. No board/phone validation, flash, or commit was performed.
