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

[setup.sh](setup.sh) prepares Raspberry Pi OS on the Compute Module (CM4 or CM5) for the NavBridge CM4 board. On the Pi, it runs straight from the `main` branch:

```bash
curl -fsSL https://raw.githubusercontent.com/DrZedEau/NavBridge_CM4/main/scripts/setup.sh | sudo bash
```

Or, from a clone of this repository: `sudo bash scripts/setup.sh`. Reboot afterwards, so that the new `config.txt` applies.

The script only starts once it has been downloaded completely (`main` is called on its last line), and it can be run again: the steps that are already done are skipped or harmless. See [Set up the Pi and flash the MCU](../documentation/SETUP.md) for where it fits in the install.

### What it does

- Boot time: disables `NetworkManager-wait-online` and removes unused packages (`cloud-init`)
- Updates the system and installs `git`, `vim`, `dfu-util` (to update the STM32 firmware) and `rfkill`
- Writes `config.txt` (the previous one is kept as a backup): 24-bit DPI video output for the ADV7125 video DAC with the timings of the BMW display (400 × 240), USB host mode for the on-board hub, and I²C for the MFi authentication coprocessor (SDA0/SCL0, shared with the camera)
- Loads the `i2c-dev` kernel module now and at every boot, so that the MFi coprocessor's bus shows up as `/dev/i2c-10`
- Makes sure Bluetooth is not blocked by `rfkill`

The script leaves `cmdline.txt` as it is. The boot splash screen comes with the NavBridge App (its `install_splash.sh`, see [Install the NavBridge App](../documentation/APP.md)).

The bootloader EEPROM setting a CM4 needs (`POWER_OFF_ON_HALT=1`, `WAKE_ON_GPIO=0`) is not set by the script: Compute Modules ship with `rpi-eeprom-update` disabled, so it is written over USB while flashing, see [Flash the Compute Module](../documentation/FLASH.md#3-cm4-only-set-the-bootloader-to-turn-the-33-v-rail-off-at-shutdown). The NavBridge App's default configuration already uses the MCU's I-Bus port (`/dev/serial/by-id/usb-NavBridge_NavBridge_CM4_MCU-if00`).

### Still to add

- The PCM2912A USB audio codec (output volume at boot)
- The CSI camera on CAM1

The previous board's script is `software/scripts/setup.sh` in the [CM5 NavBridge](https://github.com/DrZedEau/CM5_NavBridge) repository. Unlike that board, the NavBridge CM4 generates the composite sync with the STM32, so the `dpi_csync` PIO service (CM5 only) is not needed.

## License

GPL-2.0, see [LICENSE](LICENSE).
