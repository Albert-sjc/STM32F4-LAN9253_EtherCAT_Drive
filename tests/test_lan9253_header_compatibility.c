#include "drv_lan9253_ecat_util.h"

/* Compile-only check: the unmodified vendor header must resolve its original
 * device.h and definitions.h includes through the STM32 compatibility files. */
int main(void)
{
    DRV_LAN9253_UTIL_INIT init = {0};
    STM32_LAN9253_INTERFACE_CONFIG config = {0};

    (void)init;
    (void)config;
    return 0;
}
