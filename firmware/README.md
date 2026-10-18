# ESP32-P4 firmware

Board: Waveshare ESP32-P4-WIFI6-Touch-LCD-5; ESP-IDF **5.5.4**, target `esp32p4`. `app_main` starts a minimal LVGL setup/status screen and ESP32-C6-hosted provisioning service. Setup is now **AP-only save-and-reboot**, not an AP+STA connection test. The new flow remains **unverified on board/phone**. Earlier successful display, QR/phone join, captive-portal, station-IP and wrong-SSID retry results are historical observations of the previous flow, not validation of this one. Audio, the production dashboard, pairing and backend transport are not implemented. See [integration notes](main/features/provisioning/INTEGRATION.md), the [provisioning decision](../docs/adr/0001-softap-provisioning.md).

## Passive firmware UI

The provisioning `StateMachine` owns recovery confirmation/deadlines and durable outcomes; `service` owns the task, ordered inputs, adapters, and publication. A pure presenter produces complete bounded, nonsecret view state. Feature `ui/renderer` selects passive screens and retains the latest routine snapshot; screens only draw values and emit typed taps. Shared `ui/runtime` handles LVGL dispatch and token acknowledgement; `drivers/display` owns BSP/backlight/reboot lifecycle. Neither shared module includes feature headers. See [integration contracts](main/features/provisioning/INTEGRATION.md).

Routine state is coalesced, but actions and credential presentation are not. Credentials use a separate wiped slot and matching token/flush/refresh/backlight acknowledgement committed by the waiter before AP startup. The displayed token/barrier remains until commit or cancellation, not merely provisional acknowledgement. Startup failure carries the current setup session so unavailable AP credentials/QR are cleared without admitting stale startup views. Storage failure keeps the existing setup QR and adds retry messaging without rebuilding credentials. Recovery queue rejection is owner-visible through a separate ordered/context-correlated latch; screens never optimistically disable recovery.

**Refactor validation (2026-10-12):** seven provisioning host suites, five UI host suites plus ownership checks, secure-erasure tests at `-O0`/`-O2`, 15 browser tests, and firmware format check passed. ESP-IDF 5.5.4 normal and fresh isolated diagnostic builds passed. No flash/board acceptance was performed. Old diagnostic/softap configs can have QR support disabled despite current defaults; use a fresh config path rather than trusting old build artifacts. The fresh diagnostic image used `build/passive-ui-diagnostics` with `MOSAIC_DISPLAY_DIAGNOSTICS=ON`. Existing validation notes and remaining board checks are in [integration notes](main/features/provisioning/INTEGRATION.md). Independent-review follow-up regressions also passed: current-session startup failure clears acknowledged credentials, and provisional acknowledgement cannot release the render barrier before waiter commit/cancellation. All host/browser/format checks and both builds were rerun successfully after these fixes.

## Build

Install and activate ESP-IDF 5.5.4 with the ESP32-P4 toolchain and put Node.js on `PATH`. From this directory:

```sh
idf.py --version
idf.py build
# To build against current defaults without overwriting an existing local sdkconfig:
idf.py -B build/softap -D SDKCONFIG=build/softap/sdkconfig build
```

`platform/packages/protocols` owns `components/device_protos/generated/events.pb.c` and `events.pb.h`; those committed outputs must be present. `platform/packages/tokens` retains token JSON and its dependency-free offline generator. CMake runs it during configure to recreate the ignored `main/ui/generated/design_tokens.h` (and web token outputs), including for isolated build directories. Input/generator changes and a missing header trigger reconfiguration. Node.js is required; pnpm and asset generation tools are not. The UI host test runner also generates tokens before compiling. Do not hand-edit either generated directory. The `device_protos` component compiles `events.pb.c` against the included Nanopb runtime. ESP-IDF 5.5.4 builds both the normal firmware (`build/device_firmware.bin`) and isolated builds such as `build/softap/device_firmware.bin`. The normal firmware has been flashed and exercised on the target board; isolated and diagnostic images should not be confused with it. Existing ignored `sdkconfig` values take precedence over changed `sdkconfig.defaults`, so use the isolated build above for a clean config or review the local config explicitly.

The local BSP/display manifests pull in display/touch/LVGL dependencies. `components/esp_driver_ppa` overrides the IDF 5.5.4 PPA driver with Espressif's temporary rotation-stall workaround; see its [provenance and test scope](components/esp_driver_ppa/README.md). Reconfigure after adding this component (`idf.py reconfigure`) and confirm the override is selected before flashing. The owner confirmed the password and URL rendered and the stalled refresh cleared on the target board; retain the fail-closed setup display check and re-test when changing IDF, display geometry, or PPA usage.

