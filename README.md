# GateMCU

GateMCU turns an ESP8266 NodeMCU into a small WireGuard-connected gateway for devices on a home network. It provides a web interface for Wi-Fi, WireGuard, TCP port-forwarding rules and recent connection activity.

## First-time setup

1. Power on the NodeMCU and connect to the `GateMCU-Setup` Wi-Fi network. Its password is `GateMCU-Setup`.
2. Open `http://192.168.4.1` if the setup page does not open automatically.
3. Enter the home Wi-Fi name and password, then choose an admin password for GateMCU.
4. After it connects, open the device’s IP address shown on the setup page. GateMCU keeps its saved network settings across restarts.

The admin password is used in the page’s login form. There is no username.

## Web interface

- **Overview** shows Wi-Fi and WireGuard status, device memory and storage use, and the number of forwarding rules.
- **WireGuard** accepts a client `.conf` configuration and controls the tunnel.
- **Port forwarding** adds, edits and deletes up to three TCP rules. Each rule maps a port on the WireGuard address to an IPv4 address and port on the home LAN.
- **Event log** shows recent TCP forwarding connections, results, transferred data and transfer time. It is stored in RAM and holds up to 24 events from the last 30 minutes.
- **Settings** changes Wi-Fi and IP settings, the admin password, and restarts the device.

To clear saved settings and return to setup, hold the NodeMCU **FLASH** button for 5–7 seconds, then release it. The onboard LED blinks three times before restart.

## Build and flash

Requirements:

- ESP8266 RTOS SDK v3.4
- Xtensa LX106 toolchain
- NodeMCU ESP8266 connected over USB

Set `IDF_PATH` to the SDK directory, ensure the toolchain is on `PATH`, then run:

```sh
make menuconfig
make
make flash
```

## License

GateMCU is licensed under the Apache License 2.0; see [`LICENSE`](LICENSE).
