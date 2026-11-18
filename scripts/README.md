# Raspberry Pi Scripts

## MCU flashing

[mcu-flash.sh](mcu-flash.sh) flashes the STM32 from the Pi over USB, with the images in [firmware/bin/](../firmware/bin/). Once, on the Pi:

```bash
sudo apt install dfu-util
```

Then, from this repository:

```bash
bash scripts/mcu-flash.sh
```

- **Blank MCU, straight from the factory** (ST's ROM bootloader, `0483:df11`): it installs the NavBridge bootloader, then the firmware.
- **Programmed MCU**: it hands the running firmware over to the NavBridge bootloader (`0483:5741`) and writes the new firmware, in about 4 s. An interrupted update leaves the MCU in its bootloader: run the script again.
- `bash scripts/mcu-flash.sh --bootloader-update` also replaces the NavBridge bootloader itself (board on USB-C).

`dfu-util` runs through `sudo` when the USB device is not writable by the current user (the usual case), so the script asks for the password. See the [firmware README](../firmware/README.md#bootloader-and-updates) for details.

## Setup script

Setup script that prepares Raspberry Pi OS on the Compute Module for the NavBridge CM4 board. *Not added yet.*

### What it has to configure

- 24-bit DPI video output for the ADV7125 video DAC, with the timings of the BMW display
- USB host mode for the on-board hub (`otg_mode=1` in `config.txt`, the default in recent Raspberry Pi OS)
- The PCM2912A USB audio codec (output volume at boot)
- I²C for the MFi authentication coprocessor (SDA0/SCL0, shared with the camera)
- The CSI camera on CAM1
- `dfu-util`, to update the STM32 firmware
- The bootloader EEPROM: `POWER_OFF_ON_HALT=1` and `WAKE_ON_GPIO=0`, so that the CM 3.3 V rail goes off at shutdown and the MCU can turn the 5 V buck off
- The NavBridge App's I-Bus port: `/dev/serial/by-id/usb-NavBridge_NavBridge_CM4_MCU-if00`

The previous board's script is `software/scripts/setup.sh` in the [CM5 NavBridge](https://github.com/DrZedEau/CM5_NavBridge) repository. Unlike that board, the NavBridge CM4 generates the composite sync with the STM32, so the `dpi_csync` PIO service (CM5 only) is not needed.

## License

GPL-2.0, see [LICENSE](LICENSE).
