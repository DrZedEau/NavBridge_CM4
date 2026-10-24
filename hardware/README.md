# Hardware

KiCad project for the NavBridge CM4 board.

| File | Content |
|---|---|
| `NavBridge_CM4_0.1.zip` | Revision 0.1: schematics, PCB layout, project-local symbol, footprint and 3D model libraries |

The project was made with **KiCad 10**. Unzip the archive and open `NavBridge_CM4.kicad_pro`.

## Board

- 100 × 100 mm, 4 copper layers, 1.6 mm
- 4 × M2.5 mounting holes at the corners (3 mm from each edge)
- 4 × M2.5 mounting holes for the Compute Module

## Schematic sheets

| Sheet | Content |
|---|---|
| NavBridge_CM4 | CM4 / CM5 connectors, car connectors, audio connectors, LEDs, mounting holes |
| Power | Battery input, LMR14030 5 V buck, USB-C connector, TPS2117 power mux, 3.3 V LDO |
| MCU_Lin | STM32C071 MCU, ATA663231 LIN transceiver (I-Bus), TPS2116 MCU power mux |
| Analog_RGsB | ADV7125 video DAC, TMUX1574 RGB switch, 75 Ω termination |
| USB | USB2514B hub, TS3USB221 USB switch |
| Audio | PCM2912A USB audio codec and its power switch |
| M.2 | Two M.2 Key E slots, AP63203 3.3 V buck, auxiliary 5 V connector |
| SD_Card | microSD slot and its power switch |
| MFi | Apple MFi authentication coprocessor |

The archive also contains sheet files that are not used by the root schematic (`Buck_Converter_5V`, `GNSS`, `IBUS`, `PCIe`).

## License

CERN-OHL-S-2.0, see [LICENSE](../LICENSE).
