# STM32G474 + LAN9253 后续修改说明

## 1. 目的与移植原则

本文档记录当前 STM32G474 + LAN9253 EtherCAT 从站移植仍需完成的代码、工程配置和硬件验证工作。

当前原则：

- 保留 EtherCAT/driver/lan9253 下原有驱动源码；
- 通过函数指针注入 STM32 SPI 和定时器接口；
- 通过兼容接口保留原驱动对 Harmony EIC 和 PORT API 的调用；
- 第一阶段使用阻塞式普通 SPI，不使用 DMA、SPI中断或 SQI/QSPI；
- 基础通信稳定后再增加 FoE 固件升级。

## 2. 当前已完成

已新增 STM32G4 硬件适配层：

~~~text
EtherCAT/port/stm32g4/stm32_lan9253_interface.h
EtherCAT/port/stm32g4/stm32_lan9253_interface.c
~~~

适配层已经实现：

- SPI WriteRead、Write、Read、IsBusy 和 CallbackRegister；
- Timer CallbackSet、Start 和 Stop；
- EIC_CallbackRegister；
- PORT_PinSet 和 PORT_PinClear；
- EXTI 与定时器回调分发；
- DRV_LAN9253_UTIL_INIT 函数表注入。

为保持原驱动 include 不变，已新增：

~~~text
EtherCAT/driver/lan9253/device.h
EtherCAT/driver/lan9253/definitions.h
~~~

原有 drv_lan9253*.c/.h 均保持不变。

## 3. 必须完成：接入 STM32Cube 工程

当前目录没有完整的 STM32CubeIDE/CubeMX 工程。实际工程至少需要加入：

~~~text
EtherCAT/Src/*.c
EtherCAT/driver/lan9253/drv_lan9253.c
EtherCAT/driver/lan9253/drv_lan9253_ecat_util.c
EtherCAT/port/stm32g4/stm32_lan9253_interface.c
~~~

头文件搜索路径至少加入：

~~~text
EtherCAT/Src
EtherCAT/driver/lan9253
EtherCAT/port/stm32g4
~~~

必须包含 EtherCAT/driver/lan9253，使原驱动中的 device.h 和 definitions.h 能解析到新增的兼容头。

## 4. 必须完成：CubeMX 外设配置

### 4.1 SPI

配置普通主机 SPI，例如 SPI1：

- Master、Full Duplex、8-bit、MSB First；
- 软件管理 NSS，CS 使用独立 GPIO；
- SPI模式按照 LAN9253 数据手册设置；
- 初始时钟建议从 5 MHz 开始验证；
- 稳定后再提高频率并重新核对 Dummy Byte。

适配层 SPI函数内部不能自动操作 CS。完整 LAN9253 命令的 CS 由原驱动的 _ECAT_ChipSelectEnable() 和 _ECAT_ChipSelectDisable() 统一控制。

### 4.2 GPIO 与 EXTI

配置：

~~~text
LAN9253_CS      推挽输出，初始为高
LAN9253_IRQ     EXTI 输入，必需
LAN9253_SYNC0   EXTI 输入，可选
LAN9253_SYNC1   EXTI 输入，可选
LAN9253_ERR_LED 推挽输出，可选
~~~

IRQ、SYNC0、SYNC1 的上下拉、有效电平和触发边沿必须与硬件及 LAN9253 寄存器配置一致。STM32 的 EXTI 线由 GPIO引脚编号决定，三个输入应避免使用互相冲突的 EXTI线。

### 4.3 EtherCAT 定时器

配置一个 1 ms 基础定时器，例如 TIM6，并启用更新中断。不要同时从 SysTick 和 TIM 重复调用 EtherCAT 定时回调。

## 5. 必须完成：主函数初始化

CubeMX 外设初始化后，按以下顺序调用：

~~~c
#include "stm32_lan9253_interface.h"
#include "drv_lan9253_ecat_util.h"
#include "applInterface.h"

static const STM32_LAN9253_INTERFACE_CONFIG lan9253Config = {
    .spi          = &hspi1,
    .timer        = &htim6,
    .csPort       = LAN9253_CS_GPIO_Port,
    .csPin        = LAN9253_CS_Pin,
    .errorLedPort = LAN9253_ERR_LED_GPIO_Port,
    .errorLedPin  = LAN9253_ERR_LED_Pin,
    .sync0Pin     = LAN9253_SYNC0_Pin,
    .sync1Pin     = LAN9253_SYNC1_Pin,
    .escIrqPin    = LAN9253_IRQ_Pin,
    .spiTimeoutMs = 10U
};

int main(void)
{
    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_SPI1_Init();
    MX_TIM6_Init();

    if (!STM32_LAN9253_InterfaceInit(&lan9253Config))
    {
        Error_Handler();
    }

    ECAT_Initialization();

    if (MainInit() != 0U)
    {
        Error_Handler();
    }

    while (1)
    {
        MainLoop();
    }
}
~~~

顺序必须是：

~~~text
HAL/CubeMX 外设初始化
        ↓
STM32_LAN9253_InterfaceInit()
        ↓
ECAT_Initialization()
        ↓
MainInit()
        ↓
循环调用 MainLoop()
~~~

没有 Error LED 时：

~~~c
.errorLedPort = NULL,
.errorLedPin  = 0U,
~~~

不使用 SYNC0/SYNC1 时：

~~~c
.sync0Pin = STM32_LAN9253_UNUSED_PIN,
.sync1Pin = STM32_LAN9253_UNUSED_PIN,
~~~

ESC IRQ 是必要信号，不能配置成 STM32_LAN9253_UNUSED_PIN。

## 6. 必须完成：转发 HAL 回调

适配层没有直接定义 HAL 弱回调，应用需要负责转发。

~~~c
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    STM32_LAN9253_EXTI_Dispatch(GPIO_Pin);

    /* 其他 EXTI 处理继续放在这里。 */
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    STM32_LAN9253_TimerDispatch(htim);

    /* 其他定时器处理继续放在这里。 */
}
~~~

