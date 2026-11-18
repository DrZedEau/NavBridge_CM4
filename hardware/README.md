# Hardware

KiCad project for the NavBridge CM4 board.

| Path | Content |
|---|---|
| `NavBridge_CM4/` | Revision 0.2 (current): schematics, PCB layout, project-local symbol, footprint and 3D model libraries |
| `NavBridge_CM4/production/` | JLCPCB fabrication files for revision 0.2 (Gerber archive, BOM, pick-and-place), made with the Fabrication Toolkit plugin |
| `NavBridge_CM4_0.1.zip` | Revision 0.1 (archive) |

The project was made with **KiCad 10**. Open `NavBridge_CM4/NavBridge_CM4.kicad_pro`.

The project-local libraries only include the 3D models used on the board. The other models come from the standard KiCad library (`KICAD10_3DMODEL_DIR`, and `KICAD9_3DMODEL_DIR` for a few footprints).

## Board

- 100 × 100 mm, 4 copper layers, 1.6 mm
- 4 × M2.5 mounting holes at the corners (3 mm from each edge)
- 4 × M2.5 mounting holes for the Compute Module
- GND stitching vias around the board edge and across the ground pours
- The battery fuse holder (FH1) takes a Littelfuse 0451003.MRL (3 A) fuse, fitted by hand after assembly

## Schematic sheets

| Sheet | Content |
|---|---|
| NavBridge_CM4 | CM4 / CM5 connectors, car connectors, audio connectors, LEDs, mounting holes |
| Power | Battery input, LMR14030 5 V buck, USB-C connector, TPS2117 power mux, TLV75533 3.3 V LDO |
| MCU_Lin | STM32C071 MCU, two ATA663231 LIN transceivers (car I-Bus and navigation I-Bus), TPS2117 MCU power mux, SWD connector |
| Analog_RGB | ADV7125 video DAC, TMUX1574 RGB switch, 75 Ω termination |
| USB | USB2514B hub, TS3USB221 USB switch |
| Audio | PCM2912A USB audio codec and its power switch |
| M.2 | Two M.2 Key E slots, AP63203 3.3 V buck, auxiliary 5 V connector |
| Misc | Apple MFi authentication coprocessor, CSI camera connector |
| SD_Card | Empty: the SD card slot was removed in revision 0.2 |

## Known issues

Found while testing revision 0.2.

### Navigation-side I-Bus has no master termination

The board splits the I-Bus in two: the car side (U8, CN4 pin 5) keeps the car's own termination, but the navigation side (U10 pin 6 to CN3 pin 8) only has U10's internal 30 kΩ slave pull-up. That cannot drive the navigation module's input at 9600 baud: in the car, every byte sent to it came back distorted, while with CN3 unplugged the same frames went through cleanly.

- **Bodge on revision 0.2:** a 1 kΩ resistor (1/4 W) in series with a 1N4148 diode between CN3 pin 7 (+BATT, diode anode) and CN3 pin 8 (navigation I-Bus, resistor end), on the back of the connector. This is the standard LIN master termination; it draws at most 12 mA while the bus is low.
- **Next revision:** the same 1 kΩ + diode from VS to IBUS_NAV, next to U10. The car side needs nothing.
  - The ATA663231 datasheet (DS20006075E, Figure 1-14 "Typical Application Circuit SBC") shows exactly this as the "Commander node pull up": R2 = 1 kΩ in series with diode D2, from VBAT to LIN. It gives no package or power rating. With the bus held low the resistor takes about 0.2 W at 14 V, and up to about 0.75 W at 28 V (the datasheet's 2-minute VS rating), so use 1206 (0.25 W) at least, or 2010/2512 for margin, preferably AEC-Q200 anti-surge. For the diode, any small-signal diode rated above the 40 V LIN/VS maximum (BAS16, 1N4148W).
  - The same figure also has C2 = 220 pF from LIN to GND. Revision 0.2 has no capacitor on either LIN line (IBUS_CAR, IBUS_NAV): add 220 pF on each, next to U8 and U10.

The firmware's `STATUS` line shows the effect (`ibus2` `tx` counting up, `col` and `err` staying put), see [firmware/README.md](../firmware/README.md#usb).

### The MCU's power mux can be captured by a back-powered LIN regulator

U11 (TPS2117) has `PR1` tied to `VIN1` = `+3V3_LIN_REG`, so it switches the MCU to the car-side ATA663231's regulator as soon as that rail rises above about 1 V. Without 12 V (USB-C only), anything that drives or pulls up U8's `EN`, `TXD` or `RXD` feeds that rail through U8's protection diodes, and the MCU browns out in a loop. The firmware works around it (LIN pins floating until U8 is known to be powered). For the next revision, a divider on `PR1` that only selects `VIN1` above about 3 V would remove the trap.

### Other notes for the next revision

- **Pi access to the MCU's SWD:** routing SWDIO, SWCLK and NRST to spare CM GPIOs (through a buffer or switch powered from the CM 3.3 V, so the MCU's SWD pull-ups cannot feed the CM while it is off) would let the Pi program and recover the MCU with OpenOCD, without a debugger.
- **PA10 (RGB select) is also USART1 RX** for ST's ROM bootloader, and its 10 kΩ pull-down holds it low. It only matters if ST's ROM bootloader is to be used on a programmed chip; the NavBridge bootloader does not need it.

## License

CERN-OHL-S-2.0, see [LICENSE](../LICENSE).
