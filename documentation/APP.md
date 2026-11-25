# Install the NavBridge App

[NavBridge App](https://github.com/DrZedEau/NavBridge_App) is the Android Auto head unit made for this board. It reads the car's I-Bus through the MCU and plays its audio through the board's PCM2912A codec, and its default configuration already matches the board.

> [!NOTE]
> There are no prebuilt packages yet: for now the app is built on the Pi from source. Packages will be added soon.

Do this after [Set up the Pi and flash the MCU](SETUP.md), over SSH, as your normal user (not `root`).

## 1. Get the source and build it

```bash
cd ~
git clone https://github.com/DrZedEau/NavBridge_App.git
cd NavBridge_App
./scripts/install_build_deps.sh
./scripts/build_kms.sh
```

`install_build_deps.sh` asks for your password to install packages, and installs Rust for your account. Build without `sudo`. The first build takes a while.

To try it on the display before installing it:

```bash
./scripts/run_kms.sh
```

Stop it with Ctrl+C.

## 2. Install it as a service

```bash
./scripts/install_systemd.sh
./scripts/install_splash.sh
sudo reboot
```

`install_systemd.sh` installs the app in `/opt/navbridge`, with its two services (`navbridge-core` and `navbridge-ui`), and starts it at every boot on the display. `install_splash.sh` shows the NavBridge splash screen during the boot.

The configuration is in `/opt/navbridge/config/core.toml`. On this board, the I-Bus port (`[ibus] device`) and the audio devices (`[audio]`) are already right.

## Use it

- **Wireless Android Auto:** turn it on in Settings, then pair the phone over Bluetooth with the code shown on screen. The phone then connects over the Pi's own 5 GHz Wi-Fi hotspot. While the hotspot is on, the Pi is not on your home Wi-Fi.
- **Logs:** `journalctl -u navbridge-core -u navbridge-ui -f`
- **Restart:** `sudo systemctl restart navbridge-core navbridge-ui`

See the [NavBridge App README](https://github.com/DrZedEau/NavBridge_App#readme) for all the settings.

## Update

```bash
cd ~/NavBridge_App
git pull
./scripts/build_kms.sh
./scripts/install_systemd.sh
```

The installer keeps your configuration in `/opt/navbridge/config`.

Next: [Install in the car](NAV_CABLE.md#install-in-the-car).