`main/idf_component.yml` pins ESP-Hosted 1.4.7 and Wi-Fi Remote 0.14.5; it does **not** import the slice's code, credentials, or build artifacts. `sdkconfig.defaults` enables PSRAM, three panel buffers, LVGL's QR widget, and fonts referenced by the generated tokens. The board's 32 MiB flash uses a custom 8 MiB factory partition; there is no filesystem image. NVS encryption, reboot persistence, recovery, captive-popup behavior on other phones, save-and-reboot behavior, and normal-LAN isolation still require validation before production use.

### Setup display gate: historical board test result (gate retained)

A temporary diagnostic image withheld the setup request's refresh acknowledgement on the five-inch board. On two on-device setup attempts, serial logs showed the acknowledgement was withheld, the previous `show_setup()` timed out (`ESP_ERR_TIMEOUT`), and portal startup failed; the owner confirmed that no new `Mosaic-` AP appeared in the phone's Wi-Fi list. The device subsequently reconnected to its **previously saved Wi-Fi as a station**, which is expected and does not mean the setup AP opened. **Result: the missing-acknowledgement timeout path kept the setup AP closed on this board.** The temporary fault-injection switch is no longer in source. This did not simulate a physically stalled panel or establish that LVGL's flush/refresh signals always correspond to visible pixels; that remains a separate validation limit.

## Development component gallery (phase 3B)

`MOSAIC_UI_GALLERY` is a **CMake option, OFF by default**, not a production setting.
The ON branch starts only the existing UI runtime with gallery callbacks. It does
not initialize NVS/netif/event loop, start provisioning/Wi-Fi/AP, or read saved
credentials. Credential callbacks reject requests. It shares the existing BSP,
LVGL worker lock, refresh/backlight lifecycle and timer; no second UI task exists.
Normal initialization order and provisioning screens are unchanged. Gallery/helper
sources are gated out of normal builds; phase 4 screen migration remains gated on
board/phone acceptance.

From `firmware/`:

```sh
. /Users/blakealmanza/esp/esp-idf/export.sh
idf.py -B build/design-system-gallery -D SDKCONFIG=build/design-system-gallery/sdkconfig -D MOSAIC_UI_GALLERY=ON build
# Explicit hardware operation, only when ready (not run during implementation):
idf.py -B build/design-system-gallery -D SDKCONFIG=build/design-system-gallery/sdkconfig -D MOSAIC_UI_GALLERY=ON flash monitor
# Separate normal image; keep gallery configuration out of the production build:
idf.py -B build/design-system-normal -D SDKCONFIG=build/design-system-normal/sdkconfig -D MOSAIC_UI_GALLERY=OFF build
```

The device is a 5-inch desk display viewed below a monitor, not a handheld phone.
Firmware uses a larger scale independent of web: caption 28px, labels/buttons 32px,
body 36px, headings 48px, and titles 60px. Controls have an 80px minimum height;
settings icons are 40px and wordmark ink heights are 40/60px. Card/screen padding
and section gaps are 24px. These are desk-distance starting values requiring
physical review; larger text intentionally shows less content per viewport.

Swipe vertically through typography (Manrope 400/500/600), special characters and
clock digits, both wordmark sizes, settings icon, wrapping text, live controls and
explicitly labeled fixed state samples. Tap the checkbox/list row to request an
owner-applied value, buttons to increment the update counter, and **Recreate gallery
controls** to exercise destruction/recreation on the next runtime tick (outside
the event dispatch). Fixed pressed/focus/selected/disabled/busy examples have no
click handling. No encoder traversal or keyboard/input editing is implemented.

### Reusing the small visual helpers

- `main/ui/styles/styles.*`: token-backed, dark-only styles initialized once under
  the UI lock and retained for process lifetime. Do not reset these while referenced.
- `main/ui/components/controls.*`: text roles, cards, primary/secondary buttons,
  settings icon button, selectable rows and passive checkbox. Parent screens own
  objects; deleting them releases callback bindings. Callback contexts must outlive
  their controls. No generic widget/navigation framework is introduced.
- Build a screen inside runtime renderer callbacks; create controls, supply typed
  `Action` callbacks, and call `render(control, State)` from authoritative feature
  state. Clicks report `activate`, `select` or `toggle`; they do not auto-toggle or
  invent completion. Busy suppresses actions independently of disabled and uses
  `LV_STATE_USER_1`; owners supply honest pending labels. Selection uses CHECKED,
  focus uses FOCUSED, press uses PRESSED. Focus and selection are independent.
- Text labels wrap to parent width; button labels inherit their control foreground
  (primary normal/pressed is exact token white). Cards grow with content; screens
  own scrolling. Controls use the firmware-specific 80-pixel minimum hit height, not a claim
  about physical target size. Checkbox indicator uses LVGL's built-in check glyph;
  interface text remains Manrope. No switch is added without a screen use case.
