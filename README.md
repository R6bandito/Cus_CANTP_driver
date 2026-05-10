# Cus_CANTP_driver

## 仓库概述

#### 	--简介--

​	本仓库是一套CAN TP 传输层协议 (ISO 15765-2)的轻量级实现参考，其帧协议与传输流程严格遵循(ISO 15765-2)。

**当前版本(Ver 2.0)已稳定可用的功能是经典 CAN（DLC=8）+ 普通寻址模式 + 单帧/多帧收发 + 流控 + 超时检测。以及拓展寻址模式下的单帧收发。**其余功能例如：

- **拓展寻址模式下的多帧收发**
- **混合寻址模式**
- **CAN FD 支持**
- **发送超时重试机制**

上述功能待后续版本迭代。**该库目前处于持续维护状态。**

#### 	--特点--

- **协议栈本身完全硬件无关**： CANTP仅作为通信中间层，不依赖任何特定 MCU 或 CAN 控制器。通过更换底层的硬件抽象层，即可在不同平台上（例如：STM32 bxCAN、ESP32 TWAI 等）运行。当前项目中，基于STM32 bxCAN控制器， 提供两种可直接使用的硬件抽象层实现： Cus_CAN库（基于HAL实现），本仓库中附带的CANTP工具包(Utils)库（纯寄存器操作，零HAL依赖）。详见下文
- **层次化设计**：底层硬件驱动层、工具层、协议层各司其职，接口清晰，便于移植和维护。移植重点在于硬件抽象层，工具层（如果使用）。协议层无需修改也不建议修改。
- **模块裁切功能**：提供对应宏开关用于裁切模块，例如 可通过 `USE_CANTP_UTILIS` 宏来决定是否需要将配套工具库纳入一并编译。

#### 	--架构分层示例--

```
┌─────────────────────────────────┐
│ 应用层 (User App) │
├─────────────────────────────────┤
│ CAN TP 协议层 (Cus_CANTP) │
│ ISO 15765-2 帧组装/解析 │
├───────────────┬─────────────────┤
│ CUS CAN 驱动 │ UTILS 工具库 │
│ (HAL 封装) │ (纯寄存器) │
├───────────────┴─────────────────┤
│ STM32 bxCAN 硬件 │
└─────────────────────────────────┘
```

