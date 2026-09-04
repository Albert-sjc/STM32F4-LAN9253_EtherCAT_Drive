# STM32G4 LAN9253 Interface Design

## Goal

Adapt the existing LAN9253 driver to STM32G474 without modifying any existing
file under `EtherCAT/driver/lan9253`.

## Design

Keep the existing LAN9253 SPI and timer function-table contracts. Add a
blocking STM32 HAL implementation in `EtherCAT/port/stm32g4` and inject its
tables through `ECAT_Util_Initialize()` before `ECAT_Initialization()`.

Add compatibility headers named `device.h` and `definitions.h` beside the
existing LAN9253 driver because those names are already included by the
unmodified vendor files. Both headers are documented as STM32 port shims and
forward to the new interface header.

The port accepts the SPI handle, timer handle, GPIO mappings, EXTI pin mappings,
and timeout through a configuration structure. It does not define CubeMX HAL
callbacks; the application forwards GPIO and timer callbacks to dispatch
functions so it can coexist with other peripherals.

## Constraints

- Do not modify existing files in `EtherCAT/driver/lan9253`.
- Use ordinary blocking STM32 SPI, not SQI/QSPI or DMA.
- Keep chip-select ownership in the existing LAN9253 driver via compatibility
  implementations of `PORT_PinSet()` and `PORT_PinClear()`.
- Invoke the registered SPI completion callback after every completed HAL call,
  including failures, so the existing synchronous wait cannot remain stuck.
- Treat the error LED and SYNC0 pins as optional.

