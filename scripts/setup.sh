#!/bin/bash
#
# Initial setup of a Raspberry Pi CM4 or CM5 on the NavBridge_CM4 0.2 board
#
# The script:
# - Improves boot time and removes unused packages
# - Updates the system and installs required packages
# - Updates config.txt (enables display 400*240p @60HZ and I2C for MFi Chip)
# - Loads the i2c-dev kernel module
# - Ensures that Bluetooth is not blocked by rfkill

set -euo pipefail

RED="\e[31m"
GREEN="\e[32m"
YELLOW="\e[33m"
CYAN="\e[36m"
BOLD="\e[1m"
RESET="\e[0m"

section() {
    echo -e "\n${BOLD}${CYAN}================================================================${RESET}"
    echo -e "${BOLD}${CYAN}$1${RESET}"
    echo -e "${BOLD}${CYAN}================================================================${RESET}\n"
}

info()    { echo -e "${CYAN}${BOLD}➡  $1${RESET}"; }
ok()      { echo -e "${GREEN}${BOLD}✓ $1${RESET}"; }
warn()    { echo -e "${YELLOW}${BOLD}! $1${RESET}"; }
error()   { echo -e "${RED}${BOLD}✗ $1${RESET}"; }

MFI_IC_DEV=/dev/i2c-10
REQUIRED_PACKAGES=(
    git
    vim
    dfu-util
    rfkill
)
UNUSED_PACKAGES=(
    cloud-init
)
I2C_MODULES_FILE="/etc/modules-load.d/i2c-dev.conf"
CONFIG_FILE="/boot/firmware/config.txt"
CUSTOM_CONFIG=$(cat <<'EOF'
#############################################
# Custom config file for NavBridge_CM4 board
# https://github.com/DrZedEau/NavBridge_CM4
#############################################

[all]

dtparam=audio=off
camera_auto_detect=0
display_auto_detect=0
auto_initramfs=1
dtoverlay=vc4-kms-v3d
max_framebuffers=2
disable_fw_kms_setup=1
arm_64bit=1
disable_overscan=1
arm_boost=1

dtoverlay=vc4-kms-dpi-generic
dtparam=clock-frequency=8000000
dtparam=hactive=400,hfp=8,hsync=40,hbp=60
dtparam=vactive=240,vfp=3,vsync=3,vbp=16
dtparam=rgb888
dtparam=hsync-invert,vsync-invert
dtparam=pixclk-invert

[cm4]
otg_mode=1
dtparam=i2c_vc=on

[cm5]
dtoverlay=nospi10
dtoverlay=dwc2,dr_mode=host
dtparam=i2c_csi_dsi0=on

[all]

EOF
)

require_root() {
    if [[ "$EUID" -ne 0 ]]; then
        error "Run this script as root or with sudo."
        exit 1
    fi
}

backup() {
    local backup_file
    backup_file="$1.$(date +%Y%m%d_%H%M%S).bak"
    cp -p "$1" "$backup_file"
    info "Backup: $backup_file"
}

improve_boot_time() {
    section "IMPROVE BOOT TIME AND REMOVE UNUSED PACKAGES"

    info "Disable unused services"
    systemctl disable --now NetworkManager-wait-online.service

    info "Remove unused packages"
    apt -y purge "${UNUSED_PACKAGES[@]}"
    apt -y autoremove --purge

    ok "Done."
}

install_packages() {
    section "SYSTEM UPDATE AND PACKAGES INSTALLATION"

    info "Update the system"
    apt -y update
    apt -y upgrade

    info "Install the required packages"
    apt -y install "${REQUIRED_PACKAGES[@]}"

    ok "Done."
}

update_config() {
    section "UPDATE CONFIG.TXT"

    if printf '%s\n' "$CUSTOM_CONFIG" | cmp -s "$CONFIG_FILE" -; then
        ok "$CONFIG_FILE is already up to date."
        return 0
    fi

    backup "$CONFIG_FILE"
    printf '%s\n' "$CUSTOM_CONFIG" > "$CONFIG_FILE"
    ok "$CONFIG_FILE updated. Reboot to apply it."
}

load_i2c_dev() {
    section "LOAD THE I2C-DEV KERNEL MODULE"

    info "Load i2c-dev at every boot"
    if [[ -f "$I2C_MODULES_FILE" ]] && grep -qx 'i2c-dev' "$I2C_MODULES_FILE"; then
        ok "$I2C_MODULES_FILE already loads it."
    else
        echo "i2c-dev" > "$I2C_MODULES_FILE"
        ok "$I2C_MODULES_FILE written."
    fi

    info "Load i2c-dev now"
    modprobe i2c-dev

    if [[ -e "$MFI_IC_DEV" ]]; then
        ok "$MFI_IC_DEV is there (MFi coprocessor)."
    else
        warn "$MFI_IC_DEV is not there yet: it appears after a reboot with the new config.txt."
    fi

    ok "Done."
}

unblock_bluetooth() {
    section "UNBLOCK BLUETOOTH"

    local state
    state=$(rfkill list bluetooth 2>/dev/null) || state=""

    if [[ -z "$state" ]]; then
        warn "No Bluetooth device found: nothing to unblock."
        return 0
    fi

    if grep -q 'Soft blocked: yes' <<< "$state"; then
        info "Unblock Bluetooth"
        rfkill unblock bluetooth
        ok "Bluetooth unblocked. systemd-rfkill keeps it unblocked after a reboot."
    else
        ok "Bluetooth is not soft blocked."
    fi

    if grep -q 'Hard blocked: yes' <<< "$state"; then
        warn "Bluetooth is hard blocked (by a hardware switch or the firmware): rfkill cannot unblock it."
    fi

    ok "Done."
}

main() {
    require_root
    improve_boot_time
    install_packages
    update_config
    load_i2c_dev
    unblock_bluetooth
}

main "$@"
