# M2 board verification

What M2 device behavior works on the physical board with every peripheral connected ([#89](https://github.com/mosaic-device/mosaic/issues/89)). Expected behavior comes from the [firmware README](../../firmware/README.md); where the firmware still uses provisional values that differ from the [#56 spec](https://github.com/mosaic-device/mosaic/wiki/Device-Experience-Spec), the firmware is what's tested and the difference is listed under [Differences from #56](#differences-from-56).

Result values: **Pass**, **Fail** (link the `bug` issue), or **Not tested** (say why).

## Setup under test

| Item              | Value                                                                                                                      |
| ----------------- | -------------------------------------------------------------------------------------------------------------------------- |
| Date              | _YYYY-MM-DD_                                                                                                               |
| Tester            | _name_                                                                                                                     |
| Firmware revision | _`git rev-parse --short HEAD`_, clean tree                                                                                 |
| Image             | `build/m2-verify`: `MOSAIC_DEBUG_TRIGGERS`, `MOSAIC_ENCODER_TRACE`, `MOSAIC_BRIGHTNESS_TRACE` on; otherwise the normal app |
| ESP-IDF           | 5.5.4                                                                                                                      |
| Board             | Waveshare ESP32-P4-WIFI6-Touch-LCD-5, revision _as printed on the board_                                                   |
| Power             | USB-C into the board's USB-to-UART port from _supplier / laptop model_, cable _description_, USB extension _fitted or not_ |
| Wi-Fi network     | _2.4 GHz / 5 GHz, open or WPA2_ (no SSID)                                                                                  |

### Wiring

Pin map from the [hardware wiki](https://github.com/mosaic-device/mosaic/wiki/Hardware-Wiring-and-Power). Fill in what is actually on the bench.

| Connection        | GPIO    | Header pin        | As built                                                   |
| ----------------- | ------- | ----------------- | ---------------------------------------------------------- |
| Encoder CLK / A   | 5       | 11                |                                                            |
| Encoder DT / B    | 4       | 14                |                                                            |
| Encoder SW        | 2       | 8                 |                                                            |
| Encoder VCC       | —       | 2 or 18 (3.3V)    |                                                            |
| Ring DIN          | 28      | 16                | series resistor: _Ω_                                       |
| Strip DIN         | 29      | 20                | series resistor: _Ω_; LED count: _n_ (firmware assumes 12) |
| Ring and strip 5V | —       | 1 or 3            | wire length: _cm_; capacitor: _fitted / not fitted_        |
| BH1750 SDA / SCL  | 21 / 22 | 15 / 17           | ADDR: _low / unconnected_                                  |
| BH1750 VCC        | —       | 2 or 18 (3.3V)    |                                                            |
| Speaker           | —       | speaker connector | 8Ω 2W supplied speaker                                     |
| Level shifter     | —       | —                 | _none_                                                     |

## Procedure

Build and flash from `firmware/`, then keep the monitor open for the whole session:

```sh
python3 ../scripts/firmware.py -B build/m2-verify -D SDKCONFIG=build/m2-verify/sdkconfig \
  -D MOSAIC_DEBUG_TRIGGERS=ON -D MOSAIC_ENCODER_TRACE=ON -D MOSAIC_BRIGHTNESS_TRACE=ON build
python3 ../scripts/firmware.py -B build/m2-verify flash monitor
```

Start from erased settings so first-run defaults and setup are covered: `python3 ../scripts/firmware.py -B build/m2-verify erase-flash` before flashing. Debug commands are typed at the `mosaic>` prompt. "Both inputs" means do the step once by touch and once with the encoder only (turn to focus, short press to activate, 600 ms hold for Back).

### 1. Boot and setup screens

| #   | Step                                                               | Expected                                                                                              | Touch | Encoder |
| --- | ------------------------------------------------------------------ | ----------------------------------------------------------------------------------------------------- | ----- | ------- |
| 1.1 | Power on with erased flash                                         | LEDs dark from power-on until a pattern is requested; `LEDs ready` logged; boot screen, then setup QR | —     | —       |
| 1.2 | Join `Mosaic-XXXXXX` from a phone, submit Wi-Fi and a `wss://` URL | Success shown about 6 s, reboot, "Connecting to saved Wi-Fi..."                                       | —     | —       |
| 1.3 | After joining                                                      | "Wi-Fi connected · Waiting for device pairing."; "Enter setup mode" focusable                         |       |         |
| 1.4 | Activate "Enter setup mode" once, wait 10 s                        | Label asks to confirm, then reverts; nothing else happens                                             |       |         |
| 1.5 | Activate it twice within 10 s                                      | Setup QR opens; resubmit the same settings to return                                                  |       |         |
| 1.6 | Turn off the access point                                          | "Offline … Retrying; tap to enter setup."; Settings closes if open                                    |       |         |
| 1.7 | Restore the access point                                           | Rejoins and returns to the unpaired screen                                                            | —     | —       |
| 1.8 | Hold encoder (Back) on any setup screen                            | Nothing happens                                                                                       | —     |         |

### 2. Home and Settings

Run `trigger connected` to reach Home.

| #   | Step                                       | Expected                                                                                 | Touch | Encoder |
| --- | ------------------------------------------ | ---------------------------------------------------------------------------------------- | ----- | ------- |
| 2.1 | `trigger connected`                        | Home appears with the clock and Settings                                                 | —     | —       |
| 2.2 | Back on Home                               | Nothing happens                                                                          | —     |         |
| 2.3 | Open Settings                              | Settings opens, focus on the first control                                               |       |         |
| 2.4 | Move through every control                 | Encoder skips disabled controls (brightness while automatic is on, a stepper at its end) | —     |         |
| 2.5 | Toggle each checkbox and step each stepper | Value changes and applies at once                                                        |       |         |
| 2.6 | Back (on-screen button and encoder hold)   | Home, with focus on Settings                                                             |       |         |
| 2.7 | Reopen Settings                            | Focus is where it was left                                                               |       |         |
| 2.8 | `trigger disconnected`                     | Unpaired screen returns                                                                  | —     | —       |

### 3. Debug triggers: LEDs and sound

Default settings (volume 60%, not muted, LEDs on). Start from `trigger disconnected`.

| #    | Command                                                | Ring                                                               | Strip                            | Sound              | Result |
| ---- | ------------------------------------------------------ | ------------------------------------------------------------------ | -------------------------------- | ------------------ | ------ |
| 3.1  | `trigger pairing`                                      | blue, spinning                                                     | blue, breathing                  | none               |        |
| 3.2  | `trigger connected`                                    | off                                                                | dim warm white, steady           | three rising notes |        |
| 3.3  | `trigger notification`                                 | cool white, spinning for 3 s, then back to the status              | warm white, breathing, then back | two rising notes   |        |
| 3.4  | `trigger error`                                        | red, blinking                                                      | dim red, steady                  | two falling notes  |        |
| 3.5  | `trigger disconnected`                                 | off                                                                | off                              | none               |        |
| 3.6  | Repeat a command for the current status                | nothing posted, nothing changes                                    |                                  |                    |        |
| 3.7  | `volume 20`, `trigger notification`; then `volume 100` | quieter, then louder from the next cue                             |                                  |                    |        |
| 3.8  | `mute on`, `trigger notification`; `mute off`          | no sound while muted; LEDs still change                            |                                  |                    |        |
| 3.9  | LED lights off in Settings, run 3.1–3.4                | both outputs stay dark; turning on shows the current pattern       |                                  |                    |        |
| 3.10 | Every pattern at the brightest ambient light           | nothing brighter than the 30% ceiling, no random colors or flicker |                                  |                    |        |

### 4. Ambient brightness

| #   | Step                                             | Expected                                                                                    | Result |
| --- | ------------------------------------------------ | ------------------------------------------------------------------------------------------- | ------ |
| 4.1 | Boot with sensor connected                       | `Ambient light sensor ready`                                                                |        |
| 4.2 | Cover the sensor, then shine a light on it       | Screen and LEDs fade (a few seconds) dimmer, then brighter; trace logs readings; no flicker |        |
| 4.3 | Hold the light level steady near a change point  | Levels don't waver                                                                          |        |
| 4.4 | Automatic brightness off, step brightness 40→100 | Screen and LEDs follow the manual level with a fade                                         |        |
| 4.5 | Unplug the sensor while running                  | One warning under `mosaic_brightness`; fades to fixed levels; no further log spam           |        |
| 4.6 | Plug it back in                                  | `Ambient light sensor ready` within about 5 s; automatic levels resume                      |        |
| 4.7 | Boot with the sensor unplugged                   | One warning; fixed levels; device otherwise normal                                          |        |

### 5. Clock

| #   | Step                             | Expected                                                                              | Result |
| --- | -------------------------------- | ------------------------------------------------------------------------------------- | ------ |
| 5.1 | Boot, before Wi-Fi joins         | `Clock waiting for Wi-Fi`; Home (`trigger connected`) shows `--:--`                   |        |
| 5.2 | After Wi-Fi joins                | `Time synced`; Home shows Arizona time, 12-hour, no AM/PM, matching a reference clock |        |
| 5.3 | Reboot with the access point off | Home shows `--:--`, not the old time                                                  |        |

### 6. Settings persistence

| #   | Step                                                    | Expected                                                                         | Result |
| --- | ------------------------------------------------------- | -------------------------------------------------------------------------------- | ------ |
| 6.1 | First boot after erase                                  | Defaults: automatic brightness on, brightness 70, volume 60%, not muted, LEDs on |        |
| 6.2 | Change every setting, wait 2 s, power cycle             | All changes kept                                                                 |        |
| 6.3 | Change a setting and power cycle within half a second   | Previous value kept, no warning about an unreadable record                       |        |
| 6.4 | `volume 30` and `mute on` from the console, power cycle | Kept, and shown in Settings                                                      |        |

### 7. Power cycles under full load

Run at least five cycles. Before each: Wi-Fi joined, `trigger notification` repeated so the ring, strip and speaker are all active, automatic brightness off at 100 so the LEDs sit at the ceiling. Unplug USB, wait 5 s, plug in. Use the intended wire lengths and USB extension.

| Cycle | LEDs dark at startup | No flicker or random colors | No reset or brownout in the log | Rejoins Wi-Fi | Board, LEDs and regulator not unusually hot | Result |
| ----- | -------------------- | --------------------------- | ------------------------------- | ------------- | ------------------------------------------- | ------ |
| 1     |                      |                             |                                 |               |                                             |        |
| 2     |                      |                             |                                 |               |                                             |        |
| 3     |                      |                             |                                 |               |                                             |        |
| 4     |                      |                             |                                 |               |                                             |        |
| 5     |                      |                             |                                 |               |                                             |        |

Also leave it running under that load for 15 minutes and note any reset (`rst:` lines or a reboot banner in the monitor) or heat.

## Results

_Summary filled in after testing: what is verified, what failed (with `bug` issues), and what was not tested._

### Not verified

- **Header current capacity and total current draw.** No measurement equipment; the 5V header's rating is unknown and the LED load (about 360 mA at the 30% ceiling) is an estimate. Passing power cycles shows this assembly works, not that it has margin.
- **Notification and notification-detail screens.** Not implemented until M3; only the notification LED and sound feedback is checked here.
- **Pairing, server connection, update required.** M3; the status changes are simulated with debug triggers.

## Differences from #56

The firmware's provisional tables predate the written [#56 spec](https://github.com/mosaic-device/mosaic/wiki/Device-Experience-Spec). These aren't board failures, but the firmware has to be changed to match the spec:

- **LED meanings.** The spec has both lights in the user's color (default white) normally and fading to blue for a notification until Activity is checked, never flashing (FR-12). The firmware turns the ring off when idle, uses spinning, breathing and blinking patterns, shows red for error, and turns notification cool/warm white for 3 s.
- **Error sound.** The spec has no sound on error; the firmware plays two falling notes.
- **Brightness curve.** The spec proposes the strip at about 5–60%; the firmware sets 23–100% of the 30% ceiling for both lights.
- **Unavailable controls.** The spec dims unavailable controls with a short reason in text; the firmware disables them without a reason.
