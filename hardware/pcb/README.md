# Peripheral carrier PCB

A 30 × 70 mm, two-layer board that plugs onto pins 1–20 of the Waveshare ESP32-P4-WIFI6-Touch-LCD-5's 40-pin header and gives each peripheral its own latching JST-XH connector. It replaces the hand-wired perfboard prototype. The pin map comes from the [hardware wiki page](https://github.com/mosaic-device/mosaic/wiki/Hardware-Wiring-and-Power); the firmware needs no change.

The KiCad 10 project is in [`mosaic-carrier/`](mosaic-carrier/). Open `mosaic-carrier.kicad_pro` in KiCad to view or edit the schematic and layout.

## Fitting it

- **J1** is a male 2 × 10 header soldered on the **underside**, plastic against the board. It plugs into the Waveshare's female socket at pins 1–20.
- Pin 1 (the square pad, marked `1` on the top) goes at the end of the Waveshare header nearest the corner mounting hole, with the 5V row nearest the board edge. The carrier then lies over the Waveshare board, running from the corner end toward its middle.
- Nothing else is on the underside, so it clears the parts beneath it.

## Connectors

Each cable is wired to its module at one end and a JST-XH housing at the other. The pin order follows each module's own pin order, so the wires don't cross.

| Connector    | Edge | Pins, from pin 1                               | Module pins                     |
| ------------ | ---- | ---------------------------------------------- | ------------------------------- |
| J2 Encoder   | Top  | GND, 3.3V, SW (GPIO2), DT (GPIO4), CLK (GPIO5) | KY-040: GND, +, SW, DT, CLK     |
| J3 Light     | Top  | SDA (GPIO21), SCL (GPIO22), GND, 3.3V          | GY-302: SDA, SCL, GND, VCC      |
| J4 LED ring  | Left | 5V, DIN (from GPIO28), GND                     | Ring: 5V, DIN, GND; DOUT unused |
| J5 LED strip | Left | 5V, DIN (from GPIO29), GND                     | Strip: 5V, DIN, GND             |

Pin 1 is marked with a triangle beside each connector. The GY-302's ADDR pin stays unconnected (address `0x23`). The speaker stays on the Waveshare board's own connector.

Crimp tools are optional: pre-crimped JST-XH leads with housings (3-, 4- and 5-pin) only need their bare ends soldered to the modules.

## Circuit

- **LED data** goes through a 74AHCT1G125 buffer powered from the LED 5V, so the LEDs get a full 5V signal. Three-pad solder jumpers JP1 (ring) and JP2 (strip) choose the path. The middle pad feeds the LED; it comes joined to the **BUF** pad by a thin copper link, so data goes through the buffer. To drive the LEDs straight from the GPIO, as on the prototype, cut that link with a knife and bridge the middle pad to the **GPIO** pad with solder. To go back, remove that solder and bridge the middle pad to **BUF** instead.
- **330 Ω** in each data line, at the board. It limits current into the first LED if its supply is missing and damps ringing on the cable; at 800 kHz it costs nothing in signal timing.
- **10 kΩ pull-downs** (R1, R2) hold the data lines low while the P4 boots, so the LEDs don't latch random colors.
- **F1**, a 1.1 A resettable fuse (1206, 0.8 mm tall), protects the header's 5V from a shorted LED cable. Firmware caps LED brightness at 30%, about 0.36 A for 8 + 12 LEDs. If it trips, the LEDs and their buffers go dark while the rest of the device keeps running; unplug, fix the short, and it reconnects on its own after about 30 seconds.
- **C1 and C5**, two 22 µF ceramics, sit across the LED supply. They are about 1.25 mm tall, so they fit where the board's edge is tight, and each LED has its own 100 nF beside it. A large electrolytic would only add height and a bigger surge from USB at plug-in.
- **I2C pull-ups** are on the GY-302 module, so the board has none. A sensor without its own needs 4.7 kΩ to 3.3V on SDA and SCL at the sensor end.
- The encoder needs no pull-ups on the board: the KY-040 has 10 kΩ on CLK, DT and SW.

## Check and export

```sh
pnpm pcb:check
pnpm pcb:export
```

`pcb:check` runs ERC and DRC (including schematic parity) and fails on any violation. `pcb:export` then writes `hardware/pcb/build/`, which is not committed:

| File                           | Use                                                                    |
| ------------------------------ | ---------------------------------------------------------------------- |
| `mosaic-carrier-gerbers.zip`   | Gerbers and drill files to upload to JLCPCB                            |
| `jlc-bom.csv`, `jlc-cpl.csv`   | BOM and placement for JLCPCB assembly                                  |
| `mosaic-carrier-schematic.pdf` | The schematic                                                          |
| `mosaic-carrier-assembly.html` | Where each part is and what it is, for probing and rework              |
| `mosaic-carrier.step`          | 3D model of the assembled board, for enclosure CAD; built locally only |

CI's **PCB** job runs the same export and uploads these files as an artifact, except the STEP model: CI's KiCad image has no component 3D models, so it skips it.

## Ordering

1. Upload the gerber zip with the default options (2 layers, 1.6 mm) and choose a **black** solder mask with white silkscreen.
2. Turn on assembly and upload `jlc-bom.csv` and `jlc-cpl.csv`. Every part has an LCSC number. The SMD parts and JST connectors are on the top; J1 is a through-hole part on the bottom, so pick the option that assembles both sides if JLCPCB asks.
3. In the placement preview, check each part's rotation, especially U1/U2 (pin 1 dot) and the JST connectors (opening toward the board edge). Rotate any that JLCPCB shows wrongly.
4. Check J1 sits on the bottom with its pin 1 on the square pad, beside the `1` on the top. KiCad places it by pin 1 rather than its centre, so JLCPCB may need to nudge it into line with the 20 holes.
