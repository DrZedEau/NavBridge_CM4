# NavBridge CM4 MCU (STM32C071KBT6): bootloader and firmware.
#
# Needs make and arm-none-eabi-gcc on the PATH (both ship with STM32CubeIDE, under its
# plugins folder; on Linux: apt install gcc-arm-none-eabi make):
#   make         build/navbridge-cm4-bootloader.bin         (0x08000000, installed once)
#                build/navbridge-cm4-mcu.bin                (0x08004000, the firmware updates)
#                build/navbridge-cm4-bootloader-update.bin  (a firmware that replaces the
#                                                            bootloader, see updater.c)
#   make release copy the three .bin files to bin/, the images kept in the repository
#   make clean
# Flash layout: src/layout.h.

BUILD := build
FW_VERSION ?= $(or $(shell git describe --always --dirty --tags 2>/dev/null),dev)

COMMON_SRCS := \
	src/board.c \
	src/hardware.c \
	src/flash.c \
	vendor/cmsis-device-c0/Source/system_stm32c0xx.c
STARTUP := vendor/cmsis-device-c0/Source/startup_stm32c071xx.s

APP_SRCS := $(COMMON_SRCS) \
	src/main.c \
	src/power.c \
	src/csync.c \
	src/ibus.c \
	src/buttons.c \
	src/cdchanger.c \
	src/usb_cdc.c \
	src/host.c
BOOT_SRCS := $(COMMON_SRCS) \
	bootloader/main.c \
	bootloader/dfu.c
# Bootloader updater: a firmware carrying the bootloader image (bootloader/image.S).
UPD_SRCS := $(COMMON_SRCS) \
	bootloader/updater.c

CC      := arm-none-eabi-gcc
OBJCOPY := arm-none-eabi-objcopy
SIZE    := arm-none-eabi-size

MCU := -mcpu=cortex-m0plus -mthumb
CFLAGS_COMMON := $(MCU) -std=gnu11 -g3 -Wall -Wextra -Wshadow -ffunction-sections -fdata-sections \
                 -DSTM32C071xx -DFW_VERSION=\"$(FW_VERSION)\" \
                 -Isrc -Ivendor/cmsis-device-c0/Include -Ivendor/cmsis-core/Include -MMD -MP
APP_CFLAGS  := $(CFLAGS_COMMON) -O2 -DVECT_TAB_OFFSET=0x4000U
BOOT_CFLAGS := $(CFLAGS_COMMON) -Os -Ibootloader
UPD_CFLAGS  := $(CFLAGS_COMMON) -Os -DVECT_TAB_OFFSET=0x4000U -DFLASH_BOOTLOADER_WRITE
# ST's startup file replaces crt0 (which would drag in exit() and stdio).
LDFLAGS_COMMON := $(MCU) -Llinker -nostartfiles --specs=nano.specs -Wl,--gc-sections \
                  -Wl,--no-warn-rwx-segments -Wl,--print-memory-usage

APP_DIR  := $(BUILD)/app
BOOT_DIR := $(BUILD)/bootloader
UPD_DIR  := $(BUILD)/updater
APP_OBJS  := $(addprefix $(APP_DIR)/,$(APP_SRCS:.c=.o) $(STARTUP:.s=.o))
BOOT_OBJS := $(addprefix $(BOOT_DIR)/,$(BOOT_SRCS:.c=.o) $(STARTUP:.s=.o))
UPD_OBJS  := $(addprefix $(UPD_DIR)/,$(UPD_SRCS:.c=.o) $(STARTUP:.s=.o)) $(UPD_DIR)/bootloader/image.o
APP_ELF  := $(BUILD)/navbridge-cm4-mcu.elf
BOOT_ELF := $(BUILD)/navbridge-cm4-bootloader.elf
UPD_ELF  := $(BUILD)/navbridge-cm4-bootloader-update.elf

# Rebuild host.o (which prints the version) when the version changes.
VERSION_STAMP := $(BUILD)/version.txt
$(shell mkdir -p $(BUILD))
ifneq ($(file < $(VERSION_STAMP)),$(FW_VERSION))
$(file > $(VERSION_STAMP),$(FW_VERSION))
endif

all: $(BOOT_ELF) $(BOOT_ELF:.elf=.bin) $(BOOT_ELF:.elf=.hex) \
     $(APP_ELF) $(APP_ELF:.elf=.bin) $(APP_ELF:.elf=.hex) \
     $(UPD_ELF) $(UPD_ELF:.elf=.bin)

$(APP_DIR)/src/host.o: $(VERSION_STAMP)

$(APP_DIR)/%.o: %.c Makefile
	@mkdir -p $(dir $@)
	$(CC) $(APP_CFLAGS) -c $< -o $@

$(BOOT_DIR)/%.o: %.c Makefile
	@mkdir -p $(dir $@)
	$(CC) $(BOOT_CFLAGS) -c $< -o $@

$(APP_DIR)/%.o: %.s Makefile
	@mkdir -p $(dir $@)
	$(CC) $(MCU) -x assembler-with-cpp -c $< -o $@

$(BOOT_DIR)/%.o: %.s Makefile
	@mkdir -p $(dir $@)
	$(CC) $(MCU) -x assembler-with-cpp -c $< -o $@

$(UPD_DIR)/%.o: %.c Makefile
	@mkdir -p $(dir $@)
	$(CC) $(UPD_CFLAGS) -c $< -o $@

$(UPD_DIR)/%.o: %.s Makefile
	@mkdir -p $(dir $@)
	$(CC) $(MCU) -x assembler-with-cpp -c $< -o $@

# The bootloader image goes inside the updater.
$(UPD_DIR)/bootloader/image.o: bootloader/image.S $(BOOT_ELF:.elf=.bin) Makefile
	@mkdir -p $(dir $@)
	$(CC) $(MCU) -x assembler-with-cpp -c $< -o $@

$(APP_ELF): $(APP_OBJS) linker/app.ld linker/sections.ld
	$(CC) $(APP_OBJS) $(LDFLAGS_COMMON) -Tapp.ld -Wl,-Map=$(BUILD)/navbridge-cm4-mcu.map -o $@
	$(SIZE) $@

$(BOOT_ELF): $(BOOT_OBJS) linker/bootloader.ld linker/sections.ld
	$(CC) $(BOOT_OBJS) $(LDFLAGS_COMMON) -Tbootloader.ld \
		-Wl,-Map=$(BUILD)/navbridge-cm4-bootloader.map -o $@
	$(SIZE) $@

$(UPD_ELF): $(UPD_OBJS) linker/app.ld linker/sections.ld
	$(CC) $(UPD_OBJS) $(LDFLAGS_COMMON) -Tapp.ld \
		-Wl,-Map=$(BUILD)/navbridge-cm4-bootloader-update.map -o $@
	$(SIZE) $@

%.bin: %.elf
	$(OBJCOPY) -O binary $< $@

%.hex: %.elf
	$(OBJCOPY) -O ihex $< $@

RELEASE := bin

release: all
	@mkdir -p $(RELEASE)
	cp $(BOOT_ELF:.elf=.bin) $(APP_ELF:.elf=.bin) $(UPD_ELF:.elf=.bin) $(RELEASE)/

clean:
	rm -rf $(BUILD)

-include $(APP_OBJS:.o=.d) $(BOOT_OBJS:.o=.d) $(UPD_OBJS:.o=.d)

.PHONY: all release clean
