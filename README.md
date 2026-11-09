# GateMCU

GateMCU turns an ESP8266 NodeMCU into a small WireGuard gateway for devices on a home network. Configure the device from its web interface, connect it to your VPN server, and forward TCP ports to services on your LAN.

## Install from a browser

Use the [GateMCU web installer](https://jurnest.github.io/GateMCU/) to flash a NodeMCU over USB.

1. Open the installer in desktop Chrome or Microsoft Edge. The browser must support Web Serial; Safari is not supported.
2. Connect the NodeMCU with a USB data cable and select its serial port. It may appear as `USB2.0-Serial`, `CH340`, or `CP210x` with a COM port number.
3. Choose **Install GateMCU**, confirm the device, and wait for flashing to finish.
4. Connect to the `GateMCU-Setup` Wi-Fi network. Its default password is `GateMCU-Setup`.
5. If the setup page does not open automatically, visit [http://192.168.4.1](http://192.168.4.1). Enter your Wi-Fi credentials and choose a GateMCU password.
6. After the device connects to your home network, open the IP address shown on the setup page.

Installing or reinstalling firmware can erase saved device settings. Save any configuration you need before flashing.

## Configure GateMCU

Sign in with the password chosen during setup; there is no username.

- **WireGuard:** enter a client configuration and activate or deactivate the tunnel.
- **Port forwarding:** add, edit, or delete up to three TCP rules. Each rule maps a port on the VPN address to an IPv4 address and port on your home network.
- **Event log:** view up to 24 recent TCP forwarding events, held in RAM for up to 30 minutes.
- **Settings:** change Wi-Fi and IP configuration, update the GateMCU password, or restart the device.

To clear saved settings and return to initial setup, hold the NodeMCU **FLASH** button for 5–7 seconds, then release it. The onboard LED blinks three times before restart.

## Build and flash from source

The GitHub release workflow uses ESP8266 RTOS SDK v3.4 and the existing Make-based build system. For a local build, install that SDK and its Xtensa LX106 toolchain, then set `IDF_PATH` to the SDK directory:

```sh
source "$IDF_PATH/export.sh"
make defconfig
make
```

To build and flash over USB, specify your serial port if needed:

```sh
make flash ESPPORT=/dev/ttyUSB0
```

On Windows, the port can be specified as `ESPPORT=COM3` when using a compatible SDK shell.

## Releases and GitHub Pages

Push a version tag such as `v0.1.0` to build a release. GitHub Actions compiles the firmware, uploads the firmware and flash components to GitHub Releases, generates the ESP Web Tools manifest from the build outputs, and publishes the installer site.

Enable **Settings → Pages → Source → GitHub Actions**. In **Settings → Environments → github-pages**, allow deployment tags matching `v*` so version releases can publish the installer.

## License

GateMCU is licensed under the Apache License 2.0; see [`LICENSE`](LICENSE).
