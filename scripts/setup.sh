#!/bin/bash
#
# Initial setup of a Raspberry Pi CM4 or CM5 on the NavBridge_CM4 0.2 board
#
# The script:
# - Improves boot time and removes unused packages
# - Updates the system and installs required packages
# - Updates config.txt (enables display 400*240p @60HZ and I2C for MFi Chip)
# - Updates cmdline.txt (quiet boot: no messages or cursor on the display, splash screen)
# - Installs the boot splash screen (scripts/splash/splash.tga)
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
    rpi-splash-screen-support
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

CMDLINE_FILE="/boot/firmware/cmdline.txt"
CMDLINE_PARAMS=(
    console=serial0,115200
    console=tty3
    @ROOT@
    rootfstype=ext4
    fsck.repair=yes
    rootwait
    quiet
    loglevel=0
    vt.global_cursor_default=0
    systemd.show_status=false
    rd.systemd.show_status=false
    fullscreen_logo=1
    fullscreen_logo_name=logo.tga
)

SPLASH_IMAGE="splash/splash.tga"
SPLASH_URL="https://raw.githubusercontent.com/DrZedEau/NavBridge_CM4/main/scripts/$SPLASH_IMAGE"
SPLASH_FIRMWARE="/lib/firmware/logo.tga"
SPLASH_HOOK="/etc/initramfs-tools/hooks/splash-screen-hook.sh"


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

update_cmdline() {
    section "UPDATE CMDLINE.TXT"

    local -a current=()
    local param root="" custom
    if [[ -f "$CMDLINE_FILE" ]]; then
        read -r -d '' -a current < <(tr -d '\r' < "$CMDLINE_FILE") || true
    fi
    for param in "${current[@]}"; do
        if [[ "$param" == root=* ]]; then
            root=$param
        fi
    done

    if [[ -z "$root" ]]; then
        error "No root= parameter in $CMDLINE_FILE: it was not changed (the Pi would no longer boot)."
        exit 1
    fi

    custom="${CMDLINE_PARAMS[*]}"
    custom=${custom/@ROOT@/"$root"}

    if printf '%s\n' "$custom" | cmp -s "$CMDLINE_FILE" -; then
        ok "$CMDLINE_FILE is already up to date."
        return 0
    fi

    backup "$CMDLINE_FILE"
    printf '%s\n' "$custom" > "$CMDLINE_FILE"
    info "New command line: $custom"
    ok "$CMDLINE_FILE updated. Reboot to apply it."
}

install_splash_screen() {
    section "INSTALL THE SPLASH SCREEN"

    if ! apt-cache show rpi-splash-screen-support >/dev/null 2>&1; then
        warn "rpi-splash-screen-support is not available on this system: no splash screen."
        return 0
    fi


    local image script_dir tmp_dir=""
    script_dir=$(dirname "${BASH_SOURCE[0]:-}")
    if [[ -f "${BASH_SOURCE[0]:-}" && -f "$script_dir/$SPLASH_IMAGE" ]]; then
        image="$script_dir/$SPLASH_IMAGE"
    else
        tmp_dir=$(mktemp -d)
        image="$tmp_dir/splash.tga"
        info "Download $SPLASH_URL"
        curl -fsSL "$SPLASH_URL" -o "$image"
    fi

    if cmp -s "$image" "$SPLASH_FIRMWARE" && [[ -f "$SPLASH_HOOK" ]]; then
        ok "The splash screen is already installed."
    else
        info "Add the image to the initramfs"
        configure-splash --no-cmdline "$image"
        ok "Splash screen installed ($CMDLINE_FILE already has its parameters). Reboot to see it."
    fi

    if [[ -n "$tmp_dir" ]]; then
        rm -rf "$tmp_dir"
    fi
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
    update_cmdline
    install_splash_screen
    load_i2c_dev
    unblock_bluetooth
}

main "$@"