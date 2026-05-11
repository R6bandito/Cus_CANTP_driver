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

- `void Cus_Cantp_MainFunction( void )`

```c
void Cus_Cantp_MainFunction( void )
```

**参数**：void.

**返回值**：void.

**描述**：协议栈的主任务函数，负责处理所有连接的**超时检测**和**连续帧发送触发**。该函数必须在主循环（裸机），或一个独立的任务（RTOS）中周期性进行调用，并且调用频率最好接近 `HeartTick` 的1ms周期。如对实时性要求不高，也可将其与`HeartTick`一并放入1ms定时中断进行处理。

**功能细节**：

​	该函数内部会遍历所有连接控制块，并检查活跃连接的 `N_Cr` , `N_Bs` , `N_As`, `N_Ar` 超时状态。 若发生超时，则调用用户提供的错误回调 `ErrCallBack`，并将处理逻辑交由用户处理。 若未提供错误回调，则**静默释放连接**。

​	该函数内部也维护多帧发送流程。会检查 `STmin` 延时，并在延时达到后触发一次连续帧的发送 `Cus_Cantp_SendNextCF`。

**注意事项**：

1.`HeartTick` 仅负责**计数器递减**。`MainFunction` 负责**检查计数器是否到零并执行超时逻辑**。若将`MainFunction`放入中断中，由于内部可能会触发连续帧发送，涉及较复杂逻辑，因此会延长中断执行时间，若对实时性要求较高应格外注意。

2.为了在计数器归零时及时响应，该函数最好以高频率进行调用。

3.当前版本 （Ver 2.0）未引入线程保护，该函数中涉及`Cus_Cantp_SendNextCF`，会对全局控制块内部状态进行修改。如运行在多线程环境下，建议自行引入临界段进行保护。

------

- `void Cus_Cantp_ReleaseConn( Cus_CANTp_Conn_t *pConn )`

```c
void Cus_Cantp_ReleaseConn( Cus_CANTp_Conn_t *pConn )
```

**参数**：(Cus_CANTp_Conn_t *) pConn  待释放的连接控制块。

**返回值**：void.

**描述**：**释放指定的，已被分配的连接控制块**。

**功能细节**：

​	该函数内部会对传入的控制块进行合法性检查，而后将控制块的 `CurrentState` , `ConnIndex` 成员分别赋值为：`CONN_IDLE`, `-1`。表示该控制块当前处于IDLE状态，并且已被释放可供分配。Ps：该函数内部仅修改状态，不对连接控制块做0初始化处理。控制块及其对应通道的0初始化时机将在下次分配时进行。

**注意事项**：

1.该`API`对外暴露，目的在于由用户主动释放需要提前终止某个连接的场景。协议栈内部等清理路径会自动调用该函数，通常无需用户手动调用。

2.本函数遵循“延迟清零”设计，目的在于减少频繁释放/分配时的 CPU 开销。**对一个刚释放的连接块，其内部指针和数据可能存在残留值，禁止在释放后继续以其它任何方式访问其数据区**。

------

- `void Cus_Cantp_SystemInit( void )`

```c
void Cus_Cantp_SystemInit( void )
```

**参数**：void.

**返回值**：void.

**描述**：**协议栈初始化函数**。启动通信之前必须调用该函数，该函数应在所有 CAN TP 相关 API 之前调用，且只需调用一次。

**功能细节**：

​	该函数用于初始化CANTP连接池，并且备份初始通道配置，以便连接块释放后对通道进行配置复位，避免上次通信修改的 TA、SA 等参数污染下一次连接。内部执行如下操作：

- 遍历连接池，将所有连接控制块初始化为空闲状态（`CONN_IDLE`，`ConnIndex = -1`）。
- 备份当前通道配置表到内部备份数组，以便后续连接释放后恢复通道的初始配置。

**注意事项**：	

1.重复调用本函数不会导致错误，但会覆盖已有的备份配置，通常不需要这样做。

2.本函数应在创建任何连接（`CreateTxConnection` / `CreateRxConnection`）之前调用。

------

## ⚠️注意事项

- **重要约束：CAN TP 启用后的帧管理**：

对于采用 Cus_CAN + CANTP 形式，当前版本下( Ver 2.0 )，一旦启用 CAN TP 协议栈并对某个 CAN ID 建立连接：**所有从硬件接收到的 CAN 帧都会被送入 CAN TP 层进行匹配处理。**一旦接收到的某帧不符合(ISO 15765-2)协议形式，**则由于缓冲区设计原因会被静默丢弃**！

​      因此一但启用CANTP，建议所有通信统一走CANTP协议，不要混用裸CAN数据帧和CANTP协议帧。

​											**( 待续 )**