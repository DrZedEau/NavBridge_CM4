# STM32 Firmware

Firmware for the **STM32C071KBT6** (U9) on the NavBridge CM4 board. *Not added yet.*

## Role of the MCU

- Generate the composite sync (`CSYNC`) for the BMW display from the Raspberry Pi DPI `HSYNC` and `VSYNC`, with TIM1
- Read and write the BMW I-Bus through two ATA663231 LIN transceivers: U8 on the car I-Bus, U10 on the navigation module's I-Bus, and pass messages between them
- Switch the Raspberry Pi 5 V supply on and off (LMR14030 enable)
- Select the picture sent to the display (Raspberry Pi or factory navigation)
- Talk to the Raspberry Pi over USB (through the on-board hub)

The previous board's ATmega32U4 firmware (power management, I-Bus button translation, CDC emulation) is in the [CM5 NavBridge](https://github.com/DrZedEau/CM5_NavBridge) repository.

## Pin map

| Pin | Port | Net | Function |
|---|---|---|---|
| 4 | VDD | +3V3_MUX | MCU supply |
| 6 | PF2 / NRST | NRST | RESET button (SW2), SWD connector |
| 8 | PA1 | – | STAT LED (D6, yellow, active high) |
| 9 | PA2 | MCU_BUCK_EN | Enable of the 5 V buck that powers the Raspberry Pi (10 kΩ pull-down) |
| 10 | PA3 | +3V3_SENSE | Compute Module 3.3 V rail through 1 kΩ (high when the CM is on) |
| 11 | PA4 | MCU_LIN2_TX | USART2 TX to U10 (navigation I-Bus) |
| 12 | PA5 | MCU_LIN2_RX | USART2 RX from U10 (navigation I-Bus) |
| 13 | PA6 | VSYNC | DPI VSYNC input (EXTI6) |
| 18 | PA8 | HSYNC | DPI HSYNC input (TIM1_CH1) |
| 19 | PA9 | CSYNC | Composite sync output to the ADV7125 `SYNC` input (TIM1_CH2) |
| 21 | PA10 | MCU_SWITCHER_IN | TMUX1574 select (video source, 10 kΩ pull-down = factory navigation) |
| 22 | PA11 | USB_MCU_N | USB D− to the hub |
| 23 | PA12 | USB_MCU_P | USB D+ to the hub |
| 24 | PA13 | SWDIO | SWD connector |
| 25 | PA14 / BOOT0 | SWCLK | SWD connector, BOOT button (SW3) through 1 kΩ |
| 28 | PB4 | MCU_LIN2_EN | U10 `EN` (10 kΩ pull-down) |
| 29 | PB5 | MCU_LIN1_EN | U8 `EN` (10 kΩ pull-down) |
| 30 | PB6 | MCU_LIN1_TX | USART1 TX to U8 (car I-Bus) |
| 31 | PB7 | MCU_LIN1_RX | USART1 RX from U8 (car I-Bus) |

PB6/PB7 and PA4/PA5 belong to the I-Bus and PA2 controls the Raspberry Pi power: do not use them for a debug UART.

The MCU stays powered while the Compute Module is off. Keep `CSYNC` low and the USB peripheral disconnected while `+3V3_SENSE` is low, so the MCU does not back-power the video DAC and the USB hub.

## Flashing

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

### USB DFU

The MCU can also be flashed over USB with the ROM DFU bootloader, through the on-board hub.

1. Hold **BOOT** (SW3) and press **RESET** (SW2).
2. Flash with STM32CubeProgrammer, for example `STM32_Programmer_CLI -c port=USB1 -d firmware.bin 0x08000000 -v -rst`.

> **Option byte nBOOT_SEL.** From the factory the STM32C0 ignores the BOOT0 pin (`nBOOT_SEL = 1`). A blank chip still starts the bootloader on its own, so the first flash should also set `nBOOT_SEL = 0`; otherwise the BOOT button has no effect once firmware is installed and SWD is the only way back in.

## License

GPL-2.0, see [LICENSE](LICENSE). The CSYNC generator is a port of [mbt28/pico-csync-generator](https://github.com/mbt28/pico-csync-generator), which is also GPL-2.0. Third-party files, such as ST's CMSIS headers, keep their own licences.