- Assets remain generated and unchanged: use `generated/assets/assets.h` symbols;
  masks are recolored explicitly. Unsupported Unicode/emoji and physical tint,
  sharpness and touch geometry still require asset-policy/device review.

Validation: ESP-IDF 5.5.4 isolated normal/gallery builds, existing UI/provisioning
host suites and ownership checks pass. The component action contract test is
included in `python3 main/ui/tests/run.py`.

Measured binaries after the desk-distance scale update: normal **2,911,184 bytes**,
gallery **2,692,864 bytes** (**−218,320 bytes** versus normal; gallery omits reachable
production services).
Compile databases confirm gallery/helper sources are excluded from normal builds.
Firmware formatting and UI/provisioning host checks pass. No provisioning screen
migration has been performed.

No flash was performed. Board acceptance remains outstanding: actual orientation,
scroll/touch behavior, checkbox wrapping, focus ring clipping, exact text/icon tint,
glyph coverage, repeated recreation/leaks, heap/PSRAM and interaction latency.
Do not infer those from compilation or host tests. Image-size comparison is between
two different entry points, not the incremental cost of migrating production screens.

## Phone setup

1. On an unconfigured device, scan the **Wi-Fi join QR** on the right of the display. The setup page should open after joining; if the phone does not open a captive window, enter the displayed **HTTP**, not HTTPS, URL in a browser (normally `http://192.168.4.1/`). The QR only joins the AP; it cannot force the phone to open the portal. Manual fallback on the left shows the `Mosaic-XXXXXX` AP name, 12-character password, and URL. The six hex characters come from the last three bytes of the P4's factory base MAC; they are not a secret. The QR contains the same temporary AP password visible in text, and is regenerated when a new setup session creates a new key. The owner confirmed scanning and automatic portal opening on one phone after a short network-switch delay; iOS/Android coverage across devices remains unverified.
2. Enter the home Wi-Fi SSID and password (empty only for an open network), plus a `wss://` server URL. Tap **Save and reboot**. Input is validated and immediately persisted; setup stays AP-only and does not test station connectivity. Passwords must be empty or 8–63 characters. The WSS URL is syntax-checked, never contacted during setup. Public networks requiring browser sign-in are unsupported.
3. Successful persistence clears `mosaic_net/setup_next` in the same commit as `mosaic_net/settings`. The portal reports `saved` for about six seconds, then the device reboots. Only the subsequent boot tries saved Wi-Fi in STA mode. `saved` means storage success, not connectivity or pairing. A storage failure leaves retry available and must not claim a connection. The browser polls with bounded requests; a lost response is an unconfirmed result, so check the device display before retrying.
4. Wrong credentials are still saved. After reboot, the device shows offline/reconnecting and retries saved STA with backoff; it does **not** automatically open an AP. On the connected/offline screen, tap **Enter setup mode** twice within 10 seconds. This explicit recovery persists `setup_next`, then reboots into setup. A successful replacement save clears that flag in the same settings commit. Missing, invalid or unsupported settings also enter setup; real storage read errors stop provisioning startup without erasing NVS. Reboot persistence, two-tap recovery and replacement-save behavior need board verification.

The display acknowledgement contract is preserved: setup must render and acknowledge its credentials before the AP is opened; a missing acknowledgement fails closed. The historical withheld-acknowledgement test above does not establish physical panel completion or validate the new reboot flow.

### Display startup and reboot

Startup keeps the backlight off until the first screen refresh, a 100 ms settling window, and a requested full-screen redraw. Provisioning reboots blank and hold the backlight low before calling `esp_restart()`; the shutdown callback shares the same single-run handoff. The next boot releases the hold only after the BSP restores zero-duty PWM. There is no extra reboot pause, and the setup credential display gate is unchanged.

The owner reported no startup flash with the settling window and no software-reboot flash with early single-run blanking in a diagnostic build without the one-second pause. That path is now in normal firmware; the temporary reboot diagnostic option and phase logs were removed. Re-test the normal image after flashing, including entering setup and rebooting after saving.

### Display-stall diagnostic build

Normal builds leave `MOSAIC_DISPLAY_DIAGNOSTICS` off. To distinguish a blocking PPA rotation from a frame-buffer wait without changing ESP-IDF or managed components, build and flash a separate image:

```sh
idf.py -B build/display-diagnostics -D SDKCONFIG=build/display-diagnostics/sdkconfig -D MOSAIC_DISPLAY_DIAGNOSTICS=ON build
idf.py -B build/display-diagnostics flash monitor
```

