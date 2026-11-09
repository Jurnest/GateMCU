COMPONENT_SRCDIRS := src/app src/network src/vpn src/system src/security src/web
COMPONENT_ADD_INCLUDEDIRS := include src/web
COMPONENT_REQUIRES := esp_wifi esp_http_server nvs_flash lwip esp_wireguard mdns mbedtls esp8266 spi_flash bootloader_support heap
