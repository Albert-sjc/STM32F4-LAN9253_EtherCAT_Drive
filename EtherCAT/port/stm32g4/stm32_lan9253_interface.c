/*******************************************************************************
  STM32G4 LAN9253 Hardware Interface Source File

  File Name:
    stm32_lan9253_interface.c

  Summary:
    Implements the STM32G4 peripheral adapter required by the existing
    LAN9253 EtherCAT driver.

  Description:
    This file connects the LAN9253 driver's SPI and timer function-pointer
    tables to STM32 HAL.  It also provides compatibility implementations for
    the Harmony EIC_CallbackRegister(), PORT_PinSet(), and PORT_PinClear()
    interfaces used directly by drv_lan9253_ecat_util.c.

    SPI transfers are deliberately implemented as blocking transactions.  The
    original LAN9253 utility driver still expects its registered completion
    callback to be invoked, so this adapter invokes that callback after every
    completed HAL operation, including failed operations, to release the
    utility driver's synchronous wait.

    This file does not define the STM32 HAL weak callbacks.  The application
    shall forward its GPIO EXTI and timer callbacks to the dispatch functions
    provided here.
 *******************************************************************************/

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

/*******************************************************************************
  Function:
    static bool STM32_LAN9253_CompleteSPI(HAL_StatusTypeDef status)

  Summary:
    Completes one blocking SPI request and notifies the LAN9253 utility driver.

  Description:
    Clears the adapter busy state and invokes the callback previously supplied
    through spiCallbackRegister().  Notification is also performed when the
    HAL operation failed because drv_lan9253_ecat_util.c waits for this callback
    before it can continue.  The HAL result remains available to the immediate
    caller through the Boolean return value.

  Parameters:
    status - Result returned by the STM32 HAL SPI operation.

  Returns:
    true when status is HAL_OK; otherwise false.
 *******************************************************************************/
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

/*******************************************************************************
  Function:
    static HAL_StatusTypeDef STM32_LAN9253_ReadBytes(uint8_t *data,
                                                      size_t size)

  Summary:
    Generates SPI clocks and receives bytes from LAN9253.

  Description:
    A master-mode SPI peripheral must transmit data to generate receive clocks.
    This routine transmits 0xFF dummy bytes and receives data simultaneously.
    Requests are split into small blocks so no dynamic allocation is required
    and the HAL transfer length remains representable by uint16_t.

  Parameters:
    data - Destination buffer for received bytes.
    size - Number of bytes to receive.

  Returns:
    HAL_OK when all blocks were received; otherwise the first HAL error status.
 *******************************************************************************/
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

/*******************************************************************************
  Function:
    static bool STM32_LAN9253_SPI_WriteRead(void *txData, size_t txSize,
                                             void *rxData, size_t rxSize)

  Summary:
    Implements the LAN9253 SPI PLIB full-duplex transfer contract.

  Description:
    Transfers the common TX/RX length simultaneously.  If one side is longer,
    the remaining transmit bytes are written or the remaining receive bytes
    are clocked with 0xFF dummy data.  Chip select is intentionally not changed
    here; the original LAN9253 driver owns chip select for the complete command.

  Parameters:
    txData - Buffer containing bytes to transmit.
    txSize - Number of transmit bytes.
    rxData - Buffer receiving simultaneously shifted-in bytes.
    rxSize - Number of receive bytes.

  Returns:
    true when the complete transfer succeeds; otherwise false.
 *******************************************************************************/
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

/*******************************************************************************
  Function:
    static bool STM32_LAN9253_SPI_Write(void *data, size_t size)

  Summary:
    Sends bytes to LAN9253 using blocking STM32 HAL SPI.

  Parameters:
    data - Buffer containing bytes to transmit.
    size - Number of bytes to transmit.

  Returns:
    true when HAL_SPI_Transmit() succeeds; otherwise false.
 *******************************************************************************/
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

/*******************************************************************************
  Function:
    static bool STM32_LAN9253_SPI_Read(void *data, size_t size)

  Summary:
    Receives bytes from LAN9253 while transmitting dummy clock bytes.

  Parameters:
    data - Destination buffer for received bytes.
    size - Number of bytes to receive.

  Returns:
    true when all bytes are received; otherwise false.
 *******************************************************************************/
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

/*******************************************************************************
  Function:
    static bool STM32_LAN9253_SPI_IsBusy(void)

  Summary:
    Reports whether the STM32 adapter is executing a blocking SPI request.

  Returns:
    true while a request is active; otherwise false.
 *******************************************************************************/
static bool STM32_LAN9253_SPI_IsBusy(void)
{
    return spiBusy;
}

/*******************************************************************************
  Function:
    static void STM32_LAN9253_SPI_CallbackRegister(
        DRV_LAN9253_SPI_PLIB_CALLBACK callback, uintptr_t context)

  Summary:
    Registers the transfer-completion callback used by the LAN9253 utility.

  Parameters:
    callback - Function invoked after each blocking SPI request finishes.
    context  - Opaque value passed back to callback.
 *******************************************************************************/
static void STM32_LAN9253_SPI_CallbackRegister(
    DRV_LAN9253_SPI_PLIB_CALLBACK callback,
    uintptr_t context)
{
    spiCallback = callback;
    spiCallbackContext = context;
}

