#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# Flash the NavBridge CM4 MCU (STM32C071) from the Raspberry Pi, over USB.
#
#   mcu-flash.sh                        firmware update, or first install on a blank MCU
#   mcu-flash.sh --bootloader-update    replace the NavBridge bootloader, then the firmware
#
# The images come from firmware/bin/ in this repository. Other files can be given:
#   mcu-flash.sh [--bootloader-update] [firmware.bin [bootloader.bin [updater.bin]]]
#
# What it does, depending on what the MCU shows on USB:
#   - Blank MCU, straight from the factory: ST's ROM bootloader (0483:df11). The script
#     installs the NavBridge bootloader through it, then the firmware.
#   - Running firmware (serial port below): it is asked to hand over to the NavBridge
#     bootloader (DFU command), which then receives the firmware.
#   - Already in the NavBridge bootloader (0483:5741: BOOT + RESET, or an interrupted
#     update): the firmware is written.
#   - --bootloader-update: the bootloader updater (a firmware carrying the new bootloader)
#     is installed first; it rewrites the bootloader and hands over to it. Do this with the
#     board on USB-C.
# The firmware is then started and its version printed.
#
# Needs dfu-util (sudo apt install dfu-util). dfu-util runs through sudo when the USB device
# is not writable by the current user (the usual case), so the password is asked for.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
BIN_DIR="$HERE/../firmware/bin"

UPDATE_BOOTLOADER=0
if [ "${1:-}" = "--bootloader-update" ]; then
    UPDATE_BOOTLOADER=1
    shift
fi
FIRMWARE=${1:-$BIN_DIR/navbridge-cm4-mcu.bin}
BOOTLOADER=${2:-$BIN_DIR/navbridge-cm4-bootloader.bin}
UPDATER=${3:-$BIN_DIR/navbridge-cm4-bootloader-update.bin}

ST_DFU=0483:df11  # ST's ROM bootloader (blank chip)
NB_DFU=0483:5741  # NavBridge bootloader
CDC=${NAVBRIDGE_MCU_TTY:-/dev/serial/by-id/usb-NavBridge_NavBridge_CM4_MCU-if00}

die() {
    echo "mcu-flash: $*" >&2
    exit 1
}

# wait_for SECONDS COMMAND...: run COMMAND every 0.1 s until it succeeds.
wait_for() {
    local tenths=$(($1 * 10))
    shift
    for ((i = 0; i < tenths; i++)); do
        if "$@" >/dev/null 2>&1; then
            return 0
        fi
        sleep 0.1
    done
    return 1
}

# Attribute of the NavBridge bootloader's USB device: devnum (new at each start), bcdDevice
# (its version).
nb_attr() {
    local d
    for d in /sys/bus/usb/devices/*; do
        if [ "$(cat "$d/idVendor" 2>/dev/null)" = 0483 ] && [ "$(cat "$d/idProduct" 2>/dev/null)" = 5741 ]; then
            cat "$d/$1"
            return 0
        fi
    done
    return 1
}

# USB device node (/dev/bus/usb/BBB/DDD) of the device VID:PID, from sysfs.
usb_node() {
    local d
    for d in /sys/bus/usb/devices/*; do
        if [ "$(cat "$d/idVendor" 2>/dev/null):$(cat "$d/idProduct" 2>/dev/null)" = "$1" ]; then
            printf '/dev/bus/usb/%03d/%03d\n' "$(cat "$d/busnum")" "$(cat "$d/devnum")"
            return 0
        fi
    done
    return 1
}

# dfu -d VID:PID ...: dfu-util, through sudo when the device is not writable by this user
# (sudo then asks for the password).
dfu() {
    local node
    node=$(usb_node "$2") || node=
    if [ "$(id -u)" -ne 0 ] && { [ -z "$node" ] || [ ! -w "$node" ]; }; then
        sudo dfu-util "$@"
    else
        dfu-util "$@"
    fi
}

[ -f "$FIRMWARE" ] || die "$FIRMWARE: no such file"
command -v dfu-util >/dev/null || die "dfu-util not found: sudo apt install dfu-util"

if lsusb -d "$ST_DFU" >/dev/null 2>&1; then
    [ -f "$BOOTLOADER" ] || die "blank MCU: $BOOTLOADER is needed"
    echo "Blank MCU: installing the NavBridge bootloader"
    # ":leave" starts it; dfu-util may report an error on the final status request, because
    # the device has already left. The bootloader showing up decides.
    dfu -d "$ST_DFU" -a 0 -s 0x08000000:leave -D "$BOOTLOADER" || true
    wait_for 10 lsusb -d "$NB_DFU" || die "the NavBridge bootloader did not show up"
    UPDATE_BOOTLOADER=0  # just installed
elif ! lsusb -d "$NB_DFU" >/dev/null 2>&1; then
    [ -e "$CDC" ] || die "MCU not found: no $CDC, no DFU device"
    echo "Handing over to the bootloader"
    stty -F "$CDC" raw -echo
    printf 'DFU\n' >"$CDC"
    wait_for 10 lsusb -d "$NB_DFU" || die "the bootloader did not show up"
fi

if [ "$UPDATE_BOOTLOADER" = 1 ]; then
    [ -f "$UPDATER" ] || die "$UPDATER: no such file"
    old_dev=$(nb_attr devnum)
    echo "Bootloader version $(nb_attr bcdDevice): installing the bootloader updater"
    dfu -d "$NB_DFU" -a 0 -D "$UPDATER" || true
    # The updater rewrites the bootloader, then the new one shows up (new device number).
    new_bootloader() {
        local dev
        dev=$(nb_attr devnum) && [ "$dev" != "$old_dev" ]
    }
    wait_for 20 new_bootloader || die "the new bootloader did not show up (power-cycle the board: ST's DFU then starts, and this script reinstalls everything)"
    echo "Bootloader version $(nb_attr bcdDevice) installed"
fi

echo "Writing $FIRMWARE"
rc=0
# The bootloader starts the firmware once the download is complete and checked.
dfu -d "$NB_DFU" -a 0 -D "$FIRMWARE" || rc=$?

echo "Waiting for the firmware"
if ! wait_for 10 test -e "$CDC"; then
    lsusb -d "$NB_DFU" >/dev/null 2>&1 && die "still in the bootloader (dfu-util exit code $rc): run again"
    die "the firmware did not come up on USB (dfu-util exit code $rc)"
fi
sleep 0.5
stty -F "$CDC" raw -echo
exec 3<>"$CDC"
printf 'VERSION\n' >&3
# --foreground: grep stays in the script's process group. Run without a terminal (ssh host
# command), the script's shell can own the port as its controlling terminal, and a read from
# another process group would be stopped (SIGTTIN).
version=$(timeout --foreground 2 grep -a -m1 '^VERSION' <&3 | tr -d '\r' || true)
exec 3>&-
echo "${version:-Firmware is up (no VERSION reply: another program may be reading $CDC)}"