If setup refresh times out, `mosaic_display` logs entry/return counts for both calls (and the last PPA rectangle size). If PPA entered > returned, rotation is stuck; if buffer wait entered > returned, its completion/release path is stuck. These counters contain no SSID or password. Do not use this build for production; the normal `idf.py build` remains unaffected. Keep the AP off until the password has actually rendered. The project-local PPA override now applies to both builds; the counters are only for diagnosis.

`dependencies.lock` remains ignored until the clean ESP-IDF 5.5.4 resolution and transitive dependencies have been reviewed for reproducibility. The BSP/driver direct registry versions below are pinned in their manifests. Review and commit a clean firmware lock separately.

UI/runtime/display host tests: `python3 main/ui/tests/run.py` from `firmware/`. Focused provisioning host tests: `python3 main/features/provisioning/tests/run.py` from `firmware/`. Browser controller tests: `node --test firmware/main/features/provisioning/tests/setup_ui.test.mjs` from the Mosaic root. Neither replaces a board-and-phone setup/recovery test. Never put real credentials in logs or test fixtures.

## Formatting and analysis

Format only firmware-owned source, not vendor or generated C:

```sh
pnpm fw:format           # from mosaic/
pnpm fw:format:check     # from mosaic/
# After a successful IDF configure produces build/compile_commands.json:
clang-tidy -p build main/app_main.cpp
```

Run `clang-tidy` only on owned source paths; the `.clang-tidy` header filter is scoped to `main/`. Do not pass vendor or generated C files to the check. For editor diagnostics, `.clangd` points clangd at `build/compile_commands.json`. On macOS, `.clangd` replaces ESP-IDF's GCC-specific `-march=rv32imafc_zicsr_zifencei_xesploop_xespv` with the base RISC-V ISA for editor parsing only; firmware builds still use the original toolchain flags. clangd also needs the cross-compiler's system headers: configure Zed's clangd arguments with `--query-driver=<absolute path to the ESP-IDF riscv32-esp-elf-g++ binary>` (find the path in `build/compile_commands.json`). Do not add an explicit `--target` in `.clangd`: clangd passes it to GCC during driver discovery, which fails. This configuration was checked locally with Apple's `/usr/bin/clangd` and `clangd --check=main/app_main.cpp --query-driver=<path>`; upstream LLVM (`brew install llvm`) remains an option if Apple clangd cannot handle other toolchain flags. Restart the language server after changing its arguments or rerunning IDF configuration. The project `.clangd` also removes the confirmed GCC-only `-fno-shrink-wrap`, `-fno-tree-switch-conversion`, and `-fstrict-volatile-bitfields` flags from editor parsing only. If LLVM clangd reports other unsupported flags, review them individually; do not edit ESP-IDF build flags or assume compiler errors are false. Exceptions and RTTI remain disabled by IDF defaults; no project option enables them. Do not put Wi-Fi passwords in `sdkconfig.secrets.defaults`; they are entered over the temporary AP and stored in NVS. `sdkconfig.secrets.defaults.example` remains a local-file convention for later features.

## Component provenance

The component sources, headers, manifests, documentation and licenses are included in this repository. Generated device bindings come from `platform/packages/protocols`.

| Component                    | Version/source                                                                                                                                                                                                                                   | License                                                                              |
| ---------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------ |
| `esp32_p4_wifi6_touch_lcd_5` | Waveshare BSP manifest 1.0.1, [Waveshare components](https://github.com/WaveshareCloud/Waveshare-ESP32-components); direct registry versions narrowed to codec 1.6.2, GT911 1.2.1, LVGL adapter 0.6.4, HX8394 1.0.3                              | bundled `LICENSE` (Apache-2.0)                                                       |
| `esp_lcd_hx8394`             | Waveshare manifest 1.0.3, [upstream commit `49e44e4`](https://github.com/waveshareteam/Waveshare-ESP32-components/commit/49e44e4a5b81951f3af25f6a12c481b58d20ae18); direct registry versions narrowed to CMake utilities 0.5.3 and I2C bus 1.5.2 | bundled `license.txt` (MIT)                                                          |
| `esp_driver_ppa`             | Project-local override copied from ESP-IDF 5.5.4; [single upstream-derived workaround](components/esp_driver_ppa/README.md)                                                                                                           | Apache-2.0 (upstream ESP-IDF source)                                                  |
| `nanopb`                     | `NANOPB_VERSION` = `nanopb-0.4.9`; [upstream 0.4.9](https://github.com/nanopb/nanopb/tree/0.4.9)                                                                                                                     | upstream 0.4.9 `LICENSE.txt` (zlib) |

The direct registry pins have **not** been independently validated against a separate clean resolve. The generated device bindings and board components compile/link into `build/device_firmware.bin`; the target-board SoftAP/STA smoke test above is user-reported. Confirm the ESP-IDF version in the activated shell and review the resolved lockfile before treating the build as reproducible or production-ready.
