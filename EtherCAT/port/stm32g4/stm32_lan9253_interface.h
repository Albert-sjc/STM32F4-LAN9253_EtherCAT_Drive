#ifndef STM32_LAN9253_INTERFACE_H
#define STM32_LAN9253_INTERFACE_H

/*
 * STM32G4 hardware adapter for the unmodified Microchip LAN9253 driver.
 *
 * The adapter implements the SPI and timer function-pointer contracts from
 * drv_lan9253_definitions.h.  It also exposes small EIC and PORT compatibility
 * APIs used directly by drv_lan9253_ecat_util.c.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../../driver/lan9253/drv_lan9253_definitions.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Use for an optional EXTI signal that is not connected on the board. */
#define STM32_LAN9253_UNUSED_PIN ((uint16_t)0xFFFFU)

typedef struct
{
    SPI_HandleTypeDef *spi;
    TIM_HandleTypeDef *timer;

    GPIO_TypeDef *csPort;
    uint16_t csPin;

    /* Optional. Set errorLedPort to NULL when no MCU-driven Error LED exists. */
    GPIO_TypeDef *errorLedPort;
    uint16_t errorLedPin;

    /* GPIO pin masks passed by HAL_GPIO_EXTI_Callback(). */
    uint16_t sync0Pin;
    uint16_t sync1Pin;
    uint16_t escIrqPin;

    uint32_t spiTimeoutMs;
} STM32_LAN9253_INTERFACE_CONFIG;

/*
 * Stores the board configuration and injects the STM32 SPI/timer tables into
 * the existing driver through ECAT_Util_Initialize().
 */
bool STM32_LAN9253_InterfaceInit(
    const STM32_LAN9253_INTERFACE_CONFIG *config);

/* Forward these from the corresponding Cube HAL weak callbacks. */
void STM32_LAN9253_EXTI_Dispatch(uint16_t gpioPin);
void STM32_LAN9253_TimerDispatch(TIM_HandleTypeDef *timer);

/* Logical Harmony EIC channels retained for source compatibility. */
typedef enum
{
    EIC_PIN_0 = 0, /* LAN9253 SYNC0 */
    EIC_PIN_1 = 1, /* LAN9253 SYNC1 */
    EIC_PIN_7 = 7  /* LAN9253 ESC IRQ */
} EIC_PIN;

typedef void (*EIC_CALLBACK)(uintptr_t context);

void EIC_CallbackRegister(
    EIC_PIN pin,
    EIC_CALLBACK callback,
    uintptr_t context);

/*
 * Logical Harmony PORT identifiers retained for source compatibility only.
 * They do not describe the physical STM32 port/pin selected by CubeMX.
 */
typedef enum
{
    PORT_PIN_PB11 = 0, /* LAN9253 SPI chip select */
    PORT_PIN_PB31 = 1  /* Optional MCU-driven Error LED */
} PORT_PIN;

void PORT_PinSet(PORT_PIN pin);
void PORT_PinClear(PORT_PIN pin);

#ifdef __cplusplus
}
#endif

#endif /* STM32_LAN9253_INTERFACE_H */

