/* NavBridge CM4 bootloader: the first 14 KB of flash. See src/layout.h. */
MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 14K
  RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 24K
}

INCLUDE sections.ld