/*******************************************************************************
  Function:
    static void STM32_LAN9253_TimerCallbackSet(
        DRV_LAN9253_TMR_PLIB_CALLBACK callback, uintptr_t context)

  Summary:
    Registers the EtherCAT periodic timer callback.

  Parameters:
    callback - Function invoked by STM32_LAN9253_TimerDispatch().
    context  - Opaque value passed back to callback.
 *******************************************************************************/
static void STM32_LAN9253_TimerCallbackSet(
    DRV_LAN9253_TMR_PLIB_CALLBACK callback,
    uintptr_t context)
{
    timerCallback = callback;
    timerCallbackContext = context;
}

/*******************************************************************************
  Function:
    static void STM32_LAN9253_TimerStart(void)

  Summary:
    Starts the configured STM32 timer in interrupt mode.
 *******************************************************************************/
static void STM32_LAN9253_TimerStart(void)
{
    if (interfaceInitialized)
    {
        (void)HAL_TIM_Base_Start_IT(interfaceConfig.timer);
    }
}

/*******************************************************************************
  Function:
    static void STM32_LAN9253_TimerStop(void)

  Summary:
    Stops the configured STM32 timer interrupt.
 *******************************************************************************/
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

/*******************************************************************************
  Function:
    bool STM32_LAN9253_InterfaceInit(
        const STM32_LAN9253_INTERFACE_CONFIG *config)

  Summary:
    Initializes the STM32 adapter and injects its peripheral interfaces.

  Description:
    Copies the board configuration, initializes local callback state, drives
    LAN9253 chip select inactive, and passes the SPI/timer function tables to
    ECAT_Util_Initialize().  Call this routine after CubeMX peripheral
    initialization and before ECAT_Initialization().

  Parameters:
    config - STM32 peripheral handles, GPIO mappings and SPI timeout.

  Returns:
    true when all required configuration fields are valid; otherwise false.
 *******************************************************************************/
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

/*******************************************************************************
  Function:
    void EIC_CallbackRegister(EIC_PIN pin, EIC_CALLBACK callback,
                              uintptr_t context)

  Summary:
    Provides the Harmony EIC callback-registration API expected by the driver.

  Description:
    Stores callbacks under logical EIC channel numbers.  STM32 Cube configures
    the real EXTI peripheral; STM32_LAN9253_EXTI_Dispatch() translates physical
    GPIO pin masks back to these logical channels.

  Parameters:
    pin      - Logical LAN9253 EIC channel.
    callback - Function to invoke when the channel is dispatched.
    context  - Opaque value passed back to callback.
 *******************************************************************************/
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

/*******************************************************************************
  Function:
    static void STM32_LAN9253_InvokeEIC(EIC_PIN pin)

  Summary:
    Invokes a previously registered logical EIC callback when present.
 *******************************************************************************/
static void STM32_LAN9253_InvokeEIC(EIC_PIN pin)
{
    STM32_LAN9253_EIC_CALLBACK_OBJECT *object =
        &eicCallbacks[(uint32_t)pin];

    if (object->callback != NULL)
    {
        object->callback(object->context);
    }
}

/*******************************************************************************
  Function:
    void STM32_LAN9253_EXTI_Dispatch(uint16_t gpioPin)

  Summary:
    Dispatches a Cube HAL EXTI pin to the registered LAN9253 callback.

  Description:
    Forward GPIO_Pin from HAL_GPIO_EXTI_Callback() to this routine.  SYNC0,
    SYNC1 and ESC IRQ are mapped to the EIC channel identifiers retained by the
    unmodified LAN9253 utility driver.

  Parameters:
    gpioPin - STM32 GPIO pin mask reported by HAL_GPIO_EXTI_Callback().
 *******************************************************************************/
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

/*******************************************************************************
  Function:
    void STM32_LAN9253_TimerDispatch(TIM_HandleTypeDef *timer)

  Summary:
    Dispatches the configured STM32 timer interrupt to the EtherCAT callback.

  Parameters:
    timer - Timer handle reported by HAL_TIM_PeriodElapsedCallback().
 *******************************************************************************/
void STM32_LAN9253_TimerDispatch(TIM_HandleTypeDef *timer)
{
    if (interfaceInitialized && (timer != NULL) &&
        (timerCallback != NULL) &&
        (timer->Instance == interfaceConfig.timer->Instance))
    {
        timerCallback(timerCallbackContext);
    }
}

/*******************************************************************************
  Function:
    void PORT_PinSet(PORT_PIN pin)

  Summary:
    Provides the Harmony GPIO-set API expected by the LAN9253 utility driver.

  Description:
    PORT_PIN_PB11 maps to the configured STM32 chip-select output and
    PORT_PIN_PB31 maps to the optional Error LED output.  The names are logical
    compatibility identifiers and are not physical STM32 port names.

  Parameters:
    pin - Logical compatibility pin to drive high.
 *******************************************************************************/
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

/*******************************************************************************
  Function:
    void PORT_PinClear(PORT_PIN pin)

  Summary:
    Provides the Harmony GPIO-clear API expected by the LAN9253 utility driver.

  Parameters:
    pin - Logical compatibility pin to drive low.
 *******************************************************************************/
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
