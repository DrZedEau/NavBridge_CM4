# Hardware

KiCad project for the NavBridge CM4 board.

| Path | Content |
|---|---|
| `NavBridge_CM4/` | Revision 0.3 (current): schematics, PCB layout, project-local symbol, footprint and 3D model libraries |
| `NavBridge_CM4/production/` | JLCPCB fabrication files (Gerber archive, BOM, pick-and-place), made with the Fabrication Toolkit plugin |

Earlier revisions (0.1 and 0.2) are in the git history.

The project was made with **KiCad 10**. Open `NavBridge_CM4/NavBridge_CM4.kicad_pro`.

The project-local libraries only include the 3D models used on the board. The other models come from the standard KiCad library (`KICAD10_3DMODEL_DIR`, and `KICAD9_3DMODEL_DIR` for a few footprints).

## Board

- 100 × 100 mm, 4 copper layers, 1.6 mm
- 4 × M2.5 mounting holes at the corners (3 mm from each edge)
- 4 × M2.5 mounting holes for the Compute Module
- GND stitching vias around the board edge and across the ground pours
- Teardrops on vias and through-hole pads
- The battery fuse holder (FH1) takes a Littelfuse 0451003.MRL (3 A) fuse, fitted by hand after assembly

## Schematic sheets

| Sheet | Content |
|---|---|
| NavBridge_CM4 | CM4 / CM5 connectors, car connectors, audio connectors, LEDs, nRPIBOOT switch, mounting holes |
| Power | Battery input, LMR14030 5 V buck, USB-C connector (J1), TPS2117 power mux, TPS54202 3.3 V buck for the USB hub, the USB switches and the video DAC |
| MCU_Lin | STM32C071 MCU, two ATA663231 LIN transceivers (car I-Bus and navigation I-Bus), navigation I-Bus master termination, TPS2117 MCU power mux, SWD connector |
| Analog_RGB | ADV7125 video DAC and its supply filter, TMUX1574 RGB switch, 75 Ω termination |
| USB | USB2514B hub, TS3USB221 USB switch for rpiboot |
| USB-DFP | USB-C host port (J2), TS3USB221 switch between M.2 slot CN6 and J2 (set by SW4), TPS2051B 500 mA VBUS switch |
| Audio | PCM2912A USB audio codec and its power switch |
| M.2 | Two M.2 Key E slots, TPS54202 3.3 V buck |
| Misc | Apple MFi authentication coprocessor, CSI camera connector |
| SD_Card | Empty: the SD card slot was removed in revision 0.2 |

## Revision history

### 0.3 (current)

Not built yet. Changes from revision 0.2:

- **USB-C host port (J2)**, on the new USB-DFP sheet. A TS3USB221 (U14), set by the CN6 / USB-C slide switch (SW4), connects hub port 2 either to M.2 slot CN6 or to J2. VBUS comes from a TPS2051B (U15, 500 mA, on while the Compute Module is on), and 56 kΩ pull-ups on CC1/CC2 (R32, R33) advertise default USB power. The port is meant for low-power devices, such as an analog video converter.
- **3.3 V for the USB hub, the USB switches and the video DAC** now comes from a TPS54202 buck (U5) instead of the TLV75533 LDO.
- **M.2 3.3 V buck:** TPS54202 (U18) instead of the AP63203.
- **Video DAC supply:** an extra 22 µF after the ferrite bead, which lowers and damps the resonance of the bead and capacitor filter.
- **Navigation I-Bus master termination** on the board: D7 (1N4148W) and R24 (1 kΩ, 1206) from +BATT to IBUS_NAV, next to U10. This replaces the revision 0.2 bodge (see below).
- **MCU power mux (U11):** R25 (150 kΩ) and R26 (100 kΩ) divide `+3V3_LIN_REG` onto `PR1`, so U11 only selects the LIN regulator above about 2.5 V (see below).
- **Main power mux (U4):** R10 (330 kΩ) and R11 (100 kΩ) divide `+5V_BUCK` onto `PR1`, so U4 only switches to the car supply once the buck output is above about 4.3 V. With `PR1` tied straight to `VIN1`, as on revision 0.2, and USB-C and the car supply both connected, U4 would switch to the buck at about 1 V while it starts up, and stay on it while it decays at turn-off.
- **BOOT button (SW3):** now pulls BOOT0 up from the CM 3.3 V rail (`+3V3`) instead of the MCU rail (`+3V3_MUX`). The MCU can only be reflashed with the board powered from USB-C (J1).
- The auxiliary 5 V connector (CN8) was removed.
- More GND stitching vias, and teardrops on vias and through-hole pads.

