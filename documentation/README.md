# Documentation

How to build, set up and install the NavBridge CM4. Steps 2 to 4 are done on the bench, with the board powered from USB-C (J1), before it goes into the car.

1. [Make the navigation cable](NAV_CABLE.md)
2. [Flash the Compute Module](FLASH.md): Raspberry Pi OS on the eMMC, and the CM4 bootloader setting
3. [Set up the Pi and flash the MCU](SETUP.md)
4. [Install the NavBridge App](APP.md)
5. [Install in the car](NAV_CABLE.md#install-in-the-car)

## What you need

| Part | Notes |
|---|---|
| The assembled NavBridge CM4 board | With the 3 A fuse (Littelfuse 0451003.MRL) fitted in FH1 by hand |
| A Raspberry Pi CM4 or CM5 | With eMMC and wireless, see [Compute Module](#compute-module) |
| A heatsink | Needed on a CM5, nice to have on a CM4 |
| The navigation cable | DIY, see [Make the navigation cable](NAV_CABLE.md) |
| The 3D printed connector housing and case | Not published yet for this board, see [3D/README.md](../3D/README.md) |
| An audio cable | From the line output (U1) to the car's AUX or CD changer input |
| A microphone (optional) | Electret, on U2, for calls and the voice assistant |
| A USB-C data cable and a 5 V / 3 A USB-C charger | For flashing and the bench setup |

## Compute Module

The board works with the **Raspberry Pi CM4** and **CM5** only. The Compute Module must have:

- **eMMC**: the board has no SD card slot, so Lite modules (without eMMC) are not supported
- **Wireless** (Wi-Fi and Bluetooth)

> [!WARNING]
> **On a CM4, set the bootloader to turn the 3.3 V rail off at shutdown.** By default the CM4 keeps its 3.3 V rail up after `shutdown`. The MCU waits for that rail to go away before it turns the 5 V buck off, so the board would keep drawing from the car battery until it is disconnected. The setting (`POWER_OFF_ON_HALT=1`, `WAKE_ON_GPIO=0`) is written over USB while flashing, see [Flash the Compute Module](FLASH.md#3-cm4-only-set-the-bootloader-to-turn-the-33-v-rail-off-at-shutdown). The CM5 already turns its 3.3 V rail off at shutdown.

The CM5 uses the same connectors as the CM4 and keeps the CM4 pinout for the interfaces this board uses (DPI, USB 2.0, PCIe, CSI camera, I²C, `nRPIBOOT`, LEDs). From testing:

- The CM5 boots faster than the CM4.
- The CM5 needs a heatsink: without one it gets too hot. The CM4 runs fine without a heatsink, but one is still nice to have.
- A CM5 under heavy load, together with power-hungry M.2 or USB devices, can get close to the 3.5 A of the board's 5 V buck.
