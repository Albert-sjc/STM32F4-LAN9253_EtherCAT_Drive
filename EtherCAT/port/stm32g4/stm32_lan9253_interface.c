#include "stm32_lan9253_interface.h"

#include <limits.h>
#include <string.h>

/* Existing driver entry point; kept as a declaration to avoid another
 * dependency from the STM32 adapter back into the vendor utility header. */
extern void ECAT_Util_Initialize(
    const unsigned short int drvIndex,
    const void * const init);

typedef struct
{
    EIC_CALLBACK callback;
    uintptr_t context;
} STM32_LAN9253_EIC_CALLBACK_OBJECT;

#define STM32_LAN9253_EIC_CHANNEL_COUNT 8U
#define STM32_LAN9253_DUMMY_BUFFER_SIZE  32U

static STM32_LAN9253_INTERFACE_CONFIG interfaceConfig;
static bool interfaceInitialized;
static volatile bool spiBusy;

static DRV_LAN9253_SPI_PLIB_CALLBACK spiCallback;
static uintptr_t spiCallbackContext;

static DRV_LAN9253_TMR_PLIB_CALLBACK timerCallback;
static uintptr_t timerCallbackContext;

static STM32_LAN9253_EIC_CALLBACK_OBJECT
    eicCallbacks[STM32_LAN9253_EIC_CHANNEL_COUNT];

static bool STM32_LAN9253_CompleteSPI(HAL_StatusTypeDef status)
{
    spiBusy = false;

    /* drv_lan9253_ecat_util.c waits on a status changed by this callback.
     * Notify on errors too, otherwise its synchronous wait can deadlock. */
    if (spiCallback != NULL)
    {
        spiCallback(spiCallbackContext);
    }

    return status == HAL_OK;
}

static HAL_StatusTypeDef STM32_LAN9253_ReadBytes(
    uint8_t *data,
    size_t size)
{
    uint8_t dummy[STM32_LAN9253_DUMMY_BUFFER_SIZE];
    HAL_StatusTypeDef status = HAL_OK;

    memset(dummy, 0xFF, sizeof(dummy));

    while (size > 0U)
    {
        uint16_t chunk = (uint16_t)(
            size > sizeof(dummy) ? sizeof(dummy) : size);

        status = HAL_SPI_TransmitReceive(
            interfaceConfig.spi,
            dummy,
            data,
            chunk,
            interfaceConfig.spiTimeoutMs);

        if (status != HAL_OK)
        {
            break;
        }

        data += chunk;
        size -= chunk;
    }

    return status;
}

static bool STM32_LAN9253_SPI_WriteRead(
    void *txData,
    size_t txSize,
    void *rxData,
    size_t rxSize)
{
    HAL_StatusTypeDef status = HAL_OK;
    size_t simultaneousSize;

    if (!interfaceInitialized ||
        ((txSize > 0U) && (txData == NULL)) ||
        ((rxSize > 0U) && (rxData == NULL)) ||
        (txSize > UINT16_MAX) ||
        (rxSize > UINT16_MAX) ||
        ((txSize == 0U) && (rxSize == 0U)))
    {
        return STM32_LAN9253_CompleteSPI(HAL_ERROR);
    }

    spiBusy = true;
    simultaneousSize = txSize < rxSize ? txSize : rxSize;

    if (simultaneousSize > 0U)
    {
        status = HAL_SPI_TransmitReceive(
            interfaceConfig.spi,
            (uint8_t *)txData,
            (uint8_t *)rxData,
            (uint16_t)simultaneousSize,
            interfaceConfig.spiTimeoutMs);
    }

    if ((status == HAL_OK) && (txSize > simultaneousSize))
    {
        status = HAL_SPI_Transmit(
            interfaceConfig.spi,
            (uint8_t *)txData + simultaneousSize,
            (uint16_t)(txSize - simultaneousSize),
            interfaceConfig.spiTimeoutMs);
    }

    if ((status == HAL_OK) && (rxSize > simultaneousSize))
    {
        status = STM32_LAN9253_ReadBytes(
            (uint8_t *)rxData + simultaneousSize,
            rxSize - simultaneousSize);
    }

    return STM32_LAN9253_CompleteSPI(status);
}

static bool STM32_LAN9253_SPI_Write(void *data, size_t size)
{
    HAL_StatusTypeDef status;

    if (!interfaceInitialized || (data == NULL) ||
        (size == 0U) || (size > UINT16_MAX))
    {
        return STM32_LAN9253_CompleteSPI(HAL_ERROR);
    }

    spiBusy = true;
    status = HAL_SPI_Transmit(
        interfaceConfig.spi,
        (uint8_t *)data,
        (uint16_t)size,
        interfaceConfig.spiTimeoutMs);

    return STM32_LAN9253_CompleteSPI(status);
}

static bool STM32_LAN9253_SPI_Read(void *data, size_t size)
{
    HAL_StatusTypeDef status;

    if (!interfaceInitialized || (data == NULL) || (size == 0U))
    {
        return STM32_LAN9253_CompleteSPI(HAL_ERROR);
    }

    spiBusy = true;
    status = STM32_LAN9253_ReadBytes((uint8_t *)data, size);
    return STM32_LAN9253_CompleteSPI(status);
}

static bool STM32_LAN9253_SPI_IsBusy(void)
{
    return spiBusy;
}

