# Set Up the Pi and Flash the MCU

Do this on the bench, with the board powered from USB-C (J1) and **nRPIBOOT** (SW1) OFF: the MCU can only be reflashed on USB-C power. Connect to the Pi over SSH (see [Flash the Compute Module](FLASH.md)).

## 1. Run the setup script

```bash
curl -fsSL https://raw.githubusercontent.com/DrZedEau/NavBridge_CM4/main/scripts/setup.sh | sudo bash
sudo reboot
```

The script:

- Removes unused packages (`cloud-init`) and disables `NetworkManager-wait-online`, for a faster boot
- Updates the system and installs `git`, `vim`, `dfu-util` (for the MCU) and `rfkill`
- Writes `config.txt` (the previous one is kept as a backup): 24-bit DPI output for the video DAC with the BMW display's timings (400 × 240), USB host mode for the on-board hub, and I²C for the MFi coprocessor
- Loads the `i2c-dev` kernel module now and at every boot
- Makes sure Bluetooth is not blocked by `rfkill`

It can be run again: the steps that are already done are skipped or harmless. See [scripts/README.md](../scripts/README.md#setup-script) for the details.

## 2. Flash the MCU

The STM32 is flashed from the Pi over the board's USB hub, with the images in [firmware/bin/](../firmware/bin/). Get this repository on the Pi, then run the flash script:

```bash
git clone https://github.com/DrZedEau/NavBridge_CM4.git
cd NavBridge_CM4
bash scripts/mcu-flash.sh
```

The script asks for your password (`dfu-util` runs through `sudo`) and does what the MCU needs:

- **Blank MCU**, straight from the factory: it installs the NavBridge bootloader, then the firmware.
- **Programmed MCU**: it hands the running firmware over to the bootloader and writes the new firmware, in about 4 s.

It then waits for the firmware to come back on USB and prints its version.

If the update was interrupted, run the script again: the MCU stays in its bootloader until a complete firmware is written. If the MCU does not answer at all, hold **BOOT** (SW3), press and release **RESET** (SW2), release BOOT, and run the script again.

For a later update:

```bash
cd ~/NavBridge_CM4
git pull
bash scripts/mcu-flash.sh
```

See [firmware/README.md](../firmware/README.md#bootloader-and-updates) for the bootloader, the `STATUS` line and SWD.

Next: [Install the NavBridge App](APP.md).
