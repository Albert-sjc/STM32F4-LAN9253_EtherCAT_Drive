# STM32G4 LAN9253 Interface Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a tested STM32G4 hardware adapter for the existing LAN9253 driver without changing vendor driver files.

**Architecture:** Preserve the existing SPI and timer PLIB function tables and implement them with blocking STM32 HAL calls. Provide `device.h` and `definitions.h` compatibility headers so existing includes resolve unchanged.

**Tech Stack:** C11, STM32G4 HAL, EtherCAT SSC, MSVC host test doubles.

**Spec:** `docs/superpowers/specs/2026-09-04-stm32g4-lan9253-interface-design.md`

## Global Constraints

- Existing files in `EtherCAT/driver/lan9253` must not be modified.
- New compatibility headers in that directory must explain why they exist.
- The adapter uses blocking SPI and application-owned HAL callback forwarding.

---

### Task 1: Define and test the adapter contract

**Files:**
- Create: `tests/fakes/stm32g4xx_hal.h`
- Create: `tests/test_stm32_lan9253_interface.c`
- Create: `EtherCAT/port/stm32g4/stm32_lan9253_interface.h`
- Create: `EtherCAT/port/stm32g4/stm32_lan9253_interface.c`

**Interfaces:**
- Consumes: `DRV_LAN9253_UTIL_INIT`, STM32 HAL SPI/GPIO/TIM APIs.
- Produces: `STM32_LAN9253_InterfaceInit`, EXTI and timer dispatch functions, Harmony-compatible EIC/PORT functions.

- [ ] Write a host test that captures the injected function tables and exercises SPI, GPIO, EXTI, and timer behavior.
- [ ] Compile the test before implementation and confirm failure because the adapter header is absent.
- [ ] Implement the smallest blocking HAL adapter that satisfies the tests.
- [ ] Compile and run the test and confirm all assertions pass.

### Task 2: Add include compatibility shims

**Files:**
- Create: `EtherCAT/driver/lan9253/device.h`
- Create: `EtherCAT/driver/lan9253/definitions.h`

**Interfaces:**
- Consumes: `stm32g4xx_hal.h`, `stm32_lan9253_interface.h`.
- Produces: the include names required by the unmodified LAN9253 sources.

- [ ] Add documented forwarding headers with unique include guards.
- [ ] Compile a translation unit that includes the existing LAN9253 utility header.
- [ ] Compare hashes of all pre-existing LAN9253 files with the baseline hashes.

