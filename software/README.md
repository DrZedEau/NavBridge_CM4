# Raspberry Pi Setup Script

Setup script that prepares Raspberry Pi OS on the Compute Module for the NavBridge CM4 board. *Not added yet.*

## What it has to configure

- 24-bit DPI video output for the ADV7125 video DAC, with the timings of the BMW display
- The PCM2912A USB audio codec (output volume at boot)
- I²C for the MFi authentication coprocessor
- Tools to update the STM32 firmware over USB DFU

The previous board's script is `software/scripts/setup.sh` in the [CM5 NavBridge](https://github.com/DrZedEau/CM5_NavBridge) repository. Unlike that board, the NavBridge CM4 generates the composite sync with the STM32, so the `dpi_csync` PIO service (CM5 only) is not needed.

## License

GPL-2.0, see [LICENSES/GPL-2.0-only.txt](../LICENSES/GPL-2.0-only.txt).