### 0.2

First CM4 revision. The prototypes work in the car with the I-Bus bodge below.

## Known issues on revision 0.2

Both are fixed in revision 0.3.

### Navigation-side I-Bus has no master termination

The board splits the I-Bus in two: the car side (U8, CN4 pin 5) keeps the car's own termination, but the navigation side (U10 pin 6 to CN3 pin 8) only has U10's internal 30 kΩ slave pull-up. That cannot drive the navigation module's input at 9600 baud: in the car, every byte sent to it came back distorted, while with CN3 unplugged the same frames went through cleanly.

- **Bodge on revision 0.2:** a 1 kΩ resistor (1/4 W) in series with a 1N4148 diode between CN3 pin 7 (+BATT, diode anode) and CN3 pin 8 (navigation I-Bus, resistor end), on the back of the connector. This is the standard LIN master termination; it draws at most 12 mA while the bus is low.
- **Revision 0.3:** the same 1 kΩ + diode (R24, D7) from +BATT to IBUS_NAV, next to U10. The car side needs nothing.
  - The ATA663231 datasheet (DS20006075E, Figure 1-14 "Typical Application Circuit SBC") shows exactly this as the "Commander node pull up": R2 = 1 kΩ in series with diode D2, from VBAT to LIN. It gives no package or power rating. With the bus held low the resistor takes about 0.2 W at 14 V, and up to about 0.75 W at 28 V (the datasheet's 2-minute VS rating), hence the 1206 (0.25 W) resistor. The diode must be rated above the 40 V LIN/VS maximum (BAS16, 1N4148W).
  - The same figure also has C2 = 220 pF from LIN to GND. Neither revision has it on IBUS_CAR or IBUS_NAV: the I-Bus works without it, and it would only add EMC filtering.

The firmware's `STATUS` line shows the effect (`ibus2` `tx` counting up, `col` and `err` staying put), see [firmware/README.md](../firmware/README.md#usb).

### The MCU's power mux can be captured by a back-powered LIN regulator

On revision 0.2, U11 (TPS2117) has `PR1` tied to `VIN1` = `+3V3_LIN_REG`, so it switches the MCU to the car-side ATA663231's regulator as soon as that rail rises above about 1 V. Without 12 V (USB-C only), anything that drives or pulls up U8's `EN`, `TXD` or `RXD` feeds that rail through U8's protection diodes, and the MCU browns out in a loop. The firmware works around it (LIN pins floating until U8 is known to be powered).

Revision 0.3 adds a divider on `PR1` (R25, R26), so U11 only selects `VIN1` above about 2.5 V. The back-fed rail measured about 1.5 V on revision 0.2.

## Other notes for the next revision

- **Pi access to the MCU's SWD:** routing SWDIO, SWCLK and NRST to spare CM GPIOs (through a buffer or switch powered from the CM 3.3 V, so the MCU's SWD pull-ups cannot feed the CM while it is off) would let the Pi program and recover the MCU with OpenOCD, without a debugger.
- **PA10 (RGB select) is also USART1 RX** for ST's ROM bootloader, and its 10 kΩ pull-down holds it low. It only matters if ST's ROM bootloader is to be used on a programmed chip; the NavBridge bootloader does not need it.

## License

CERN-OHL-S-2.0, see [LICENSE](../LICENSE).
