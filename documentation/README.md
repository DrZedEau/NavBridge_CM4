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
| A Raspberry Pi CM4 or CM5 | With eMMC and wireless, see [Compute Module Compatibility](../README.md#compute-module-compatibility) |
| A heatsink | Needed on a CM5, nice to have on a CM4 |
| The navigation cable | DIY, see [Make the navigation cable](NAV_CABLE.md) |
| The 3D printed connector housing and case | Not published yet for this board, see [3D/README.md](../3D/README.md) |
| An audio cable | From the line output (U1) to the car's AUX or CD changer input |
| A microphone (optional) | Electret, on U2, for calls and the voice assistant |
| A USB-C data cable and a 5 V / 3 A USB-C charger | For flashing and the bench setup |
