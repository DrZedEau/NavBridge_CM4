# STM32 Firmware

Firmware for the **STM32C071KBT6** (U6) on the NavBridge CM4 board. *Not added yet.*

## Role of the MCU

- Generate the composite sync (`CSYNC`) for the BMW display from the Raspberry Pi DPI `HSYNC` and `VSYNC`, with TIM1
- Read and write the BMW I-Bus through the ATA663231 LIN transceiver
- Switch the Raspberry Pi 5 V supply on and off (LMR14030 enable)
- Select the picture sent to the display (Raspberry Pi or factory navigation)
- Talk to the Raspberry Pi over USB (through the on-board hub)

The previous board's ATmega32U4 firmware (power management, I-Bus button translation, CDC emulation) is in the [CM5 NavBridge](https://github.com/DrZedEau/CM5_NavBridge) repository.

## Pin map

| Pin | Port | Net | Function |
|---|---|---|---|
| 4 | VDD | +3V3_MUX | MCU supply |
| 6 | PF2 / NRST | NRST | RESET button (SW2) |
| 8 | PA1 | – | STAT LED (D5, blue, active high) |
| 9 | PA2 | MCU_BUCK_EN | Enable of the 5 V buck that powers the Raspberry Pi |
| 10 | PA3 | +3V3 | Compute Module 3.3 V rail (high when the CM is on) |
| 13 | PA6 | VSYNC | DPI VSYNC input (EXTI6) |
| 18 | PA8 | HSYNC | DPI HSYNC input (TIM1_CH1) |
| 19 | PA9 | CSYNC | Composite sync output to the ADV7125 `SYNC` input (TIM1_CH2) |
| 21 | PA10 | MCU_SWITCHER_IN | TMUX1574 select (video source) |
| 22 | PA11 | USB_MCU_N | USB D− to the hub |
| 23 | PA12 | USB_MCU_P | USB D+ to the hub |
| 25 | PA14 / BOOT0 | – | BOOT button (SW4) |
| 29 | PB5 | MCU_LIN_EN | ATA663231 `EN` |
| 30 | PB6 | MCU_TX | USART1 TX to the LIN transceiver |
| 31 | PB7 | MCU_RX | USART1 RX from the LIN transceiver |

PB6 and PB7 belong to the I-Bus and PA2 controls the Raspberry Pi power: do not use them for a debug UART.

## Flashing

The board has no SWD connector (PA13 / SWDIO is not connected), so the MCU is flashed over USB with the ROM DFU bootloader, through the on-board hub.

1. Hold **BOOT** (SW4) and press **RESET** (SW2).
2. Flash with STM32CubeProgrammer, for example `STM32_Programmer_CLI -c port=USB1 -d firmware.bin 0x08000000 -v -rst`.

> **Option byte nBOOT_SEL.** From the factory the STM32C0 ignores the BOOT0 pin (`nBOOT_SEL = 1`). A blank chip still starts the bootloader on its own, so the first flash should also set `nBOOT_SEL = 0`; otherwise the BOOT button has no effect once firmware is installed, and without SWD there is no other way back in.

## License

GPL-2.0, see [LICENSE](LICENSE). The CSYNC generator is a port of [mbt28/pico-csync-generator](https://github.com/mbt28/pico-csync-generator), which is also GPL-2.0. Third-party files, such as ST's CMSIS headers, keep their own licences.
