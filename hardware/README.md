# Hardware

KiCad project for the NavBridge CM4 board. It was made with **KiCad 10**: open [`NavBridge_CM4/NavBridge_CM4.kicad_pro`](NavBridge_CM4/).

| Path | Content |
|---|---|
| `NavBridge_CM4/*.kicad_sch` | Schematic: root sheet and 8 sub-sheets |
| `NavBridge_CM4/NavBridge_CM4.kicad_pcb` | PCB layout |
| `NavBridge_CM4/lib/symbols/` | Project symbol libraries |
| `NavBridge_CM4/lib/footprints/` | Project footprint libraries |
| `NavBridge_CM4/lib/3dmodels/` | 3D models for the project footprints |
| `NavBridge_CM4/fabrication-toolkit-options.json` | Settings for the JLCPCB Fabrication Toolkit plugin |

The project libraries only hold the parts used on the board. Standard parts come from KiCad's own libraries.

The CM4 / CM5 connector symbols (CN1, CN2) come from the `PCM_JLCPCB_Connectors` library, which is installed through KiCad's Plugin and Content Manager. The schematic carries its own copy of these symbols, so the project opens without it.

When you import new parts with the LCSC or JLCPCB import plugins, point their output to `lib/`, or move the new files there and update `sym-lib-table` / `fp-lib-table`. Otherwise they create their own `libs/` or `library/` folder again.

## Revisions and fabrication files

Each board revision is a git tag (`v0.1`, ...). Gerbers, drill files, BOM, CPL, the schematic PDF and the STEP model for a revision are attached to the matching [GitHub release](https://github.com/DrZedEau/NavBridge_CM4/releases). The `production/` folder that the fabrication plugin writes is not tracked.

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

## License

CERN-OHL-S-2.0, see [LICENSE](../LICENSE).
