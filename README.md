# STM32G474 + LAN9253 EtherCAT 从站

## 项目简介

本项目用于将 LAN9253 EtherCAT 从站控制器接入 STM32G474。STM32G474 作为 PDI Host，通过普通 SPI访问 LAN9253；STM32G474 与 LAN9253 共同组成 EtherCAT 从站设备。

项目包含：

- Beckhoff EtherCAT Slave Stack Code（SSC）生成的协议栈与对象字典；
- LAN9253 ESC访问驱动；
- STM32G4 SPI、GPIO、EXTI和定时器适配层；
- LAN9253 ESI XML、SSC工程及对象字典源文件；
- STM32适配层主机侧测试；
- 后续移植和上板验证说明。

## 当前状态

目前已经完成：

- 保留原有 LAN9253 驱动接口；
- 使用函数指针注入 STM32 SPI和定时器实现；
- 实现普通阻塞式 SPI Write、Read和WriteRead；
- 实现 Harmony EIC和PORT兼容接口；
- 实现 EXTI及定时器回调分发；
- 增加 device.h和definitions.h兼容头；
- 增加主机侧接口与头文件兼容测试；
- 完成适配层注释和后续工作文档。

目前尚未完成：

- 完整 STM32CubeMX/CubeIDE工程；
- 真实 STM32G474 ARM-GCC整体编译；
- 实际硬件 SPI、IRQ和SYNC信号验证；
- PDO应用函数；
- TwinCAT状态机和PDO联调；
- FoE固件升级与Bootloader。

因此，当前代码属于移植基础和适配骨架，尚未完成真实硬件验收。

详细待办参见：

[STM32G4 LAN9253 后续修改说明](docs/stm32g4_lan9253_remaining_changes.md)

## 系统架构

~~~text
EtherCAT Master
      │
      │ EtherCAT
      ▼
   LAN9253
      │
      │ SPI PDI
      ▼
 STM32G474
~~~

软件调用关系：

~~~text
EtherCAT SSC
    │
    ▼
HW_EscRead / HW_EscWrite
    │
    ▼
drv_lan9253.c
    │
    ▼
ECAT_Lan9253_SPIRead / SPIWrite / SPIFastRead
    │
    ▼
drv_lan9253_ecat_util.c
    │
    ▼
DRV_LAN9253_UTIL_SPI_PLIB_INTERFACE
    │
    ▼
stm32_lan9253_interface.c
    │
    ▼
STM32G4 HAL SPI / GPIO / TIM / EXTI
~~~

## 目录结构

~~~text
3_LAN9253/
├─ README.md
├─ docs/
│  ├─ stm32g4_lan9253_remaining_changes.md
│  └─ superpowers/
├─ EtherCAT/
│  ├─ Src/
│  │  ├─ ecat_def.h
│  │  ├─ ecatslv.c
│  │  ├─ ecatappl.c
│  │  ├─ mailbox.c
│  │  ├─ LAN9253EtherCATSlave.c
│  │  └─ LAN9253EtherCATSlaveObjects.h
│  ├─ driver/lan9253/
│  │  ├─ drv_lan9253.c
│  │  ├─ drv_lan9253.h
│  │  ├─ drv_lan9253_ecat_util.c
│  │  ├─ drv_lan9253_ecat_util.h
│  │  ├─ drv_lan9253_definitions.h
│  │  ├─ device.h
│  │  └─ definitions.h
│  ├─ port/stm32g4/
│  │  ├─ stm32_lan9253_interface.c
│  │  └─ stm32_lan9253_interface.h
│  ├─ LAN9253EtherCATSlave.xml
│  ├─ LAN9253EtherCATSlave.esp
│  └─ LAN9253EtherCATSlave.xlsx
└─ tests/
   ├─ fakes/stm32g4xx_hal.h
   ├─ test_stm32_lan9253_interface.c
   └─ test_lan9253_header_compatibility.c
~~~

## 主要文件

### EtherCAT/Src

包含 SSC协议栈、CoE、FoE、Mailbox、对象字典以及应用回调。

LAN9253EtherCATSlave.c 中的以下函数仍需根据实际应用实现：

~~~c
APPL_InputMapping();
APPL_OutputMapping();
APPL_Application();
~~~

### EtherCAT/driver/lan9253

包含 LAN9253 ESC读写与 SPI协议实现。

现有 drv_lan9253*.c/.h 保持原有逻辑。新增的 device.h和definitions.h 只是 STM32兼容入口，用于满足原驱动已有的 include。

### EtherCAT/port/stm32g4

stm32_lan9253_interface.c/.h 实现：

