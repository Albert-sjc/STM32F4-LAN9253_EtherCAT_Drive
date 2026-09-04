#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "stm32_lan9253_interface.h"

static const DRV_LAN9253_UTIL_INIT *capturedInit;
static uint8_t transmitted[64];
static size_t transmittedSize;
static const uint8_t *receiveStream;
static size_t receiveOffset;
static unsigned int spiCallbackCount;
static unsigned int timerCallbackCount;
static unsigned int eicCallbackCount;
static uintptr_t lastContext;
static unsigned int timerStartCount;
static unsigned int timerStopCount;
static GPIO_TypeDef *lastPort;
static uint16_t lastPin;
static GPIO_PinState lastPinState;

void ECAT_Util_Initialize(unsigned short int index, const void *init)
{
    assert(index == 0U);
    capturedInit = init;
}

HAL_StatusTypeDef HAL_SPI_Transmit(
    SPI_HandleTypeDef *hspi,
    uint8_t *data,
    uint16_t size,
    uint32_t timeout)
{
    (void)hspi;
    assert(timeout == 25U);
    memcpy(transmitted + transmittedSize, data, size);
    transmittedSize += size;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_SPI_TransmitReceive(
    SPI_HandleTypeDef *hspi,
    uint8_t *txData,
    uint8_t *rxData,
    uint16_t size,
    uint32_t timeout)
{
    (void)hspi;
    assert(timeout == 25U);
    memcpy(transmitted + transmittedSize, txData, size);
    transmittedSize += size;
    for (uint16_t i = 0; i < size; ++i)
    {
        rxData[i] = receiveStream == NULL ? 0U : receiveStream[receiveOffset++];
    }
    return HAL_OK;
}

HAL_StatusTypeDef HAL_TIM_Base_Start_IT(TIM_HandleTypeDef *htim)
{
    (void)htim;
    ++timerStartCount;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_TIM_Base_Stop_IT(TIM_HandleTypeDef *htim)
{
    (void)htim;
    ++timerStopCount;
    return HAL_OK;
}

void HAL_GPIO_WritePin(
    GPIO_TypeDef *port,
    uint16_t pin,
    GPIO_PinState state)
{
    lastPort = port;
    lastPin = pin;
    lastPinState = state;
}

static void SPI_Callback(uintptr_t context)
{
    ++spiCallbackCount;
    lastContext = context;
}

static void Timer_Callback(uintptr_t context)
{
    ++timerCallbackCount;
    lastContext = context;
}

static void EIC_Callback(uintptr_t context)
{
    ++eicCallbackCount;
    lastContext = context;
}

int main(void)
{
    SPI_HandleTypeDef spi = {0};
    TIM_HandleTypeDef timer = {0};
    GPIO_TypeDef csPort = {1};
    GPIO_TypeDef ledPort = {2};
    const STM32_LAN9253_INTERFACE_CONFIG config = {
        .spi = &spi,
        .timer = &timer,
        .csPort = &csPort,
        .csPin = 0x0010U,
        .errorLedPort = &ledPort,
        .errorLedPin = 0x0020U,
        .sync0Pin = 0x0040U,
        .sync1Pin = 0x0080U,
        .escIrqPin = 0x0100U,
        .spiTimeoutMs = 25U
    };

    assert(STM32_LAN9253_InterfaceInit(&config));
    assert(capturedInit != NULL);
    assert(capturedInit->spiPlib != NULL);
    assert(capturedInit->timerPlib != NULL);

    capturedInit->spiPlib->spiCallbackRegister(SPI_Callback, 0x1234U);
    {
        uint8_t tx[] = {0x02U, 0x12U, 0x34U};
        assert(capturedInit->spiPlib->spiWrite(tx, sizeof(tx)));
        assert(transmittedSize == sizeof(tx));
        assert(memcmp(transmitted, tx, sizeof(tx)) == 0);
        assert(spiCallbackCount == 1U);
        assert(lastContext == 0x1234U);
        assert(!capturedInit->spiPlib->spiIsBusy());
    }

    {
        const uint8_t incoming[] = {0x78U, 0x56U, 0x34U, 0x12U};
        uint8_t rx[sizeof(incoming)] = {0};
        receiveStream = incoming;
        receiveOffset = 0U;
        transmittedSize = 0U;
        assert(capturedInit->spiPlib->spiRead(rx, sizeof(rx)));
        assert(memcmp(rx, incoming, sizeof(rx)) == 0);
        for (size_t i = 0; i < sizeof(rx); ++i)
        {
            assert(transmitted[i] == 0xFFU);
        }
    }

    PORT_PinClear(PORT_PIN_PB11);
    assert(lastPort == &csPort);
    assert(lastPin == config.csPin);
    assert(lastPinState == GPIO_PIN_RESET);
    PORT_PinSet(PORT_PIN_PB11);
    assert(lastPinState == GPIO_PIN_SET);
    PORT_PinClear(PORT_PIN_PB31);
    assert(lastPort == &ledPort);
    assert(lastPin == config.errorLedPin);

    EIC_CallbackRegister(EIC_PIN_7, EIC_Callback, 0x5678U);
    STM32_LAN9253_EXTI_Dispatch(config.escIrqPin);
    assert(eicCallbackCount == 1U);
    assert(lastContext == 0x5678U);

    capturedInit->timerPlib->timerCallbackSet(Timer_Callback, 0x9ABCU);
    capturedInit->timerPlib->timerStart();
    assert(timerStartCount == 1U);
    STM32_LAN9253_TimerDispatch(&timer);
    assert(timerCallbackCount == 1U);
    assert(lastContext == 0x9ABCU);
    capturedInit->timerPlib->timerStop();
    assert(timerStopCount == 1U);

    return 0;
}
