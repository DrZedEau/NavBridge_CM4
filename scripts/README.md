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

Or, from a clone of this repository: `sudo bash scripts/setup.sh`. Reboot afterwards, so that the new `config.txt`, `cmdline.txt` and splash screen apply.

The script only starts once it has been downloaded completely (`main` is called on its last line), and it can be run again: the steps that are already done are skipped or harmless.

### What it does

- Boot time: disables `NetworkManager-wait-online` and removes unused packages (`cloud-init`)
- Updates the system and installs the required packages, including `dfu-util` to update the STM32 firmware
- Writes `config.txt` (the previous one is kept as a backup): 24-bit DPI video output for the ADV7125 video DAC with the timings of the BMW display (400 × 240), USB host mode for the on-board hub, and I²C for the MFi authentication coprocessor (SDA0/SCL0, shared with the camera)
- Writes `cmdline.txt` (the previous one is kept as a backup) for a quiet boot: the console moves to `tty3`, and the kernel messages, the cursor and the systemd status lines no longer show on the display, with the splash screen instead of the Raspberry Pi logos. Only its `root=` parameter is taken from the previous file, because it differs on each install; any other parameter it had, such as the Wi-Fi country (`cfg80211.ieee80211_regdom`), is dropped
- Installs the [splash screen](#splash-screen)
- Loads the `i2c-dev` kernel module now and at every boot, so that the MFi coprocessor's bus shows up as `/dev/i2c-10`
- Makes sure Bluetooth is not blocked by `rfkill`

### Splash screen

[splash/splash.png](splash/splash.png) shows on the display from the start of the kernel until the app draws over it. It goes through Raspberry Pi's [rpi-splash-screen-support](https://github.com/raspberrypi/rpi-splash-screen-support): `configure-splash` puts it in the initramfs as `/lib/firmware/logo.tga`. It runs with `--no-cmdline`, because it would remove `quiet`: the `cmdline.txt` step adds the splash parameters itself.

The kernel reads [splash/splash.tga](splash/splash.tga): an uncompressed 24-bit TGA, 400 × 240 (the display, so it fills it without cropping) and at most 224 colours. To change the picture, replace `splash.png`, then:

```bash
python3 scripts/splash/make-splash.py scripts/splash/splash.png scripts/splash/splash.tga
```

[make-splash.py](splash/make-splash.py) only needs Python. It stretches the picture to 400 × 240 (a 16:9 picture then fills the 16:9 screen with its proportions) and reduces it to 224 colours, keeping black pure black. Run through `curl`, the setup script downloads `splash.tga` from the `main` branch; from a clone, it uses the one next to it.

- `logo.nologo` hides the splash screen too: `cmdline.txt` must not have it.
- On a quiet boot (`quiet`, `loglevel=0`), the splash screen needs the 6.18 kernel or later: the 6.12 kernel hides it. Check with `uname -r`.

### Still to add

- The PCM2912A USB audio codec (output volume at boot)
- The CSI camera on CAM1
- The bootloader EEPROM: `POWER_OFF_ON_HALT=1` and `WAKE_ON_GPIO=0`, so that the CM 3.3 V rail goes off at shutdown and the MCU can turn the 5 V buck off
- The NavBridge App's I-Bus port: `/dev/serial/by-id/usb-NavBridge_NavBridge_CM4_MCU-if00`

The previous board's script is `software/scripts/setup.sh` in the [CM5 NavBridge](https://github.com/DrZedEau/CM5_NavBridge) repository. Unlike that board, the NavBridge CM4 generates the composite sync with the STM32, so the `dpi_csync` PIO service (CM5 only) is not needed.

## License

GPL-2.0, see [LICENSE](LICENSE).
