#ifndef STM32_LAN9253_DEVICE_COMPATIBILITY_H
#define STM32_LAN9253_DEVICE_COMPATIBILITY_H

/*
 * STM32G4 port compatibility header.
 *
 * The original Microchip LAN9253 driver includes <device.h> to obtain the
 * platform integer types, size_t, bool and peripheral declarations.  This
 * project intentionally keeps the vendor driver sources unchanged, so this
 * small header provides the same include entry point for STM32G4 builds.
 *
 * Board pin assignments and peripheral handles are not defined here.  They
 * are supplied at run time through STM32_LAN9253_INTERFACE_CONFIG.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "stm32g4xx_hal.h"

#endif /* STM32_LAN9253_DEVICE_COMPATIBILITY_H */

