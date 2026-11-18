/* NavBridge CM4 firmware: after the bootloader (14 KB) and the firmware info page (2 KB).
 * See src/layout.h. */
MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08004000, LENGTH = 112K
  RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 24K
}

INCLUDE sections.ld
