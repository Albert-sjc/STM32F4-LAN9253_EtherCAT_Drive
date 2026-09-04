#ifndef TESTS_FAKE_STM32G4XX_HAL_H
#define TESTS_FAKE_STM32G4XX_HAL_H

#include <stdint.h>

/* The production STM32 compiler accepts GNU packed attributes.  MSVC is used
 * only for this host-side contract test, so make that spelling a no-op here. */
#if defined(_MSC_VER) && !defined(__attribute__)
#define __attribute__(value)
#endif

typedef struct { void *Instance; } SPI_HandleTypeDef;
typedef struct { void *Instance; } TIM_HandleTypeDef;
typedef struct { uint32_t identity; } GPIO_TypeDef;

typedef enum
{
    HAL_OK = 0,
    HAL_ERROR = 1,
    HAL_BUSY = 2,
    HAL_TIMEOUT = 3
} HAL_StatusTypeDef;

typedef enum
{
    GPIO_PIN_RESET = 0,
    GPIO_PIN_SET
} GPIO_PinState;

HAL_StatusTypeDef HAL_SPI_Transmit(
    SPI_HandleTypeDef *hspi,
    uint8_t *data,
    uint16_t size,
    uint32_t timeout);

HAL_StatusTypeDef HAL_SPI_TransmitReceive(
    SPI_HandleTypeDef *hspi,
    uint8_t *txData,
    uint8_t *rxData,
    uint16_t size,
    uint32_t timeout);

HAL_StatusTypeDef HAL_TIM_Base_Start_IT(TIM_HandleTypeDef *htim);
HAL_StatusTypeDef HAL_TIM_Base_Stop_IT(TIM_HandleTypeDef *htim);

void HAL_GPIO_WritePin(
    GPIO_TypeDef *port,
    uint16_t pin,
    GPIO_PinState state);

#endif