static void STM32_LAN9253_SPI_CallbackRegister(
    DRV_LAN9253_SPI_PLIB_CALLBACK callback,
    uintptr_t context)
{
    spiCallback = callback;
    spiCallbackContext = context;
}

static void STM32_LAN9253_TimerCallbackSet(
    DRV_LAN9253_TMR_PLIB_CALLBACK callback,
    uintptr_t context)
{
    timerCallback = callback;
    timerCallbackContext = context;
}

static void STM32_LAN9253_TimerStart(void)
{
    if (interfaceInitialized)
    {
        (void)HAL_TIM_Base_Start_IT(interfaceConfig.timer);
    }
}

static void STM32_LAN9253_TimerStop(void)
{
    if (interfaceInitialized)
    {
        (void)HAL_TIM_Base_Stop_IT(interfaceConfig.timer);
    }
}

static const DRV_LAN9253_UTIL_SPI_PLIB_INTERFACE stm32SpiPlib =
{
    STM32_LAN9253_SPI_WriteRead,
    STM32_LAN9253_SPI_Write,
    STM32_LAN9253_SPI_Read,
    STM32_LAN9253_SPI_IsBusy,
    STM32_LAN9253_SPI_CallbackRegister
};

static const DRV_LAN9253_UTIL_TMR_PLIB_INTERFACE stm32TimerPlib =
{
    STM32_LAN9253_TimerCallbackSet,
    STM32_LAN9253_TimerStart,
    STM32_LAN9253_TimerStop
};

static const DRV_LAN9253_UTIL_INIT stm32Lan9253InitData =
{
    &stm32SpiPlib,
    &stm32TimerPlib
};

bool STM32_LAN9253_InterfaceInit(
    const STM32_LAN9253_INTERFACE_CONFIG *config)
{
    if ((config == NULL) || (config->spi == NULL) ||
        (config->timer == NULL) || (config->csPort == NULL) ||
        (config->spiTimeoutMs == 0U))
    {
        return false;
    }

    interfaceConfig = *config;
    memset(eicCallbacks, 0, sizeof(eicCallbacks));
    spiCallback = NULL;
    timerCallback = NULL;
    spiBusy = false;
    interfaceInitialized = true;

    /* Keep LAN9253 deselected before the original driver starts probing it. */
    HAL_GPIO_WritePin(
        interfaceConfig.csPort,
        interfaceConfig.csPin,
        GPIO_PIN_SET);

    ECAT_Util_Initialize(0U, &stm32Lan9253InitData);
    return true;
}

void EIC_CallbackRegister(
    EIC_PIN pin,
    EIC_CALLBACK callback,
    uintptr_t context)
{
    uint32_t channel = (uint32_t)pin;

    if (channel < STM32_LAN9253_EIC_CHANNEL_COUNT)
    {
        eicCallbacks[channel].callback = callback;
        eicCallbacks[channel].context = context;
    }
}

static void STM32_LAN9253_InvokeEIC(EIC_PIN pin)
{
    STM32_LAN9253_EIC_CALLBACK_OBJECT *object =
        &eicCallbacks[(uint32_t)pin];

    if (object->callback != NULL)
    {
        object->callback(object->context);
    }
}

void STM32_LAN9253_EXTI_Dispatch(uint16_t gpioPin)
{
    if (!interfaceInitialized)
    {
        return;
    }

    if ((interfaceConfig.sync0Pin != STM32_LAN9253_UNUSED_PIN) &&
        (gpioPin == interfaceConfig.sync0Pin))
    {
        STM32_LAN9253_InvokeEIC(EIC_PIN_0);
    }
    else if ((interfaceConfig.sync1Pin != STM32_LAN9253_UNUSED_PIN) &&
             (gpioPin == interfaceConfig.sync1Pin))
    {
        STM32_LAN9253_InvokeEIC(EIC_PIN_1);
    }
    else if (gpioPin == interfaceConfig.escIrqPin)
    {
        STM32_LAN9253_InvokeEIC(EIC_PIN_7);
    }
}

void STM32_LAN9253_TimerDispatch(TIM_HandleTypeDef *timer)
{
    if (interfaceInitialized && (timer != NULL) &&
        (timerCallback != NULL) &&
        (timer->Instance == interfaceConfig.timer->Instance))
    {
        timerCallback(timerCallbackContext);
    }
}

void PORT_PinSet(PORT_PIN pin)
{
    if (!interfaceInitialized)
    {
        return;
    }

    if (pin == PORT_PIN_PB11)
    {
        HAL_GPIO_WritePin(
            interfaceConfig.csPort,
            interfaceConfig.csPin,
            GPIO_PIN_SET);
    }
    else if ((pin == PORT_PIN_PB31) &&
             (interfaceConfig.errorLedPort != NULL))
    {
        HAL_GPIO_WritePin(
            interfaceConfig.errorLedPort,
            interfaceConfig.errorLedPin,
            GPIO_PIN_SET);
    }
}

void PORT_PinClear(PORT_PIN pin)
{
    if (!interfaceInitialized)
    {
        return;
    }

    if (pin == PORT_PIN_PB11)
    {
        HAL_GPIO_WritePin(
            interfaceConfig.csPort,
            interfaceConfig.csPin,
            GPIO_PIN_RESET);
    }
    else if ((pin == PORT_PIN_PB31) &&
             (interfaceConfig.errorLedPort != NULL))
    {
        HAL_GPIO_WritePin(
            interfaceConfig.errorLedPort,
            interfaceConfig.errorLedPin,
            GPIO_PIN_RESET);
    }
}

