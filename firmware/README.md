# ESP32-P4 firmware

Firmware for the Waveshare ESP32-P4-WIFI6-Touch-LCD-5 using ESP-IDF **5.5.4** and ESP32-C6-hosted Wi-Fi. Display/touch, phone setup, credential save, reboot, Wi-Fi join and recovery have been verified on the physical board. The production dashboard, pairing and backend transport are not implemented; device states can be injected with [debug triggers](#debug-triggers).

## Build and flash

Activate ESP-IDF with the ESP32-P4 toolchain and put Node.js on `PATH`. From `firmware/`:

```sh
python3 ../scripts/firmware.py build
python3 ../scripts/firmware.py flash monitor
```

To use current defaults without changing a local `sdkconfig`:

```sh
python3 ../scripts/firmware.py -B build/softap -D SDKCONFIG=build/softap/sdkconfig build
```

Existing `sdkconfig` values override `sdkconfig.defaults`. Review configuration before flashing. The board has 32 MiB flash and an 8 MiB factory partition; no filesystem image is needed.

- Protocol bindings are committed; regenerate through `platform/packages/protocols`, never by hand.
- CMake regenerates token outputs offline using Node.js. Firmware builds do not require pnpm or asset converters.
- Finished fonts/icons and licenses are committed; see [asset maintenance](../platform/packages/assets/README.md).
- The [local PPA override](components/esp_driver_ppa/README.md) fixes a rotation stall. Reconfigure after changing components and re-test display startup when changing IDF, geometry or PPA usage.
- `dependencies.lock` is committed. Use the wrapper above (or root `pnpm fw:*` commands), not raw `idf.py`: component manager 2.x writes machine-specific local paths. The wrapper expands the committed `${MOSAIC_FIRMWARE_DIR}` placeholder before IDF runs and restores it afterward, preserving dependency updates. It always runs from `firmware/`; paths passed to IDF are relative to that directory. CI must use the same wrapper.
- Do not run firmware commands concurrently. After a forced termination or machine crash, inspect the lockfile, restore only the local driver path to the placeholder, and remove `.mosaic-idf.lock` before retrying. Do not discard genuine dependency updates. Component manager 3.x supports relative paths but requires ESP-IDF 6+.
- Test lock portability without hardware: `python3 ../scripts/firmware_test.py`. These tests simulate IDF in relocated directories; they do not prove a clean ESP-IDF build or physical provisioning.

## Phone setup

1. Scan the displayed Wi-Fi QR or join `Mosaic-XXXXXX` using the displayed password.
2. If the captive portal does not open, visit the displayed **HTTP** URL, normally `http://192.168.4.1/`.
3. Enter the home SSID, password and `wss://` server URL, then tap **Save and reboot**. Passwords must be empty for an open network or 8–63 characters.
4. The board saves settings, shows success for about six seconds, then reboots and joins the saved network.

Setup does not test Wi-Fi connectivity or contact the server. Incorrect credentials are saved too: the device retries Wi-Fi rather than automatically opening an AP. To correct them, tap **Enter setup mode** twice within ten seconds, then submit replacement settings. Storage failures allow retry; if the browser loses the save response, check the device before resubmitting.

Setup credentials must receive display acknowledgement before the AP opens; failure keeps AP/HTTP/DNS closed. The QR joins Wi-Fi but cannot guarantee portal opening. Networks requiring browser sign-in are unsupported.

## Development gallery

The gallery exercises typography, assets and passive controls without starting provisioning, Wi-Fi or reading saved credentials. Keep its configuration separate from normal firmware:

```sh
python3 ../scripts/firmware.py -B build/design-system-gallery -D SDKCONFIG=build/design-system-gallery/sdkconfig -D MOSAIC_UI_GALLERY=ON build
python3 ../scripts/firmware.py -B build/design-system-gallery flash monitor
```

Swipe to browse; interactive controls update the gallery state. **Recreate gallery controls** exercises destruction/recreation. Fixed state samples are not interactive; keyboard navigation and text editing are not implemented. Gallery layout, tint, touch geometry and performance require their own physical review.

Shared helpers live in `main/ui/styles/` and `main/ui/components/`. Create controls inside runtime renderer callbacks and render authoritative feature state; callbacks report actions, not completion. Screens own objects and scrolling; callback contexts must outlive controls. Styles live for the process lifetime. Firmware tokens use desk-distance text sizing and an 80 px minimum control height. Use generated asset symbols and explicit mask recoloring.

## Debug triggers

Navigation, LED and speaker feedback follow `MOSAIC_EVENT` events on a dedicated ESP-IDF [`esp_event`](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32p4/api-reference/system/esp_event.html) loop, wrapped by `main/events/bus.hpp`. `MOSAIC_EVENT_STATUS_CHANGED` carries the connection status (disconnected, pairing, connected, error) and is posted only when it changes; `MOSAIC_EVENT_NOTIFICATION_RECEIVED` is posted once per notification. Features subscribe with `events::subscribe()` after `events::start()`; handlers run on the `mosaic_events` task and must hand blocking work (audio, LVGL) to their own task. A post that cannot be queued within 100 ms is logged and returned as an error. The M3 connection will publish here. Until then, a development-only image adds a serial command that publishes through the same bus:

```sh
python3 ../scripts/firmware.py -B build/debug-triggers -D SDKCONFIG=build/debug-triggers/sdkconfig -D MOSAIC_DEBUG_TRIGGERS=ON build
python3 ../scripts/firmware.py -B build/debug-triggers flash monitor
```

At the `mosaic>` prompt on the UART console, run `trigger notification`, `trigger pairing`, `trigger connected`, `trigger disconnected` or `trigger error`. Posted events log `Status: <name>` or `Notification` under `mosaic_events`; setting the current status again posts nothing. `volume [0-100]` and `mute [on|off]` show or change speaker cue output until restart. Provisioning and Wi-Fi run normally. The option is off by default and cannot be combined with the gallery; the CI firmware build checks that the normal image contains no trigger code.

## Tests and tooling

From `firmware/`:

```sh
python3 main/features/provisioning/tests/run.py
python3 main/support/tests/run.py
python3 main/ui/tests/run.py
python3 main/events/tests/run.py
python3 main/drivers/tests/run.py
python3 main/features/led_feedback/tests/run.py
python3 main/features/audio_cues/tests/run.py
python3 main/features/brightness/tests/run.py
python3 main/features/clock/tests/run.py
clang-tidy -p build main/app_main.cpp
```

From the repository root:

```sh
node --test firmware/main/features/provisioning/tests/setup_ui.test.mjs
pnpm fw:format
pnpm fw:format:check
```

Run analysis only on owned source, not vendor/generated code. Configure clangd with `--query-driver=<absolute path to riscv32-esp-elf-g++>` from `build/compile_commands.json`. `.clangd` adjusts unsupported GCC flags for editor parsing only; do not change firmware compiler flags to fix editor diagnostics.

### Display diagnostics

Use a separate image to distinguish a blocking PPA rotation from a frame-buffer wait:

```sh
python3 ../scripts/firmware.py -B build/display-diagnostics -D SDKCONFIG=build/display-diagnostics/sdkconfig -D MOSAIC_DISPLAY_DIAGNOSTICS=ON build
python3 ../scripts/firmware.py -B build/display-diagnostics flash monitor
```

On refresh timeout, `mosaic_display` logs entry/return counters. More entries than returns identify the blocked operation. Diagnostics are off in normal builds. Startup waits for refresh, a 100 ms settling window and redraw before lighting the panel; reboot blanks and holds the backlight low.

## Rotary encoder

A KY-040 on GPIO 5 (A/CLK), 4 (B/DT) and 2 (switch) is an LVGL encoder: turning moves focus and pressing activates. Power the module from 3.3V, never 5V, because its pull-ups go to its VCC pin. The PCNT peripheral counts edges in hardware, and `drivers/encoder_logic.cpp` turns the count into one step per detent, realigning at each rest position. `espressif/button` debounces the switch. A press shorter than 600 ms clicks when released, and a latch hands each click to LVGL so a tap between input reads still clicks. Holding the button for 600 ms is Back: it never reaches LVGL, so it cannot also click or toggle a slider's edit mode. If the encoder fails to start, the UI logs it and keeps touch input.

Steps and presses are logged at Debug, which normal builds compile out. For a board check, use a gallery build in its own directory (its focusable controls show focus moving) with `MOSAIC_ENCODER_TRACE`, which raises only the encoder's logs:

```sh
python3 ../scripts/firmware.py -B build/encoder-trace -D SDKCONFIG=build/encoder-trace/sdkconfig -D MOSAIC_UI_GALLERY=ON -D MOSAIC_ENCODER_TRACE=ON build
python3 ../scripts/firmware.py -B build/encoder-trace flash monitor
```

Boot logs `Rotary encoder ready`; each click logs `Step +1`/`Step -1` (a fast spin may log larger steps) and each short press logs `Click` and each long press logs `Back` under `mosaic_encoder`. Do not raise the global default log level instead: early boot then logs long enough to trip the bootloader watchdog and reset in a loop.

## Screens and navigation

`main/ui/navigation/navigator.cpp` is the device's screen map, and `main/app/screens.cpp` connects it to the features:

| Screen | Shown when | Back |
| --- | --- | --- |
| Boot | until provisioning reports its first state | nothing |
| Setup | provisioning's screens: setup QR, connecting, offline, and "Wi-Fi connected · waiting for pairing" (unpaired) | nothing |
| Home | Wi-Fi online and paired (status `connected`) | nothing |
| Settings | opened from Home | Home, with focus back on Settings |
| Notification | reserved for M3, above Home or Settings | the screen underneath |

Home appears only once the device is paired; with debug triggers, `trigger connected` shows it and `trigger disconnected` returns to the unpaired screen. Losing Wi-Fi, or setup credentials appearing, brings provisioning's screens to the front and closes Settings. What opens from where is provisional until #56.

Touch and the encoder share focus. Each screen has its own focus group, so turning moves through the visible screen's controls only (skipping disabled ones) and a screen remembers its focus when you return. A short press activates the focused control, the same as a tap. Holding the encoder button for 600 ms is Back, as is the on-screen Back button. Back with nowhere to go does nothing.

`main/ui/navigation/host.cpp` gives each screen a container and focus group inside the runtime's single renderer. Provisioning's renderer keeps its setup credentials handshake: credentials bring its screen to the front before they render. M3 adds the notification screen as `Screens::notification` and opens it with `navigation::open()`.

## LED ring and strip

The WS2812 ring (8 LEDs) takes data on GPIO 28 and the strip on GPIO 29, both driven directly from 3.3V GPIO by the RMT peripheral; see the [hardware wiki](https://github.com/mosaic-device/mosaic/wiki/Hardware-Wiring-and-Power) for power and the series resistor. The strip length is set to the planned 12 LEDs in `main/drivers/leds.hpp`; change it there once the strip is cut. The driver starts first in `app_main`, sends a dark frame and blanks the LEDs again on restart. Every value it sends passes through `limit()` in `main/drivers/leds_logic.hpp`, which scales full-scale colors onto the 30% ceiling (76/255 per channel).

`main/features/led_feedback/` shows the device status and notifications. The patterns in `patterns.cpp` are provisional until #56 defines LED meanings; edit them there:

| Event | Ring | Strip |
| --- | --- | --- |
| `disconnected` | off | off |
| `pairing` | blue, spinning | blue, breathing |
| `connected` (idle) | off | dim warm white, steady |
| `error` | red, blinking | dim red, steady |
| `notification` (3 s, then back to the status) | cool white, spinning | warm white, breathing |

To check them on the board, flash the [debug triggers](#debug-triggers) image and run each `trigger` command. The boot log shows `LEDs ready` under `mosaic_leds` with the pins, lengths and ceiling.

## Clock

Home shows the time as 12-hour local time without AM/PM, such as `2:05`. Each time Wi-Fi joins, `main/features/clock/` syncs over SNTP from `pool.ntp.org`, and ESP-IDF re-syncs hourly while connected. Startup never waits for it.

Until a sync succeeds this boot, Home shows `--:--`, and it stays that way if sync never works. A time kept across a restart without Wi-Fi, or one earlier than 2026, is never shown as correct. Boot logs `Clock waiting for Wi-Fi` under `mosaic_clock`, and the first sync logs `Time synced`.

**Time zone:** every device uses Arizona time (`MST7`: UTC-7 all year, no daylight saving), where Mosaic is developed. It is `kTimeZone` in `main/features/clock/clock.hpp`; the owner's zone can come from setup or the server later. To check it on the board, flash the [debug triggers](#debug-triggers) image, let Wi-Fi join, and run `trigger connected` to show Home.

## Speaker cues

Cues play through the board's ES8311 codec and the speaker connector, using the BSP's `bsp_audio_codec_speaker_init()`. Opening the codec for a cue turns the amplifier on, and closing it afterwards turns it off, so the amplifier is on only while a cue plays. Each cue starts after 50 ms of silence, while the DAC and amplifier settle, and ends with 80 ms of silence that flushes the I2S buffers before the amplifier switches off. Every cue logs `i2s_channel_disable(…): the channel has not been enabled yet` from `i2s_common`: the codec library reconfigures I2S on each open without checking that the previous close already disabled it. It is harmless; avoiding it would mean keeping the codec, and so the amplifier, on between cues.

`main/features/audio_cues/cues.cpp` maps events to cues; the choices are provisional until #56:

| Event | Cue |
| --- | --- |
| notification | `notification`: two rising notes |
| status `connected` | `connected`: three rising notes |
| status `error` | `error`: two falling notes |
| `pairing`, `disconnected` | none |

Volume (default 60%) applies from the next cue. Muting, or volume 0, skips cues without opening the codec, and stops a playing cue within about 10 ms. Settings will store both (#88); until then they reset on restart. If cues arrive faster than they play, up to four wait and the rest are dropped.

The cues are 22050 Hz mono 16-bit WAV files in `main/features/audio_cues/generated/`, synthesized from a spec in `platform/packages/assets` and original to Mosaic; change them there, not here. To check them on the board, flash the [debug triggers](#debug-triggers) image and run `trigger notification`, `trigger connected` and `trigger error`, with `volume` and `mute` in between.

## Ambient brightness

A BH1750 light sensor sets the screen and LED brightness. It has its own I2C bus: SDA on GPIO 21, SCL on GPIO 22, 100 kHz, address 0x23 (ADDR low or unconnected), separate from the BSP's touch and audio bus on GPIO 7/8. Power the breakout from 3.3V: its pull-ups go to its VCC, and the P4's pins must never see 5V. The P4's weak internal pull-ups are also enabled as a backstop.

`main/features/brightness/policy.cpp` turns readings into levels. It works in perceived brightness (0-100, where 50 looks half as bright as 100) and converts to PWM only for the drivers, using CIE 1976 lightness, because eyes judge light roughly logarithmically and equal PWM steps look uneven. The curve is provisional until #56:

| Light | Screen, perceived (PWM) | LEDs, perceived (share of the 30% ceiling) |
| --- | --- | --- |
| 1 lux or darker | 45 (15%) | 55 (23%) |
| 20 lux | 65 (34%) | 75 (48%) |
| 150 lux | 85 (66%) | 90 (76%) |
| 600 lux or brighter | 100 (100%) | 100 (100%) |

Between points, levels follow log(lux). Readings every 200 ms are smoothed over about a second. Levels change only once the light moves more than about 26% from where they were last set, so light wavering near a point never flickers them, and each change fades in equal perceived steps over a few seconds. The LED level only dims below the 30% ceiling, and the display driver never lets the backlight drop below 10%, because the setup credentials handshake treats a lit panel as visible.

If the sensor is missing at startup or stops responding, the device logs one warning under `mosaic_brightness`, fades to fixed levels (perceived screen 85, LEDs 90) and retries every 5 s without logging; it logs `Ambient light sensor ready` when the sensor returns.

To check it on the board, build with `MOSAIC_BRIGHTNESS_TRACE`, which logs each reading that changes the levels:

```sh
python3 ../scripts/firmware.py -B build/brightness-trace -D SDKCONFIG=build/brightness-trace/sdkconfig -D MOSAIC_BRIGHTNESS_TRACE=ON build
python3 ../scripts/firmware.py -B build/brightness-trace flash monitor
```

Cover the sensor and shine a light on it; the screen and LEDs should fade without flicker. Unplug the sensor to see the fallback, and plug it back in to see it recover.

## Architecture and limits

See [provisioning design constraints](main/features/provisioning/README.md#design-constraints) and the [provisioning decision](../docs/adr/0001-softap-provisioning.md).

Setup uses HTTP on a temporary WPA2 AP. Never log credentials or put them in build configuration. Flash/NVS encryption, factory-reset policy, broader phone compatibility, fault injection and LAN-isolation testing remain separate production-readiness work.

## Component provenance

Bundled components retain their source, manifests and licenses:

| Component | Source | License |
| --- | --- | --- |
| `esp32_p4_wifi6_touch_lcd_5` | Waveshare BSP 1.0.1; dependency pins in manifests | Apache-2.0 |
| `esp_lcd_hx8394` | Waveshare 1.0.3, upstream commit `49e44e4` | MIT |
| `esp_driver_ppa` | ESP-IDF 5.5.4 with local workaround; see component README | Apache-2.0 |
| `nanopb` | 0.4.9 | zlib |
