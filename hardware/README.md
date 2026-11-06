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

## License

CERN-OHL-S-2.0, see [LICENSE](../LICENSE).