EXTI 分发关系：

~~~text
LAN9253 IRQ   → PDI_Isr()
LAN9253 SYNC0 → Sync0_Isr()
LAN9253 SYNC1 → Sync1_Isr()
~~~

## 7. 必须完成：NVIC 与 SPI 不可重入配置

主循环、IRQ、SYNC0、SYNC1 和 EtherCAT 定时器可能访问同一个 SPI。当前适配层不可重入，必须通过中断优先级防止嵌套访问。

现有驱动使用 BASEPRI 屏蔽抢占优先级数值大于或等于 4 的中断。STM32G474 建议先采用：

~~~text
SysTick             抢占优先级 3
LAN9253 IRQ         抢占优先级 5
LAN9253 SYNC0       抢占优先级 5
LAN9253 SYNC1       抢占优先级 5
EtherCAT 1 ms TIM   抢占优先级 5
~~~

Cortex-M 中数值越小，抢占优先级越高。

这个配置可以：

1. 让原驱动的 BASEPRI=4 在主循环 SPI访问期间屏蔽 EtherCAT中断；
2. 防止 EtherCAT IRQ、SYNC和定时器相互抢占；
3. 保证 SysTick不被 BASEPRI屏蔽，使阻塞式 HAL SPI超时能够增长。

不能只依靠 spiBusy 拒绝 ISR 内的 SPI请求；原驱动会同步等待完成回调，简单拒绝可能造成死锁或丢失 EtherCAT事件。

## 8. 应当修改：暴露 SPI与定时器错误

当前适配层在 HAL SPI失败时仍调用完成回调，以避免原驱动永久等待。但原驱动没有检查函数指针返回的 false，错误可能被忽略。

建议增加：

~~~c
HAL_StatusTypeDef STM32_LAN9253_GetLastSPIStatus(void);
HAL_StatusTypeDef STM32_LAN9253_GetLastTimerStatus(void);
~~~

或者在配置结构中增加错误回调。至少应记录并上报：

~~~text
HAL_ERROR
HAL_BUSY
HAL_TIMEOUT
~~~

应用可以据此记录日志、重新初始化 SPI、复位 LAN9253 或复位系统。

当前 TimerStart 和 TimerStop 也丢弃了 HAL返回值，需要一并记录。

## 9. 应当修改：加强初始化参数校验

STM32_LAN9253_InterfaceInit() 还应检查：