- STM32 HAL SPI适配；
- SPI完成回调；
- LAN9253 CS与Error LED GPIO映射；
- ESC IRQ、SYNC0和SYNC1回调分发；
- EtherCAT 1 ms定时器分发；
- SPI和Timer函数表注入。

## STM32CubeMX配置要求

### SPI

第一阶段建议：

- Master；
- Full Duplex；
- 8-bit；
- MSB First；
- 软件NSS；
- CS使用普通GPIO；
- 初始频率约5 MHz；
- 不使用DMA和SPI中断。

SPI模式、最大时钟和Dummy Byte必须根据 LAN9253数据手册及实际波形验证。

完整 LAN9253事务期间，CS由原驱动统一控制。STM32适配层中的 SPI Write、Read和WriteRead不能自行拉高或拉低CS。

### GPIO与EXTI

需要配置：

~~~text
LAN9253_CS      GPIO输出，初始高
LAN9253_IRQ     EXTI输入，必需
LAN9253_SYNC0   EXTI输入，可选
LAN9253_SYNC1   EXTI输入，可选
LAN9253_ERR_LED GPIO输出，可选
~~~

IRQ和SYNC信号的有效电平、上下拉及触发边沿必须与 LAN9253配置一致。

### 定时器

配置一个1 ms基础定时器，例如 TIM6，并启用更新中断。该定时器用于驱动 EtherCAT协议栈周期检查。

## 初始化示例

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

初始化顺序不能调整：

~~~text
HAL与CubeMX外设
      ↓
STM32_LAN9253_InterfaceInit
      ↓
ECAT_Initialization
      ↓
MainInit
      ↓
MainLoop
~~~

## HAL回调转发

适配层不直接定义 HAL弱回调。应用需要转发：

~~~c
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    STM32_LAN9253_EXTI_Dispatch(GPIO_Pin);
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    STM32_LAN9253_TimerDispatch(htim);
}
~~~

## 中断优先级

STM32适配层和原 LAN9253驱动共用一个 SPI实例，当前实现不可重入。建议初始优先级：

~~~text
SysTick             抢占优先级3
LAN9253 IRQ         抢占优先级5
LAN9253 SYNC0       抢占优先级5
LAN9253 SYNC1       抢占优先级5
EtherCAT 1 ms TIM   抢占优先级5
~~~

Cortex-M中数值越小，抢占优先级越高。

SysTick必须能够在阻塞式 HAL SPI调用期间运行，否则 HAL超时可能无法递增。EtherCAT相关中断应保持相同抢占优先级，避免它们相互嵌套并重复访问 SPI。

## 测试

tests目录包含主机侧测试：

- test_stm32_lan9253_interface.c：验证函数表注入、SPI、GPIO、EXTI和Timer基本行为；
- test_lan9253_header_compatibility.c：验证原驱动头可以通过新增兼容头正常包含；
- tests/fakes/stm32g4xx_hal.h：主机测试使用的最小 HAL替身。

这些测试不等价于 STM32目标编译和真实硬件测试。提交前仍需使用 STM32CubeIDE/ARM-GCC 编译完整工程。

## 上板验证

推荐顺序：

1. 检查 CS、SCK、MOSI和MISO波形；
2. 读取 LAN9253 Byte Order Test寄存器，期望为0x87654321；
3. 验证 ESC寄存器读写和 Fast Read；
4. 检查 LAN9253 IRQ；
5. 使用 TwinCAT扫描从站并进入 PREOP；
6. 验证 CoE/SDO；
7. 实现 PDO映射并进入 SAFEOP、OP；
8. 长时间验证 SPI重入、看门狗和周期稳定性；
9. 最后启用 SYNC0、SYNC1和Distributed Clocks。

## FoE说明

当前代码包含 FoE相关 SSC文件，但 STM32G474 的 Flash分区、Bootloader、镜像校验、掉电保护和回滚机制尚未实现。

在基础 EtherCAT通信完成前，不要直接复用其他 MCU的 Flash擦写或 BankSwap逻辑。

## 已知限制

- 当前 SPI实现为阻塞式；
- SPI访问不可重入；
- 原驱动忽略底层 SPI函数的返回值；
- HAL SPI错误状态尚未暴露给应用；
- Timer启动和停止错误尚未暴露；
- 配置结构参数校验仍需加强；
- 尚未完成真实 STM32G474硬件验证。

## 许可证

EtherCAT SSC源文件受 Beckhoff EtherCAT Slave Stack Code许可协议约束。

LAN9253驱动文件包含 Microchip版权和使用条款。使用、修改和发布前，应确认 Microchip、Beckhoff及其他第三方组件的许可条件与目标产品相符，并保留原始版权和许可证说明。