> **注意**：本仓库仅包含 CAN TP 协议栈代码及配套的 UTILS 工具库。 
>
> **Cus CAN 驱动库（基于 HAL 封装）位于独立仓库 [Cus_CAN](https://github.com/R6bandito/stm32-Cuscan_driver)**，如需使用 HAL 模式，请自行前往该仓库获取。

------

## 🚀快速开始

此处不阐述移植过程，仅作为在文件已经完全移植的情况下，快速启动通信的指南，如需移植指南请移步对应篇章。

#### 	**Cus_CAN + CANTP**

示例设备 ： ( STM32F103ZET6 (接收端) / STM32F103C8T6 (发送端) )

STM32F103C8T6 测试环境配置如下：

```c
  HAL_Init();
  SystemClock_Config_72Mhz();	// 配置时钟主频 72Mhz.
  debug_uart_Init();		    // 启用串口调试.
```

1. 配置CAN外设（CAN外设参数及过滤器参数），使能CAN时钟，并启动CAN外设通信。Cus_CAN驱动库提供了快速启动API

   （ Cus_CAN_QuickSetup() ），此处调用该API后，将CAN1配置为了 500Kb/s  全通过滤模式。

```c
    Cus_CAN_GPIO_t gpio = 
    { .Alternate = 0, .CAN_GPIO_RX = GPIO_PIN_11, .CAN_GPIO_TX = GPIO_PIN_12, .CAN_GPIOPort_x = GPIOA };

    HAL_StatusTypeDef hReturn = Cus_CAN_QuickSetup(CAN1, &gpio);
    if ( hReturn != HAL_OK )
    {
      for( ; ; );
    }
```

   2. 获取设备控制块，并打开 **发送完成 / FIFO悬挂** 中断。（示例为FIFO0） 

      Ps: `EnableInterrupt`内部会设置优先级及Enable对应IRQN，无需再次调用NVIC相关API。

```c
Cus_CAN_Device_t *pDev = Cus_CAN_getControlBlock(CAN1);
pDev->EnableInterrupt(pDev, CAN_IT_RX_FIFO0_MSG_PENDING);
pDev->EnableInterrupt(pDev, CAN_IT_TX_MAILBOX_EMPTY);
```

3. 注册缓冲区（注：通过设备控制块注册的该缓冲区并不直接用于CANTP，但是CANTP接收帧依赖Cus驱动库实现的环形缓冲区，因此需要为Cus库注册一块内存用于环形缓冲区）。

```c
uint8_t ITBuffer[1024];
pDev->registerRxBuffer(pDev, ITBuffer, sizeof(ITBuffer));
```

​	将核心功能`Cus_Cantp_HeartTick()`放入 1ms 定时中断中，该API用于协议栈内部维护的定时器计数。将`Cus_Cantp_MainFunction()`放入主循环（裸机程序），或单独开出一个任务专用于 `Cus_Cantp_MainFunction`（RTOS系统）. 也可将其与`Cus_Cantp_HeartTick`一并放入1 ms 定时中断中，不过可能导致中断时间过长，对实时性有一定影响。

示例程序采用裸机程序，因此将`Cus_Cantp_MainFunction`与`Cus_Cantp_HeartTick`一起放入Systick的 1ms 定时中断中。如下所示

```c
   void SysTick_Handler(void)
   {
    HAL_IncTick();
    Cus_Cantp_HeartTick();
    Cus_Cantp_MainFunction();
   }
```

4. 初始化 CANTP 系统，并创建接收或发送连接控制块。

- `Cus_Cantp_CreateTxConnection`：创建发送控制块（作为发送方）。
- `Cus_Cantp_CreateRxConnection`：创建接收控制块（作为接收方）。

关于 API 参数，详见 API 解析。

```c
    Cus_Cantp_SystemInit();		// 初始化 CANTP 系统.

    Cus_CANTp_Conn_t *pConn = Cus_Cantp_CreateTxConnection(0x12, 0x18, CANTP_ADDR_MODE_COMMON, 
    												(void *)CAN1, Cus_CanTP_canSendFunc_Asynchronous, NULL);
    if ( !pConn )				// 创建发送控制块 (C8T6).
    {
      for( ; ; );
    }
```

```c
    Cus_Cantp_SystemInit();		// 初始化 CANTP 系统.

    Cus_CANTp_Conn_t *pConn = Cus_Cantp_CreateRxConnection(0x12, CANTP_ADDR_MODE_COMMON, 0, 0, (void *)CAN1, recvBuffer, sizeof(recvBuffer), Cus_CanTP_canSendFunc_Asynchronous, indication, NULL);
    if ( !pConn )				// 创建接收控制块 (ZET6).
    {
      for( ; ; );
    }
```

`Cus_CanTP_canSendFunc_Asynchronous` 是 Cus CAN 库提供的异步发送回调。`indication` 是用户需要自己实现的接收回调（在中断上下文执行，不能阻塞）。 **无论是接收还是发送， 均需要配置 发送回调！**

5. 对于接收方。 在开启接收FIFO中断，初始化CANTP系统及配置接收控制块后，便已进入接收状态。 当bxCAN收到一帧CAN数据后，会触发中断，将其放入Cus_CAN驱动库维护的环形缓冲区中，并且从该缓冲区中取帧喂给CANTP系统。

​	对于发送方，在配置发送控制块后，通过`Cus_Cantp_startTransmit()` 请求一次CANTP传输。该 API 中会自动区分是否开启多帧传输。

```c
    // (C8T6)请求一次CANTP发送.
    S8 ret = Cus_Cantp_startTransmit(pConn, data, sizeof(data));
```

------

#### **UTILS + CANTP**







------

## **API Reference**

- `void Cus_Cantp_HeartTick( void )`

```c
void Cus_Cantp_HeartTick( void )
```

**参数**：void.

**返回值**：void.

**描述**：核心 API 之一，协议栈的心跳驱动函数。该函数**必须由用户以 1 毫秒的固定周期调用**，通常放在硬件定时器中断服务函数（如 `SysTick_Handler`）中。

**功能细节**：

​	该函数内部会遍历所有连接控制块（包括空闲池中的控制块），对于目前处于活跃状态的控制块（非 `CONN_IDLE`）执行以下操作：

- `Timer_N_Ar`：接收方发送确认超时计数器递减。
- `Timer_N_As`：发送方发送确认超时计数器递减。
- `Timer_N_Bs`：发送方等待流控帧超时计数器递减。
- `Timer_N_Cr`：接收方等待连续帧超时计数器递减。
- `Timer_StminDelayOnly`：连续帧最小间隔延时计数器递减。

**注意事项**：

1.调用周期一定要固定 1ms 。若调用周期不确定则会导致协议栈超时判断提前或滞后，进而可能影响多帧传输可靠性。

2.当前版本 ( Ver 2.0 ) 未引入多线程保护，如需多线程环境使用请自行引入临界区进行保护。

------

## ⚠️注意事项

- **重要约束：CAN TP 启用后的帧管理**：

对于采用 Cus_CAN + CANTP 形式，当前版本下( Ver 2.0 )，一旦启用 CAN TP 协议栈并对某个 CAN ID 建立连接：**所有从硬件接收到的 CAN 帧都会被送入 CAN TP 层进行匹配处理。**一旦接收到的某帧不符合(ISO 15765-2)协议形式，**则由于缓冲区设计原因会被静默丢弃**！

​      因此一但启用CANTP，建议所有通信统一走CANTP协议，不要混用裸CAN数据帧和CANTP协议帧。

​											**( 待续 )**