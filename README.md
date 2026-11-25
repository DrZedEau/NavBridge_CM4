# NavBridge CM4

A Raspberry Pi Compute Module 4 carrier board for BMW E-Series vehicles equipped with the OEM navigation system (E46, E39, E85, and similar platforms).

The NavBridge CM4 installs inline between the factory wiring harness and the BMW MK3/MK4 navigation module, allowing a Raspberry Pi Compute Module to integrate with the original infotainment system while preserving the OEM look and functionalities.

Its primary purpose is to provide modern features such as Android Auto and Apple CarPlay through the Raspberry Pi while continuing to use the factory display, controls, and audio system.

The board works with the **Raspberry Pi CM4** and **CM5**, with eMMC and wireless (see [Compute Module Compatibility](#compute-module-compatibility)).

<p align="center">
  <img src="images/PCB/navbridge_cm4_pcb_3d.png" width="800">
</p>

---

## Overview

The board interfaces directly with the factory navigation module and provides:

- Video output from the Raspberry Pi to the OEM BMW display (analog RGB with sync on green)
- Switching of the display between the factory navigation picture and the Raspberry Pi
- Stereo audio output for integration with the factory AUX input or CDC interface
- Microphone input
- Integration with the BMW I-Bus for power management and button control
- Two M.2 Key E expansion slots
- A USB-C host port for low-power USB devices
- A CSI camera connector
- An Apple MFi authentication coprocessor

The hardware is designed to run [NavBridge App](https://github.com/DrZedEau/NavBridge_App), a standalone Android Auto head unit for the Raspberry Pi, or the [LIVI](https://github.com/f-io/LIVI) project.

---

## Features

### Hardware

- 100 × 100 mm, 4-layer PCB
- Raspberry Pi CM4 / CM5 connectors (2 × 100-pin Hirose DF40C)
- Automotive-grade 5 V / 3.5 A buck converter (**LMR14030-Q1**), switched on and off by the MCU
- Fused battery input with a **SMCJ24A** TVS diode
- USB-C port (J1) for Raspberry Pi flashing (rpiboot) and bench power, with automatic selection between USB-C and the car supply (**TPS2117**)
- 3.3 V / 2 A buck converter (**TPS54202**) for the USB hub, the USB switches and the video DAC
- 4-port USB 2.0 hub (**USB2514B**) for the MCU, the audio codec, M.2 slot CN7, and either M.2 slot CN6 or the USB-C host port
- USB-C host port (J2), USB 2.0, 500 mA (**TPS2051B**), sharing a hub port with M.2 slot CN6 through a **TS3USB221** switch (SW4)
- 24-bit video DAC (**ADV7125**) fed by the Raspberry Pi DPI output
- Analog RGB switch (**TMUX1574**) to select the factory navigation picture or the Raspberry Pi picture
- Integrated **PCM2912A** USB audio codec
  - Stereo line output
  - Mono microphone input with bias
- Two M.2 Key E slots, powered by a dedicated 3.3 V / 2 A buck converter (**TPS54202**)
  - CN6: PCIe x1 + USB 2.0 (the USB is shared with the USB-C host port)
  - CN7: USB 2.0 only
- 22-pin CSI camera connector (CAM1, 2 lanes) on the bottom side
- Apple **MFi343S00177** authentication coprocessor on I²C
- SWD connector for the STM32

The board has no SD card slot, so it needs a Compute Module with eMMC (not a Lite module), and with wireless (see [Compute Module Compatibility](#compute-module-compatibility)).

### Vehicle Integration

- Two **ATA663231** LIN transceivers: one on the car I-Bus, one on the navigation module's I-Bus, with the MCU in between
- LIN master termination (1 kΩ + diode) on the navigation-side I-Bus, since the board is the only master on that segment
- **STM32C071** microcontroller for:
  - Raspberry Pi power management
  - OEM button translation
  - Composite sync (CSYNC) generation for the OEM display
  - Video source selection
- The MCU is powered from the car battery through the car-side LIN transceiver's built-in 3.3 V regulator, so it keeps watching the I-Bus while the Raspberry Pi is off
- No modifications to the factory wiring or infotainment system are required

---

## Board Overview

<p align="center">
  <img src="images/PCB/navbridge_cm4_pcb_top.png" width="600">
</p>

<p align="center">
  <img src="images/PCB/navbridge_cm4_pcb_bottom.png" width="600">
</p>

### Connectors

| Ref | Connector | Use |
|---|---|---|
| CN1, CN2 | Hirose DF40C-100DS | Raspberry Pi CM4 / CM5 |
| CN3 | Molex Micro-Fit 3.0, 12 pins (43045-1200) | To the navigation module |
| CN4 | 2 × 9 pin header, 2.54 mm, right angle | Car harness (X1313 connector) |
| CN5 | 2 × 5 pin header, 1.27 mm | STM32 SWD |
| CN6 | M.2 Key E | PCIe x1 + USB 2.0 module |
| CN7 | M.2 Key E | USB 2.0 module |
| U1 | JST GH, 3 pins | Stereo line output |
| U2 | JST GH, 2 pins | Microphone input |
| U20 | 22-pin FPC, 0.5 mm (bottom side) | CSI camera (CAM1) |
| J1 | USB-C | Raspberry Pi flashing and bench power |
| J2 | USB-C | USB 2.0 host port, 500 mA |

#### CN4 – Car harness

| Pin | Signal | Pin | Signal |
|---|---|---|---|
| 1 | +BATT | 2 | GND |
| 3 | – | 4 | – |
| 5 | I-Bus | 6 | GND |
| 7 | – | 8 | – |
| 9 | Green out | 10 | GND |
| 11 | Blue out | 12 | GND |
| 13 | Red out | 14 | GND |
| 15 | – | 16 | – |
| 17 | Nav audio + | 18 | Nav audio − |

#### CN3 – Navigation module

| Pin | Signal | Pin | Signal |
|---|---|---|---|
| 1 | GND | 7 | +BATT |
| 2 | GND | 8 | I-Bus (navigation side) |
| 3 | GND | 9 | Red in |
| 4 | GND | 10 | Green in |
| 5 | GND | 11 | Blue in |
| 6 | Nav audio − | 12 | Nav audio + |

+BATT and the navigation audio pass straight through between CN3 and CN4. The I-Bus does not: the car I-Bus (CN4 pin 5) and the navigation module's I-Bus (CN3 pin 8) each have their own ATA663231 transceiver, and the STM32 passes messages between them. The red, green and blue signals from the navigation module go through the RGB switch before reaching the display.

#### Audio connectors

| Connector | Pin 1 | Pin 2 | Pin 3 |
|---|---|---|---|
| U2 (microphone) | MIC in | GND | |
| U1 (line out) | Left | GND | Right |

### Buttons and LEDs

| Silkscreen | Part | Function |
|---|---|---|
| nRPIBOOT | SW1 (slide switch) | Pulls the CM `nRPIBOOT` low and routes the CM USB port to the USB-C connector J1, for flashing with rpiboot |
| CN6 / USB-C | SW4 (slide switch) | Connects hub port 2 to M.2 slot CN6 (ON) or to the USB-C host port J2 (OFF) |
| RESET | SW2 | STM32 reset |
| BOOT | SW3 | STM32 BOOT0, to start the USB DFU bootloader |
| PWR | D2 (red) | CM power LED |
| ACT | D1 (green) | CM activity LED |
| STAT | D6 (yellow) | STM32 status LED |

The MCU can only be reflashed with the board powered from USB-C (J1). The BOOT button and the SWD connector's VTref both take their 3.3 V from the Compute Module.

### Power

```text
+BATT ─┬─ fuse ─ TVS ─ LMR14030 5 V buck (enabled by MCU) ─┐
       │                                                   ├─ TPS2117 ─ +5 V ─ CM and peripherals
       │                            USB-C (J1) 5 V ────────┘
       │
       ├─ diode ─ ATA663231 (car I-Bus) ─ 3.3 V ─┐
       │                                         ├─ TPS2117 ─ 3.3 V ─ STM32, RGB switch
       │                  CM 3.3 V output ───────┘
       │
       └─ diode ─ 1 kΩ ─ navigation I-Bus (LIN master termination)

+5 V ─ TPS54202 buck ─ 3.3 V ─ USB hub, USB switches, video DAC (through a ferrite bead)
+5 V ─ TPS54202 buck ─ 3.3 V ─ M.2 slots, camera
+5 V ─ TPS2051B switch ─ PCM2912A audio codec
+5 V ─ TPS2051B switch (500 mA) ─ USB-C host port (J2)
```

The two TPS54202 converters and the two TPS2051B switches are enabled by the CM 3.3 V output, so they only run while the Compute Module is on.

The battery fuse holder (FH1) takes a Littelfuse 0451003.MRL (3 A) fuse, fitted by hand after assembly.

The TPS2117 selects between the car supply and USB-C automatically, so the board can be powered on the bench from USB-C alone. A divider on its `PR1` pin makes it switch to the car supply only once the buck output is above about 4.3 V, not while the buck is still starting up. The MCU's TPS2117 does the same with the LIN regulator, above about 2.5 V.

### Video

The Raspberry Pi drives the **ADV7125** video DAC through its 24-bit DPI interface. The STM32 combines the DPI `HSYNC` and `VSYNC` into a composite sync (`CSYNC`) with a hardware timer and feeds it to the DAC, which adds it to the green channel (RGsB) as the BMW display expects.

The **TMUX1574** switch, controlled by the STM32, selects which picture reaches the display: the Raspberry Pi or the factory navigation module.

### USB

The CM USB 2.0 port goes through a **TS3USB221** switch controlled by the nRPIBOOT switch (SW1):

- **nRPIBOOT off** (normal use): CM → **USB2514B** hub:
  - port 1: M.2 slot CN7
  - port 2: M.2 slot CN6 or the USB-C host port (J2), through a second **TS3USB221** set by the CN6 / USB-C switch (SW4)
  - port 3: PCM2912A audio codec
  - port 4: STM32
- **nRPIBOOT on**: CM → USB-C connector (J1), to flash the eMMC with rpiboot

The USB-C host port (J2) supplies up to 500 mA through a **TPS2051B** current-limited switch and advertises default USB power (56 kΩ pull-ups on CC1/CC2). It is powered while the Compute Module is on and is meant for low-power devices, such as an analog video converter. When it is selected, M.2 slot CN6 has no USB, only PCIe.

---

## Compute Module Compatibility

The board works with the **Raspberry Pi CM4** and **CM5** only. The Compute Module must have:

- **eMMC**: the board has no SD card slot, so Lite modules (without eMMC) are not supported
- **Wireless** (Wi-Fi and Bluetooth)

> [!WARNING]
> **On a CM4, set the bootloader to turn the 3.3 V rail off at shutdown.** By default the CM4 keeps its 3.3 V rail up after `shutdown`. The MCU waits for that rail to go away before it turns the 5 V buck off, so the board would keep drawing from the car battery until it is disconnected. Set these in the CM4's bootloader EEPROM:
>
> ```text
> POWER_OFF_ON_HALT=1
> WAKE_ON_GPIO=0
> ```
>
> Compute Modules ship with the `rpi-eeprom-update` service disabled, so this is done over USB while flashing, see [Flash the Compute Module](documentation/FLASH.md#3-cm4-only-set-the-bootloader-to-turn-the-33-v-rail-off-at-shutdown). The CM5 already turns its 3.3 V rail off at shutdown.

### CM4 or CM5

The CM5 uses the same connectors as the CM4 and keeps the CM4 pinout for the interfaces this board uses (DPI, USB 2.0, PCIe, CSI camera, I²C, `nRPIBOOT`, LEDs). From testing:

- The CM5 boots faster than the CM4.
- The CM5 needs a heatsink: without one it gets too hot. The CM4 runs fine without a heatsink, but one is still nice to have.

A few more points for the CM5:

- The CM5's USB 3.0 ports are not used.
- CSYNC is generated by the STM32, so the board does not rely on the CM5's RP1 PIO as the [CM5 NavBridge](https://github.com/DrZedEau/CM5_NavBridge) did.
- The 5 V rail comes from a 3.5 A buck converter. A CM5 under heavy load, together with power-hungry M.2 or USB devices, can get close to that limit.

---

## Raspberry Pi Setup

On a fresh Raspberry Pi OS install on the Compute Module (CM4 or CM5), run:

```bash
curl -fsSL https://raw.githubusercontent.com/DrZedEau/NavBridge_CM4/main/scripts/setup.sh | sudo bash
```

Then reboot. The script writes `config.txt` for the board's display, USB hub and I²C, updates the system and installs the packages it needs, loads `i2c-dev` and unblocks Bluetooth. See [scripts/README.md](scripts/README.md#setup-script) for the details.

The full procedure, from flashing the Compute Module (including the CM4 bootloader setting) to the MCU firmware and the app, is in the [documentation](#documentation).

---

## Documentation

Step-by-step guides in [documentation/](documentation/README.md):

1. [Make the navigation cable](documentation/NAV_CABLE.md)
2. [Flash the Compute Module](documentation/FLASH.md), with the CM4 bootloader setting
3. [Set up the Pi and flash the MCU](documentation/SETUP.md)
4. [Install the NavBridge App](documentation/APP.md)
5. [Install in the car](documentation/NAV_CABLE.md#install-in-the-car)

---

## Repository Structure

```text
hardware/      KiCad project (revision 0.3)
documentation/ Build, setup and installation guides
firmware/      STM32C071 MCU firmware
scripts/       Raspberry Pi setup script
3D/            STL files for printed parts (case and connector)
images/        Pictures and renders
```

The KiCad project in [hardware/](hardware/) was made with KiCad 10 and includes its symbol, footprint and 3D model libraries.

---

## Project Status

Revision 0.3 is the current design. It adds the USB-C host port and fixes the issues found on the revision 0.2 prototypes, which work in the car with the I-Bus bodge described in [hardware/README.md](hardware/README.md#known-issues-on-revision-02). See [Revision history](hardware/README.md#revision-history) for the changes. Earlier revisions are in the git history.

- [x] Schematic and PCB layout (revision 0.3)
- [x] Revision 0.2 prototypes built and tested in the car
- [x] Tested with a CM4 and a CM5
- [ ] Revision 0.3 prototypes
- [ ] STM32 firmware: first version in [firmware/](firmware/) (I-Bus bridge, Pi power, video source, CSYNC, USB bootloader), being tested on the prototype
- [ ] Raspberry Pi setup script: first version in [scripts/](scripts/) (`config.txt`, packages, I²C, Bluetooth)
- [ ] 3D printed case and connector

---

## Roadmap

- Build and test the revision 0.3 prototypes
- Expand compatibility testing across additional BMW platforms

---

## Contributing

Contributions are welcome.

Bug reports, hardware improvements, firmware enhancements, and documentation updates are all appreciated. Please open an issue or submit a pull request if you would like to contribute.

---

## License

Copyright 2026 DrZedEau

The hardware design — the KiCad project in `hardware/`, the 3D printed parts in `3D/` and the images in `images/` — is licensed under the **CERN Open Hardware Licence Version 2 – Strongly Reciprocal** (CERN-OHL-S-2.0), see [LICENSE](LICENSE). You may redistribute and modify it and make products from it under the terms of that licence. It is distributed without any express or implied warranty, including of merchantability, satisfactory quality and fitness for a particular purpose.

Source location: https://github.com/DrZedEau/NavBridge_CM4

As required by section 4 of the CERN-OHL-S, products made from this design must, where practicable, keep this source location visible.

The firmware in `firmware/` and the Raspberry Pi setup script in `scripts/` are licensed under the **GNU General Public License v2.0** (GPL-2.0), see [firmware/LICENSE](firmware/LICENSE) and [scripts/LICENSE](scripts/LICENSE). Third-party files keep their own licences.

---

## References

- CM5 NavBridge (previous board) https://github.com/DrZedEau/CM5_NavBridge
- NavBridge App https://github.com/DrZedEau/NavBridge_App
- LIVI project https://github.com/f-io/LIVI
- Raspberry Pi CM4 datasheet https://datasheets.raspberrypi.com/cm4/cm4-datasheet.pdf
- BMW Wiring Diagram System (WDS) https://bmwteka.com/wds/en/e85/e82460cf
- BMW On-Board Monitor documentation https://bmwteka.com/wds/en/e85/1b7f9876
- Community resources https://www.e46fanatics.com/threads/bmw-on-board-monitor-without-navigation-unit.1303552