- spi->Instance 和 timer->Instance 不为 NULL；
- csPin 和 escIrqPin 是有效的单 bit GPIO mask；
- escIrqPin 不能是 STM32_LAN9253_UNUSED_PIN；
- IRQ、SYNC0、SYNC1 映射不能重复；
- errorLedPort 为 NULL 时忽略 errorLedPin；
- 禁止运行期间重复初始化。

## 10. 应当修改：接口头直接包含 HAL

当前 stm32_lan9253_interface.h 通过 drv_lan9253_definitions.h → device.h 间接获得 HAL类型。建议在接口头中直接加入：

~~~c
#include "stm32g4xx_hal.h"
~~~

device.h 仍然保留，用于满足原 LAN9253 驱动的 include。

## 11. 应当补充：测试

现有测试覆盖基本初始化、SPI写、SPI读、Dummy Byte、GPIO、ESC IRQ 和定时器分发。还需补充：

- spiWriteRead 等长和不等长传输；
- 大于32字节的分块读取；
- HAL_ERROR、HAL_BUSY、HAL_TIMEOUT；
- SPI失败后仍释放原驱动完成等待；
- SPI忙时触发 EXTI 的并发场景；
- SYNC0、SYNC1 和无关 EXTI 分发；
- 无关定时器不会触发 EtherCAT回调；
- 未配置 Error LED 时不访问空指针；
- 空配置、无效句柄、重复引脚和重复初始化。

## 12. 必须完成：PDO应用函数

以下函数目前仍是模板：

~~~c
APPL_InputMapping()
APPL_OutputMapping()
APPL_Application()
~~~

文件位置：

~~~text
EtherCAT/Src/LAN9253EtherCATSlave.c
~~~

当前主要 PDO对象为：

~~~text
0x6000:01 TxData，从站 → 主站，16 bit
0x7000:01 RxData，主站 → 从站，16 bit
~~~

基础联调阶段可以先实现主站写入值、从站返回加一值，用于验证 PDO映射和 OP状态通信。

## 13. 暂缓：FoE 固件升级

STM32G474 的 Flash分区、Bootloader、掉电保护和回滚机制尚未设计。基础 EtherCAT通信完成前，不要直接移植 SAMD51 的 NVMCTRL擦写和 BankSwap代码。

推荐顺序：

1. SPI和 LAN9253初始化；
2. EtherCAT PREOP、SAFEOP、OP；
3. SDO和 PDO通信；
4. IRQ和 Distributed Clocks；
5. 独立设计 STM32 Bootloader与 A/B固件区；
6. 最后接入 FoE下载。

## 14. 上板验证顺序

### 阶段一：SPI物理通信

- 示波器或逻辑分析仪确认 CS、SCK、MOSI、MISO；
- 读取 Byte Order Test寄存器，期望值为 0x87654321；
- 验证 SPI异常时程序不会永久卡死。

### 阶段二：ESC寄存器

- 读写 AL Event Mask；
- 读取 PDI配置；
- 检查 IRQ极性；
- 检查连续 Fast Read和地址自动递增。

### 阶段三：EtherCAT状态机

- TwinCAT能够扫描到从站；
- INIT → PREOP → SAFEOP → OP；
- 无 AL Status错误。

### 阶段四：过程数据

- SDO读写对象字典；
- RxPDO更新 0x7000:01；
- TxPDO返回 0x6000:01；
- 长时间运行无 SPI重入和看门狗错误。

### 阶段五：同步模式

- 接入 SYNC0/SYNC1；
- 启用 Distributed Clocks；
- 检查同步周期、抖动、ISR时间和 SPI总线占用。

## 15. 上传前检查表

- [ ] STM32CubeIDE/ARM-GCC 整体编译通过且无新增警告；
- [ ] InterfaceInit 在 ECAT_Initialization 前调用；
- [ ] EXTI 和 TIM HAL回调已转发；
- [ ] SPI使用软件 NSS，完整事务期间 CS保持有效；
- [ ] EtherCAT中断不会嵌套访问 SPI；
- [ ] SysTick在阻塞 SPI期间仍可更新；
- [ ] Byte Order Test读到 0x87654321；
- [ ] HAL错误能被记录或通知应用；
- [ ] PDO映射函数已实现；
- [ ] TwinCAT能够进入 OP；
- [ ] ESI XML、SII EEPROM和固件功能配置一致；
- [ ] 已确认 Microchip LAN9253代码与 Beckhoff SSC许可条件。
