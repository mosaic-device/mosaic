# Peripheral carrier PCB

A 30 × 70 mm, two-layer board that plugs onto pins 1–20 of the Waveshare ESP32-P4-WIFI6-Touch-LCD-5's 40-pin header and gives each peripheral its own latching JST-XH connector. It replaces the hand-wired perfboard prototype. The pin map comes from the [hardware wiki page](https://github.com/mosaic-device/mosaic/wiki/Hardware-Wiring-and-Power); the firmware needs no change.

The KiCad 10 project is in [`mosaic-carrier/`](mosaic-carrier/). Open `mosaic-carrier.kicad_pro` in KiCad to view or edit the schematic and layout.

## Fitting it

- **J1** is a male 2 × 10 header soldered on the **underside**, plastic against the board. It plugs into the Waveshare's female socket at pins 1–20.
- Pin 1 (square pad, marked `PIN 1` on the underside) goes at the end of the Waveshare header nearest the corner mounting hole, with the 5V row nearest the board edge. The carrier then lies over the Waveshare board, running from the corner end toward its middle.
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

- **LED data** goes through a 74AHCT1G125 buffer powered from the LED 5V, so the LEDs get a full 5V signal. Solder jumpers JP1 (ring) and JP2 (strip) choose the path: **1–2 (default, bridged) = through the buffer; cut 1–2 and bridge 2–3 = straight from the GPIO**, as on the prototype.
- **330 Ω** in each data line, at the board. It limits current into the first LED if its supply is missing and damps ringing on the cable; at 800 kHz it costs nothing in signal timing.
- **10 kΩ pull-downs** (R1, R2) hold the data lines low while the P4 boots, so the LEDs don't latch random colors.
- **F1**, a 1.1 A resettable fuse, protects the header's 5V from a shorted LED cable. Firmware caps LED brightness at 30%, about 0.36 A for 8 + 12 LEDs.
- **C1**, 220 µF, sits across the LED supply. A larger capacitor would only draw a bigger surge from USB at plug-in.
- **R5, R6** are I2C pull-up footprints left empty: the GY-302 has its own. Fit 4.7 kΩ if a sensor without pull-ups is used.
- The encoder needs no pull-ups on the board: the KY-040 has 10 kΩ on CLK, DT and SW.

## Check and export

```sh
pnpm pcb:check
pnpm pcb:export
```

`pcb:check` runs ERC and DRC (including schematic parity) and fails on any violation. `pcb:export` then writes `hardware/pcb/build/`, which is not committed:

| File                           | Use                                         |
| ------------------------------ | ------------------------------------------- |
| `mosaic-carrier-gerbers.zip`   | Gerbers and drill files to upload to JLCPCB |
| `jlc-bom.csv`, `jlc-cpl.csv`   | BOM and placement for JLCPCB assembly       |
| `mosaic-carrier-schematic.pdf` | The schematic                               |

CI's **PCB** job runs the same export and uploads these files as an artifact.

## Ordering

1. Upload the gerber zip with the default options: 2 layers, 1.6 mm, any color.
2. Turn on assembly for the top side and upload `jlc-bom.csv` and `jlc-cpl.csv`. Every part has an LCSC number.
3. In the placement preview, check each part's rotation, especially U1/U2 (pin 1 dot), C1 (polarity) and the JST connectors (opening toward the board edge). Rotate any that JLCPCB shows wrongly.
4. J1 is left out of the assembly files: solder the header yourself on the underside.
