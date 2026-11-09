#include "gateway.h"

void app_main(void)
{
    gateway_wifi_init();
    setup_web_start();
    captive_dns_start();
    reset_button_start();
}
