# NavBridge CM4

A Raspberry Pi Compute Module carrier board for BMW E-Series cars with the factory navigation system (E46, E39, E85 and similar).

The board plugs in between the car harness and the BMW MK3/MK4 navigation module. It brings Android Auto and Apple CarPlay to the factory display, buttons and audio, keeps the factory navigation, and needs no change to the car's wiring. It is made for [NavBridge App](https://github.com/DrZedEau/NavBridge_App), and also runs [LIVI](https://github.com/f-io/LIVI).

<p align="center">
  <img src="images/PCB/navbridge_cm4_pcb_3d.png" width="800">
</p>

## Features

- **Video:** the Pi's picture on the factory display (analog RGB with sync on green), switched with the navigation module's picture from the factory buttons
- **Audio:** USB audio codec, with a stereo line output to the car's AUX or CD changer input and a microphone input
- **I-Bus:** an STM32 between the car and the navigation module turns the Pi on and off with the car, passes the factory buttons to the app and emulates a CD changer. It stays powered from the battery while the Pi is off.
- **Expansion:** two M.2 Key E slots (PCIe + USB, and USB), a USB-C host port for low-power devices (500 mA) and a CSI camera connector
- **CarPlay:** Apple MFi authentication coprocessor
- **Bench use:** a USB-C port to flash the Compute Module and power the board without the car. The MCU is updated from the Pi over USB.
- **Power:** automotive 5 V / 3.5 A buck converter on the fused, TVS-protected battery supply
- 100 × 100 mm, 4-layer PCB

Parts, connectors, pinouts, power and the revision history: [hardware/README.md](hardware/README.md).

## Compute Module

Raspberry Pi **CM4** or **CM5**, with **eMMC** (the board has no SD card slot) and **wireless**. The CM5 boots faster but needs a heatsink.

> [!WARNING]
> A CM4 needs a bootloader setting to turn its 3.3 V rail off at shutdown, otherwise the board keeps drawing from the car battery. See [Compute Module](documentation/README.md#compute-module).

## Documentation

| | |
|---|---|
| [Build and install guides](documentation/README.md) | [Make the navigation cable](documentation/NAV_CABLE.md), [flash the Compute Module](documentation/FLASH.md), [set up the Pi and flash the MCU](documentation/SETUP.md), [install the app](documentation/APP.md), [install in the car](documentation/NAV_CABLE.md#install-in-the-car) |
| [hardware/](hardware/README.md) | KiCad 10 project (revision 0.3), board details, revision history |
| [firmware/](firmware/README.md) | STM32C071 firmware: I-Bus, Pi power, video source, CSYNC, USB bootloader |
| [scripts/](scripts/README.md) | Raspberry Pi setup script and MCU flash script |
| [3D/](3D/README.md) | Printed case and connector housing (not added yet) |
| [images/](images/) | Pictures and renders |

## Project Status

Revision 0.3 adds the USB-C host port and fixes the issues found on the revision 0.2 prototypes, which work in the car with a small I-Bus fix (see [Revision history](hardware/README.md#revision-history)).

- [x] Schematic and PCB layout (revision 0.3)
- [x] Revision 0.2 prototypes built and tested in the car, with a CM4 and a CM5
- [ ] Revision 0.3 prototypes
- [ ] STM32 firmware: first version, being tested on the prototype
- [ ] Raspberry Pi setup script: first version
- [ ] 3D printed case and connector
- [ ] Testing on more BMW models

## Contributing

Bug reports, hardware improvements, firmware and documentation updates are welcome: open an issue or a pull request.

## License

Copyright 2026 DrZedEau

The hardware design — the KiCad project in `hardware/`, the 3D printed parts in `3D/` and the images in `images/` — is licensed under the **CERN Open Hardware Licence Version 2 – Strongly Reciprocal** (CERN-OHL-S-2.0), see [LICENSE](LICENSE). You may redistribute and modify it and make products from it under the terms of that licence. It is distributed without any express or implied warranty, including of merchantability, satisfactory quality and fitness for a particular purpose.

Source location: https://github.com/DrZedEau/NavBridge_CM4

As required by section 4 of the CERN-OHL-S, products made from this design must, where practicable, keep this source location visible.

The firmware in `firmware/` and the Raspberry Pi setup script in `scripts/` are licensed under the **GNU General Public License v2.0** (GPL-2.0), see [firmware/LICENSE](firmware/LICENSE) and [scripts/LICENSE](scripts/LICENSE). Third-party files keep their own licences.

## References

- CM5 NavBridge (previous board) https://github.com/DrZedEau/CM5_NavBridge
- NavBridge App https://github.com/DrZedEau/NavBridge_App
- LIVI project https://github.com/f-io/LIVI
- Raspberry Pi CM4 datasheet https://datasheets.raspberrypi.com/cm4/cm4-datasheet.pdf
- BMW Wiring Diagram System (WDS) https://bmwteka.com/wds/en/e85/e82460cf
- BMW On-Board Monitor documentation https://bmwteka.com/wds/en/e85/1b7f9876
- Community resources https://www.e46fanatics.com/threads/bmw-on-board-monitor-without-navigation-unit.1303552
