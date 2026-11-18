/* SPDX-License-Identifier: GPL-2.0-only
 *
 * The bootloader image carried by the bootloader updater (see updater.c). The Makefile
 * builds build/navbridge-cm4-bootloader.bin first.
 */
    .section .rodata.bootloader_image, "a"
    .balign 8
    .global bootloader_image
bootloader_image:
    .incbin "build/navbridge-cm4-bootloader.bin"
    .global bootloader_image_end
bootloader_image_end:
