# GateMCU

GateMCU is an ESP8266 home gateway with a WireGuard client and LAN port forwarding.

ESP8266 firmware for connecting a home LAN to a WireGuard server and forwarding selected ports. The WireGuard protocol comes from upstream `trombik/esp_wireguard` (BSD-3-Clause), pinned as a Git submodule at v0.9.0 in `components/esp_wireguard`. That implementation supports ESP8266 RTOS SDK v3.4 and identifies itself as alpha.

## Current firmware milestone

- First boot starts the `GateMCU-Setup` access point. Its Wi-Fi password is `GateMCU-Setup`. Connect to that network, then use the automatically opened setup page or browse to `http://192.168.4.1`; captive DNS and common phone connectivity-check redirects try to open it automatically.
- The setup page is minimal and responsive, defaults to English, and has a persistent TR/EN selector. It keeps the setup AP available while trying Wi-Fi, shows an error on failure, and shows the DHCP address on success before closing the setup AP.
- On the home network, the management page is available by IP and at `http://gatemcu.local` (mDNS). The dark dashboard has separate Overview, WireGuard, Forwarding and Settings pages.
- First setup asks for the Wi-Fi network name (SSID), that network's password, and a separate admin password for GateMCU. Later visits use an in-page password login and a session cookie; there is no username or browser Basic Auth prompt. Settings includes Wi-Fi changes, password changes, device addresses and a non-destructive restart action. Language can be changed from the navigation bar.
- HTTP request header capacity is set to 1024 bytes to accommodate browser requests.
- Hold the NodeMCU **FLASH** button (GPIO0) for 5–7 seconds, then release while the firmware is running to erase saved gateway settings. The onboard LED blinks three times before the board restarts into setup mode. Do not power on the board while holding FLASH; GPIO0 also selects the ESP8266 boot mode.
- The dark dashboard accepts a pasted WireGuard `.conf`, reads the private key, peer public key, optional preshared key, endpoint, tunnel IPv4 address/prefix and keepalive. It saves them to NVS, syncs time and starts the upstream WireGuard client. The DNS and AllowedIPs lines are not applied by this ESP8266 build.
- Up to three TCP forwarding rules can be saved. Each listens on the WireGuard tunnel IP and proxies to one IPv4 address and port on the home LAN. UDP forwarding is not implemented.

## Source layout

```text
main/
  include/   public gateway interfaces
  src/
    app/       application startup
    network/   Wi-Fi, captive DNS and TCP forwarding
    security/  password and session credential storage
    system/    reset controls
    vpn/       WireGuard lifecycle
    web/       HTTP pages, API handlers, sessions and routing
components/
  esp_wireguard/  upstream WireGuard component (Git submodule, v0.9.0)
```

## Build prerequisites

- ESP8266 RTOS SDK v3.4 and Xtensa LX106 toolchain
- `IDF_PATH` set to the SDK directory and toolchain available on `PATH`
- NodeMCU ESP8266 board connected over USB

```sh
make menuconfig
make
make flash
```

## Current limitations

- The dashboard is served over HTTP. Access it through the home LAN or the encrypted WireGuard tunnel; do not expose it directly to the public internet.
- UDP forwarding is not implemented.
