# STM32 Firmware

Firmware for the **STM32C071KBT6** (U9) on the NavBridge CM4 board. Bare-metal C on ST's CMSIS headers, no HAL.

## What it does

- **I-Bus bridge.** Every valid frame heard on the car I-Bus (LIN1, U8) is sent unchanged on the navigation module's I-Bus (LIN2, U10), and the other way round. While the Pi's picture is shown, the app's buttons are kept from the navigation module, see [Buttons](#buttons).
- **I-Bus to the Raspberry Pi.** Every frame from either side is sent to the Pi over USB, one text line per frame, in the format of the CM5 NavBridge firmware. An `RGB RPI` / `RGB NAV` line tells the app which picture is shown, so it only acts on the buttons while it is the Pi's.
- **Raspberry Pi power.** The 5 V buck is off at power-on. The ignition message turns it on, and it turns off once the Compute Module's 3.3 V rail has gone away.
- **Video source.** The RGB switch starts on the factory navigation picture. The telephone button toggles between the navigation module and the Pi; AM, FM, MODE, radio BM, INFO, TONE and SELECT bring the navigation picture back. MENU is left to the app. The app can also switch it over USB (`RGB RPI`, `RGB NAV`).
- **CSYNC.** Composite sync for the ADV7125, generated from the DPI `HSYNC` and `VSYNC` by TIM1 (progressive scan).
- **USB update.** A small bootloader of its own lets the Pi update the firmware over USB, see [Bootloader and updates](#bootloader-and-updates).
- **CD changer emulator.** While the ignition is on, the MCU answers the radio as a CD changer, so that the radio's CD mode plays the Pi's audio. Not when the car has a real CD changer, see [CD changer emulator](#cd-changer-emulator).
- **STAT LED** (D6) on whenever the MCU is powered.

Not implemented yet: the keyboard (HID) button bindings and a watchdog. The previous board's ATmega32U4 firmware, which has the HID bindings, is in the [CM5 NavBridge](https://github.com/DrZedEau/CM5_NavBridge) repository.

## Behaviour

### I-Bus

Both buses run at 9600 baud, 8 data bits, even parity, 1 stop bit. A frame is: source, length (bytes that follow), destination, data, checksum (XOR of all the bytes is 0).

- Frames are cut out of the byte stream by their length byte and checksum. A byte with a parity, framing or noise error ends the frame being assembled. Only frames with a valid checksum are forwarded and reported.
- Before sending, the MCU waits until the bus has been quiet for 3 ms. The LIN transceiver echoes the bus, so each byte is checked as it comes back before the next one is sent. If another device talks at the same time, the MCU stops, and sends the whole frame again after a short random delay (up to 8 times).
- Our own echo is never taken as a received frame, so forwarded frames are not reported twice and do not loop.
- The transceivers are only enabled once U8 is known to have its 12 V supply (`lin=on` in `STATUS`), see [LIN transceiver supply](#lin-transceiver-supply). On the bench with USB-C only, the I-Bus stays off.

Forwarding in each direction can be switched off in [src/config.h](src/config.h) (`IBUS_FORWARD_CAR_TO_NAV`, `IBUS_FORWARD_NAV_TO_CAR`).

> **Revision 0.2: the navigation-side I-Bus needs a master pull-up.** The board splits the I-Bus: the car side keeps the car's own termination, but the navigation side (U10 to CN3 pin 8) only has U10's internal 30 kΩ slave pull-up. With the navigation module connected that is too weak: in the car every byte sent to it came back distorted (`ibus2` `col` and `err` rising, `tx` at 0, every frame dropped), while with CN3 unplugged the same frames went through. The fix is a LIN master termination on that line: 1 kΩ in series with a diode (e.g. 1N4148) from +BATT (CN3 pin 7, diode anode) to the navigation I-Bus (CN3 pin 8, resistor end), see [hardware/README.md](../hardware/README.md#known-issues).

### Messages the board reacts to

| Message | Frame | Action |
|---|---|---|
| Ignition KL-15 on (position 2 or start) | `80 04 BF 11 xx`, bit 1 of `xx` set | Buck on |
| Telephone button | `F0 04 FF 48 08` | Toggle navigation / Pi picture (Pi only while the CM is on) |
| AM, FM, MODE, radio BM | `F0 04 68 48 21/31/23/30` | Navigation picture |
| INFO | `F0 05 FF 47 00 38` | Navigation picture |
| TONE | `F0 04 68 48 04` | Navigation picture |
| SELECT | `F0 05 FF 47 00 0F` | Navigation picture |

TONE and SELECT open the radio's menus, which the radio draws on the navigation module's screen. The radio is on the car side and hears them straight from the board monitor, so they cannot be kept from it: they bring the navigation picture back, to show that menu. MENU does not: it is an app button, see [Buttons](#buttons).

### Buttons

While the Pi's picture is shown, the app buttons below are not passed on to the navigation module, so its menus do not move behind the Pi's picture. They still reach the Pi, and the car side (radio, telephone) gets them as before. With the navigation picture shown, they go to the navigation module as usual.

| App button | Press frame |
|---|---|
| Presets 1 to 6 | `F0 04 68 48 11`, `01`, `12`, `02`, `13`, `03` |
| < and > (search left, right) | `F0 04 68 48 10`, `F0 04 68 48 00` |
| MENU | `F0 04 FF 48 34` |
| CLOCK | `F0 04 FF 48 07` |
| Tape direction | `F0 04 68 48 14` |
| Knob press | `F0 04 3B 48 05` |
| Knob turns, every step count | `F0 04 3B 49 xx` |
| Steering wheel search up, down | `50 04 68 3B 01`, `50 04 68 3B 08` |
| Steering wheel send/end | `50 04 C8 3B 80` |
| Steering wheel R/T (mode) | `50 04 FF 3B 40` |

- The frames are matched on their source, message and button code, whatever their destination, as the NavBridge app reads them ([src/ibus_msg.h](src/ibus_msg.h), `ibus_app_button`).
- **A button's hold and release frames go where its press went**, so the navigation module never sees a press without its release.
- **Every other frame is always passed on**: the buttons that switch the picture (telephone, AM, FM, MODE, radio BM, INFO, TONE, SELECT), other buttons such as eject, and everything that is not a button (ignition, lights, the radio's screen texts, the telephone's data).
- This matches the `RGB` lines the app gets (see [USB](#usb)): with `RGB RPI` the app buttons are the app's and the navigation module does not get them; with `RGB NAV` they are the navigation module's and the app ignores them.

`btnkept` in `STATUS` counts the app button frames kept from the navigation module.

### CD changer emulator

The Pi's line output (U1) goes to the radio's CD changer input, and the radio only plays that input, in its CD mode (chosen with MODE), when a CD changer answers on the I-Bus at address `18`. The MCU answers as one, with the frames of the CM5 NavBridge firmware ([src/cdchanger.c](src/cdchanger.c)):

| Radio | MCU (address `18`) |
|---|---|
| Poll: `68 03 18 01` | Ready `18 04 FF 02 00`, then the status |
| Request: `68 05 18 38 cc aa` | Status: `18 0A 68 39 ss aa 00 01 00 01 01` |

- Always disc 1, track 1. The status follows the radio's requests: `cc` = 03 play, 02 pause, 01 stop, 04 seek (`aa` 00 back, 01 forward), 06 disc, 0A track (both answered as playing), anything else answered with the current status. Playing is `ss aa` = `02 09`, which makes the radio play the input.
- The radio's requests reach the Pi like any other frame (`IBUS1 RX ... 680518380A00` for a track change, for example), so the app can turn them into media controls.
- The MCU's frames go to the radio on the car side, and to the navigation module as well, as a real changer's frames would be forwarded to it. They are not reported to the Pi.

**When it runs:**

- Only while the ignition is on (KL-15, from the IKE's `80 04 BF 11 xx`). It stops answering as soon as the ignition goes off.
- **Never when the car has a real CD changer.** Any frame from address `18` that is not the MCU's own means a real changer is there: the emulator then stays off until the MCU restarts, which it does at every wake-up of a parked car (see [Parked car: transceiver sleep](#parked-car-transceiver-sleep)). A real changer announces itself (`18 04 FF 02 01`) when the car wakes up, and answers the radio's polls within milliseconds.
- After the ignition comes on, the MCU first lets a real changer show up. It starts when a radio poll has gone 200 ms without an answer, or after 10 s if the radio has not polled. It then announces itself (`18 04 FF 02 01`), and the radio polls it at once. Both times are in [src/config.h](src/config.h) (`CDCHANGER_ANSWER_MS`, `CDCHANGER_LISTEN_MS`); `CDCHANGER_EMULATOR 0` turns the emulator off.
- If the MCU restarts while the ignition is already on, the emulator waits for the next ignition message.

`changer` in `STATUS` shows its state: `off` (ignition off), `wait` (ignition on, waiting for a real changer to show up), `on` (answering the radio), `real` (a real changer was heard).

### Power

| State | Buck | USB | CSYNC | RGB switch |
|---|---|---|---|---|
| Power-on (car battery connected), or woken by I-Bus activity | off | off | low | navigation |
| Ignition on | on | | | |
| CM 3.3 V present (`+3V3_SENSE` high for 50 ms) | | connected | running | |
| CM 3.3 V gone | off 2 s later | disconnected | low | navigation |

- The Pi shuts itself down when it reads the ignition-off message (NavBridge App: `shutdown = true`). The MCU then sees the 3.3 V rail go away and turns the buck off.
- If the CM never brings its 3.3 V rail up within 30 s of the buck turning on, the buck is turned off again.
- The MCU stays powered from the car (car-side ATA663231 regulator) while the CM is off. USB and CSYNC are only active while the CM 3.3 V rail is present, and the sync inputs are pulled down meanwhile, so the MCU never back-powers the USB hub, the video DAC or the CM's DPI pins.
- A warm reset (RESET button, crash) keeps the buck as it was, so the Pi does not lose power if it runs from the car. The state is kept in `PWR_BKP0R`, which only a power-on reset clears. A USB update keeps it too: the firmware and the bootloader hand over by jumping, and the pin stays driven.

### Parked car: transceiver sleep

With the CM off, the MCU runs from the car-side ATA663231's 3.3 V regulator. Once the CM and the buck are off and neither I-Bus has carried a byte for 30 s (`IBUS_SLEEP_TIMEOUT_MS` in [src/config.h](src/config.h)), the firmware puts both transceivers into Sleep mode (about 9 µA each), the navigation side first. The car-side one's regulator then switches off, and the MCU with it. The board then draws only the transceivers' sleep current from the battery.

The next I-Bus activity wakes the car-side transceiver (any byte holds the bus low longer than its 50 to 150 µs wake-up time). Its regulator comes back, the MCU starts (bootloader, then firmware), and the transceivers are back in Normal mode within a few milliseconds. The frames sent while the MCU starts are not seen; the ignition message, which comes later, is.

Sleep follows the ATA663231 datasheet (section 1.2.3): EN falls together with TXD, so no dominant pulse reaches the bus. On U8 both are on GPIOB and fall in one register write; on U10 EN (PB4) falls a few nanoseconds before TXD (PA4), within the 3.2 µs the datasheet allows. If the MCU is still powered 0.5 s after (something else supplies it), both transceivers are woken again and `sleepfail` in `STATUS` counts it.

On the bench this shows with 12 V on `+BATT` and the CM off (no USB-C): the STAT LED goes off 30 s after power-up, and pulling the car I-Bus (CN4 pin 5) to GND for a moment brings it back.

> **The CM must turn its 3.3 V rail off when it shuts down.** On a CM4 that needs `POWER_OFF_ON_HALT=1` and `WAKE_ON_GPIO=0` in the bootloader EEPROM (`sudo rpi-eeprom-config --edit`); the CM4 defaults are `POWER_OFF_ON_HALT=0` and `WAKE_ON_GPIO=1`. Otherwise the rail stays up after `shutdown`, and the buck stays on until the battery is disconnected. The CM5 already defaults to `POWER_OFF_ON_HALT=1`, and `WAKE_ON_GPIO` does not apply to it.

### CSYNC

The DPI mode of the BMW display (`config.txt`: 8 MHz pixel clock, `hsync=40`, 508 pixels per line, `hsync-invert`, `vsync-invert`) gives:

- HSYNC width = hsync pixels / pixel clock = 40 / 8 MHz = **5.0 µs**
- Line period = total pixels / pixel clock = 508 / 8 MHz = **63.5 µs**

As in the progressive program of Raspberry Pi's [dpi_csync.c](https://github.com/raspberrypi/utils/blob/master/piolib/examples/dpi_csync.c), CSYNC starts on each HSYNC leading edge and lasts one HSYNC width (5.0 µs). While VSYNC is active, it lasts one line minus one HSYNC width (58.5 µs). CSYNC is active low, like the ADV7125 `SYNC` input.

TIM1 does it in hardware, with no interrupt per line: each HSYNC leading edge restarts the counter (slave reset mode on PA8), CH2 drives CSYNC on PA9 in PWM mode for the pulse width, and the VSYNC interrupt (PA6) chooses the width. A VSYNC change that comes during the HSYNC part of a pulse applies to that pulse, as the RP1 program reads VSYNC at the end of HSYNC.

The widths are set in pixels in [src/config.h](src/config.h) and scaled to the line period that TIM1 measures, so the internal oscillator's tolerance cancels out: the pulses have the accuracy of the Pi's crystal. CSYNC only runs while the measured line period is within 1/16 of 63.5 µs; otherwise, or when HSYNC stops, it stays idle (high).

A VSYNC edge that comes within 3 µs of the next line waits for that line to start, so the broad pulses start on the same line in every frame. On the prototype the CM5's VSYNC edges come 1.8 µs into the line, inside the HSYNC part, and every change applies to that line (`vspos=86..89 vsnow=... vsnext=0` in `STATUS`).

**Edge phase against the DAC clock.** The ADV7125 latches `SYNC` on the Pi's 8 MHz pixel clock, while the MCU's CSYNC edges sit on its own 48 MHz grid, a fixed delay after the HSYNC edge plus 0 to 21 ns. If that fixed delay puts the edge right at a DAC sampling instant, the DAC catches it one pixel (125 ns) apart from one line to the next, and the picture shakes horizontally. The delay depends on the TI1 input filter, which `CSYNC FILTER n` changes live: 0, 1, 2 and 4 give four different phases, about 21 to 42 ns apart. In the car (prototype, CM5, BMW display) filter 2 made the picture shake, and 0 and 1 were steady; the default is 0 (`CSYNC_INPUT_FILTER` in [src/csync.c](src/csync.c)), the farthest from the bad phase. Another board or Compute Module could put the bad phase elsewhere: check the picture, and try `CSYNC FILTER 1`, `2` or `4` if it shakes.

### USB

The MCU is a USB CDC ACM device (virtual serial port) behind the USB2514B hub: `0483:5740`, manufacturer "NavBridge", product "NavBridge CM4", serial number "MCU". On the Pi it is:

```text
/dev/serial/by-id/usb-NavBridge_NavBridge_CM4_MCU-if00
```

The NavBridge App's `[ibus] device` setting has to point there instead of the ATmega32U4's port.

The MCU sends one line per I-Bus frame, while the port is open (DTR set):

```text
IBUS1 RX <ms since start> <frame in hex>     frame heard on the car I-Bus (LIN1)
IBUS2 RX <ms since start> <frame in hex>     frame heard on the navigation I-Bus (LIN2)
```

For example `IBUS1 RX 7323494 8004BF110329`. Lines that do not fit in the USB buffer are dropped whole.

It also tells the app which picture the display shows, so the app acts on the buttons only while it is the Pi's:

```text
RGB RPI      Raspberry Pi picture: the buttons are for the app
RGB NAV      factory navigation picture: the buttons are for the navigation module and radio
```

- The line is sent as soon as the port is opened (DTR set), then each time the picture changes. The `RGB` command asks for it again.
- The picture starts on the navigation module each time the CM starts. The app can switch it with `RGB RPI` (for example when it starts) and `RGB NAV`. The reply is the `RGB` line, even when the picture was already that one.
- The buttons that switch the picture (see [Messages the board reacts to](#messages-the-board-reacts-to)) are meant for the board, so their frame always comes while `RGB NAV` holds. A switch to the navigation picture is announced before that frame, and a switch to the Pi after it. The app can drop every button frame received under `RGB NAV` and never act on a switching press.
- Only the buttons should be dropped. The ignition, speed, lights and other readings are valid whatever the picture; the app needs the ignition-off message to shut the Pi down.
- If the USB buffer is full, the `RGB` line is not dropped: it goes out as soon as there is room.

It accepts one command per line:

| Command | Reply |
|---|---|
| `VERSION` | `VERSION NavBridge-CM4 <git describe>` |
| `STATUS` | One line with the power, video and I-Bus state, see below |
| `RGB` | `RGB RPI` or `RGB NAV`: the picture the display shows |
| `RGB RPI` | Show the Pi's picture; reply `RGB RPI` |
| `RGB NAV` | Show the navigation picture; reply `RGB NAV` |
| `DFU` | `DFU OK`, then hand over to the bootloader for an update (`0483:5741`), see [Bootloader and updates](#bootloader-and-updates) |
| `DIAG` | `DIAG resetflags=... csr2=... bkp0=... bkp1=... bkptest=ok optr=...`: reset flags, backup registers and option bytes |
| `CSYNC FILTER n` | Set the HSYNC input filter (0..15) until the next restart, see [CSYNC](#csync) |
| `IBUS SEND <1\|2> <hex>` | Queue a frame (checksum included) on the car (1) or navigation (2) I-Bus, for tests: `IBUS SEND 2 F0043B480582` |

`STATUS` fields:

| Field | Meaning |
|---|---|
| `cm`, `buck`, `rgb`, `lin` | CM 3.3 V present, buck enable, video source, LIN transceivers enabled (only with 12 V) |
| `btnkept` | App button frames from the car kept from the navigation module while the Pi's picture was shown, see [Buttons](#buttons) |
| `changer` | CD changer emulator: `off`, `wait`, `on` or `real`, see [CD changer emulator](#cd-changer-emulator) |
| `idle`, `sleepfail` | Seconds since the last I-Bus byte heard or sent; times the transceivers were put to sleep but the MCU stayed powered |
| `csync`, `line`, `sync`, `broad` | CSYNC state, measured line period and pulse widths |
| `vsync`, `filter` | VSYNC seen, HSYNC input filter setting |
| `vspos`, `vsnow`, `vsnext`, `vswait` | Since the previous `STATUS`: line position (TIM1 ticks of 20.8 ns) of the VSYNC edges; changes applied to the line in progress, from the next line, after waiting for the line to start |
| `period` | Since the previous `STATUS`: spread of the measured line period (ticks) |
| `locks`, `losses` | CSYNC lock count, and how often it was lost (HSYNC stopped or changed) |
| `ibus1`, `ibus2` | Per bus: `rx` frames received, `tx` frames sent, `err` bytes with UART errors, `bad` checksum failures, `col` collisions (our byte came back different), `tmo` echo timeouts (our byte never came back), `drop` frames given up, `q` frames waiting, `bytes` bytes heard from others |
| `boot0`, `up` | Whether the BOOT0 pin is used (option byte), milliseconds since start |

The `line`, `sync` and `broad` values are counted with the MCU's internal oscillator (±1 %) and printed as if it ran at exactly 48 MHz; the pulses themselves are scaled to the measured line, so their real widths do not depend on it.

The USB clock comes from the internal 48 MHz oscillator, trimmed by the CRS on the host's start-of-frame packets, so the board needs no crystal.

## Pin map

| Pin | Port | Net | Function |
|---|---|---|---|
| 4 | VDD | +3V3_MUX | MCU supply |
| 6 | PF2 / NRST | NRST | RESET button (SW2), SWD connector |
| 8 | PA1 | – | STAT LED (D6, yellow, active high) |
| 9 | PA2 | MCU_BUCK_EN | Enable of the 5 V buck that powers the Raspberry Pi (10 kΩ pull-down) |
| 10 | PA3 | +3V3_SENSE | Compute Module 3.3 V rail through 1 kΩ (high when the CM is on) |
| 11 | PA4 | MCU_LIN2_TX | USART2 TX (AF1) to U10 (navigation I-Bus) |
| 12 | PA5 | MCU_LIN2_RX | USART2 RX (AF1) from U10 (navigation I-Bus) |
| 13 | PA6 | VSYNC | DPI VSYNC input (EXTI6) |
| 18 | PA8 | HSYNC | DPI HSYNC input (TIM1_CH1, AF2) |
| 19 | PA9 | CSYNC | Composite sync output to the ADV7125 `SYNC` input (TIM1_CH2, AF2) |
| 21 | PA10 | MCU_SWITCHER_IN | TMUX1574 select: low = factory navigation (10 kΩ pull-down), high = Raspberry Pi |
| 22 | PA11 | USB_MCU_N | USB D− to the hub |
| 23 | PA12 | USB_MCU_P | USB D+ to the hub |
| 24 | PA13 | SWDIO | SWD connector |
| 25 | PA14 / BOOT0 | SWCLK | SWD connector, BOOT button (SW3) through 1 kΩ |
| 28 | PB4 | MCU_LIN2_EN | U10 `EN` (10 kΩ pull-down) |
| 29 | PB5 | MCU_LIN1_EN | U8 `EN` (10 kΩ pull-down) |
| 30 | PB6 | MCU_LIN1_TX | USART1 TX (AF0) to U8 (car I-Bus) |
| 31 | PB7 | MCU_LIN1_RX | USART1 RX (AF0) from U8 (car I-Bus) |

PB6/PB7 and PA4/PA5 belong to the I-Bus and PA2 controls the Raspberry Pi power: do not use them for a debug UART.

### LIN transceiver supply

The car-side ATA663231 (U8) also powers the MCU while the CM is off: its 3.3 V regulator feeds `+3V3_LIN_REG`, the preferred input of the MCU's power mux (U11, TPS2117, `PR1` tied to `VIN1`). The mux switches to it as soon as it rises above about 1 V.

Without 12 V on `+BATT` (bench, USB-C only), U8 is unpowered. Driving its `EN` or `TXD` high, or pulling its `RXD` up, then feeds `+3V3_LIN_REG` through U8's protection diodes. The mux switches the MCU onto that rail, it collapses, and the MCU browns out and restarts over and over: the STAT LED blinks fast and USB never comes up. The firmware therefore keeps every LIN pin floating (`EN` low through its pull-downs, `RXD` and `TXD` pulled down) until U8 is known to be powered:

- the CM 3.3 V rail is absent: then U8's regulator is what powers the MCU;
- or U8 drives its `RXD` or `TXD` high against the MCU pull-downs, as it does in Silent mode (after a warm reset). An unpowered U8 cannot.

On the bench, connect 12 V before USB-C, or press RESET once both are on. Its `EN` must also never go low while its `TXD` is low: that puts U8 to sleep and switches its regulator off.

## Building

The compiled images are in [bin/](bin/), so building is only needed after changing the code:

| File | Content |
|---|---|
| `bin/navbridge-cm4-bootloader.bin` | Bootloader, at `0x08000000` |
| `bin/navbridge-cm4-mcu.bin` | Firmware, at `0x08004000` |
| `bin/navbridge-cm4-bootloader-update.bin` | Bootloader updater: a firmware that replaces the bootloader over USB |

The build needs `make` and the Arm GNU toolchain (`arm-none-eabi-gcc`) on the PATH. STM32CubeIDE ships both, under `C:\ST\STM32CubeIDE_*\STM32CubeIDE\plugins\` (`...gnu-tools-for-stm32...\tools\bin` and `...externaltools.make...\tools\bin`). On a Raspberry Pi or another Linux machine: `sudo apt install gcc-arm-none-eabi make`.

```bash
make release
```

That builds the three programs into `build/` (`.elf`, `.bin`, `.hex`, `.map`) and copies the three `.bin` files to `bin/`. `make` alone only builds; `make clean` removes `build/`.

## Bootloader and updates

The MCU holds two programs, which hand over to each other by jumping (never by resetting, so the buck keeps its state):

| Address | Size | Content |
|---|---|---|
| `0x08000000` | 14 KB | **Bootloader** ([bootloader/](bootloader/)): installed once; replaced only on purpose, with the bootloader updater |
| `0x08003800` | 2 KB | Firmware info: size and CRC of the firmware, written by the bootloader at the end of a complete download |
| `0x08004000` | 112 KB | **Firmware** ([src/](src/)): updated over USB as often as needed |

The bootloader runs first at every reset and starts the firmware at once, unless:

- the firmware handed over for an update (`DFU` command);
- **BOOT** (SW3) is held while **RESET** (SW2) is pressed;
- there is no valid firmware: blank, interrupted update, or a CRC that no longer matches.

Then it is a USB DFU device, `0483:5741` "NavBridge CM4 bootloader", and the STAT LED blinks fast. A complete download starts the new firmware. Entered with BOOT, it waits until a firmware is written or the MCU is reset. The bootloader keeps the board's rules: it never drives the LIN pins, keeps the buck as it was, and only enables USB while the CM 3.3 V rail is present.

On `DFU`, the firmware clears the firmware info before handing over. So until a complete new firmware has been written, the bootloader stays in update mode, across resets and power cycles too. An interrupted update is fixed by running the update again.

The option bytes stay as they leave the factory (`nBOOT_SEL = 1`): the chip always boots the bootloader. ST's own ROM bootloader (`0483:df11`) is only used once, on a blank chip, to install the NavBridge one. Re-entering ST's bootloader on a programmed chip does not work on this board (see [Bootloader notes](#bootloader-notes)).

### Updating from the Raspberry Pi

The MCU is reached through the on-board USB hub, so the Pi writes it with `dfu-util`. On the Pi, with this repository checked out:

```bash
sudo apt install dfu-util
```

```bash
bash scripts/mcu-flash.sh
```

[mcu-flash.sh](../scripts/mcu-flash.sh) takes the images from `firmware/bin/` and does whatever the MCU needs:

- **Blank MCU, straight from the factory** (ST's ROM bootloader, `0483:df11`): installs the NavBridge bootloader through it, then the firmware through the bootloader.
- **Running firmware**: sends `DFU`, waits for the bootloader, writes the firmware.
- **Already in the bootloader** (BOOT+RESET, or an interrupted update): writes the firmware.

It then waits for the firmware to come back on USB and prints its version. A firmware update takes about 4 s. `dfu-util` runs through `sudo` when the USB device is not writable by the current user (the usual case), so the script asks for the password. Other image files can be given as arguments: `mcu-flash.sh [firmware.bin [bootloader.bin [updater.bin]]]`.

Tested on the prototype (USB-C only): first install on a blank chip, repeated updates from the running firmware, BOOT+RESET, and an update killed after 2 of 7 blocks (the MCU stayed in the bootloader, and the next run finished it).

### Bootloader notes

- **Bootloader version.** The bootloader reports its version as its USB `bcdDevice` (`BOOTLOADER_VERSION` in [src/config.h](src/config.h)), shown by `lsusb -v` and by `mcu-flash.sh --bootloader-update`. 1.00 was the first build: it turned the buck off when it took over for an update, and could start the firmware just before `dfu-util` read its last status (`dfu-util` then exited with an error, the firmware was fine). 1.01 fixes both.
- **Updating the bootloader itself, over USB** (board on USB-C): `bash scripts/mcu-flash.sh --bootloader-update` on the Pi. The updater ([bootloader/updater.c](bootloader/updater.c)) is a firmware that carries the new bootloader. Installed through the current bootloader, it erases flash page 0 first, writes pages 1 to 6, writes page 0 last, verifies, and hands over to the new bootloader, which then receives the normal firmware. While page 0 is erased the chip counts as empty, so if power is lost in that window it starts ST's DFU at power-on, and `mcu-flash.sh` with the bootloader image reinstalls everything (that recovery has not been tried). Only the updater build can write the bootloader's pages. Tested on the prototype: 1.00 to 1.01 in about 6 s, then five firmware updates with `dfu-util` exiting 0, and an interrupted update recovered.
- **Over SWD** (see [SWD](#swd)), write the bootloader at `0x08000000`; the bootloader then waits in update mode, and `mcu-flash.sh` writes the firmware over USB (the firmware info that marks it valid is only written by the bootloader).
- **The PWR backup registers** (`PWR_BKP0R`..`BKP3R`) only keep their low 16 bits. They hold the buck state across a warm reset and the update request; `DIAG` checks them.
- **ST's ROM bootloader on a programmed chip.** It starts a valid image again, whether entered by a jump or by a reset with `FLASH_ACR.PROGEMPTY` set. With `nBOOT_SEL = 0`, BOOT0 high at reset gave no USB activity at all on this board. Its interface scan uses pins that are wired to other circuits here: PA10 (USART1 RX) is the RGB select with a 10 kΩ pull-down, and PB6/PB7 go to U8. AN2606 also asks that no USB host be connected during that scan, and the hub always is. Not investigated further, since the NavBridge bootloader does not need it.

### SWD

CN5 is a standard 10-pin 1.27 mm Cortex debug connector:

| Pin | Signal | Pin | Signal |
|---|---|---|---|
| 1 | VTref (CM 3.3 V) | 2 | SWDIO |
| 3 | GND | 4 | SWCLK |
| 5 | GND | 6 | – |
| 7 | – | 8 | – |
| 9 | GND | 10 | NRST |

VTref is connected to the Compute Module 3.3 V rail, so the debugger only sees the target voltage while the CM is on.

With an ST-LINK (STM32CubeProgrammer, also bundled with STM32CubeIDE), connecting under reset works whatever the MCU runs:

```bash
STM32_Programmer_CLI -c port=SWD mode=UR reset=HWrst -e all -w bin/navbridge-cm4-bootloader.bin 0x08000000 -v -hardRst
```

The bootloader then waits in update mode, and `mcu-flash.sh` on the Pi writes the firmware.

## Files

| File | Content |
|---|---|
| [bin/](bin/) | Compiled bootloader, firmware and bootloader updater (`make release`) |
| [Makefile](Makefile) | Builds the three programs |
| [src/config.h](src/config.h) | Settings: DPI timing for CSYNC, I-Bus timing and forwarding, power delays, USB IDs |
| [src/layout.h](src/layout.h) | Flash layout and the bootloader / firmware contract |
| [src/main.c](src/main.c) | Firmware: start-up, main loop, what each I-Bus frame triggers |
| [src/board.c](src/board.c) | Shared: clock (HSI48, no crystal), time base, jump between the two programs |
| [src/hardware.c](src/hardware.c) | Shared: LED, buck enable, RGB switch, 3.3 V sense, LIN enables, BOOT button |
| [src/flash.c](src/flash.c) | Shared: flash erase and programming (never the bootloader's pages), firmware info and CRC |
| [src/usb_drd.h](src/usb_drd.h) | Shared: USB peripheral registers, packet memory, power on and off |
| [src/power.c](src/power.c) | Firmware: buck and CM power logic |
| [src/csync.c](src/csync.c) | Firmware: CSYNC generator (TIM1, EXTI6) |
| [src/ibus.c](src/ibus.c), [src/ibus_msg.h](src/ibus_msg.h) | Firmware: I-Bus receive, transmit and bridge; the messages the board reacts to |
| [src/buttons.c](src/buttons.c) | Firmware: the picture the buttons select, and which button frames reach the navigation module (no hardware access, testable on a PC) |
| [src/cdchanger.c](src/cdchanger.c) | Firmware: CD changer emulator (no hardware access, testable on a PC) |
| [src/usb_cdc.c](src/usb_cdc.c) | Firmware: USB CDC ACM device |
| [src/host.c](src/host.c) | Firmware: lines to and from the Pi |
| [bootloader/main.c](bootloader/main.c) | Bootloader: start the firmware or stay for an update |
| [bootloader/dfu.c](bootloader/dfu.c) | Bootloader: USB DFU 1.1 device |
| [bootloader/updater.c](bootloader/updater.c), [bootloader/image.S](bootloader/image.S) | Bootloader updater: a firmware that replaces the bootloader over USB |
| `linker/` | `bootloader.ld` and `app.ld` (memory), `sections.ld` (shared) |
| `vendor/` | ST's CMSIS device files (cmsis-device-c0 v1.4.1) and the CMSIS core headers, unmodified |

## License

GPL-2.0, see [LICENSE](LICENSE). The CSYNC generator follows [mbt28/pico-csync-generator](https://github.com/mbt28/pico-csync-generator) (GPL-2.0) and Raspberry Pi's [dpi_csync.c](https://github.com/raspberrypi/utils/blob/master/piolib/examples/dpi_csync.c) (BSD-3-Clause). Third-party files, such as ST's CMSIS headers, keep their own licences.
