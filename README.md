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

## ⚙️构建与配置

​	本协议栈通过**预处理器宏**进行功能裁切和参数配置。以下宏当前版本均定义在 `Cus_CANTP.h` 中，用户可根据需求修改。

|           宏名称           | 默认值 |                             描述                             |
| :------------------------: | :----: | :----------------------------------------------------------: |
|       API_USE_LEGACY       |   0    | 是否启用旧版兼容 API（开发过程中部分API由于设计问题导致上层不易用，或逻辑难以理解，后续版本中，该部分API被迭代以后放入该宏当中）。建议保持为0。 |
|      USE_CANTP_UTILIS      |   0    | 是否启用配套的 UTILS 工具库（纯寄存器操作，零 HAL 依赖）。设为 `1` 后，编译时会自动包含 `Cus_CANTP_Utils.h`，并可使用 `Cus_Cantp_utilsSendAsync` 和 `Cus_Cantp_utilsRecieve_FROM_ISR`。以及工具库中其余对外公开方法。（若配套使用 CAN_Cus 库，不建议开启该宏，有冲突风险）。 |
|      MAX_SUPPORT_CONN      |   4    | 最大同时支持的连接数。可增大该宏以在一台设备中支持更多连接。但会增加 RAM 占用。 |
|         TIMER_NBS          |  200   |      N_Bs 超时时间（ms）。发送方等待流控帧的最大时间。       |
|         TIMER_NAS          |  100   |     N_As 超时时间（ms）。发送方等待发送确认的最大时间。      |
|         TIMER_NAR          |  200   |     N_Ar 超时时间（ms）。接收方等待发送确认的最大时间。      |
|         TIMER_NCR          |  100   |      N_Cr 超时时间（ms）。接收方等待连续帧的最大时间。       |
| CHANNEL_CONFIG_TABLE_COUNT |   4    | 通道配置表大小（定义在 `Cus_CANTP_cfg.h` 中）。应与 `MAX_SUPPORT_CONN` 保持一致。 |



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

- `void Cus_Cantp_TxConfirmation( void *CanDevice, U8 mailbox )`

```c
void Cus_Cantp_TxConfirmation( void *CanDevice, U8 mailbox )
```

**参数**：

- （void *）CanDevice  指向对应 CAN 外设基址的指针。为便于移植，内部将其转化为`uint8_t *`进行比较，而不依赖

   `CAN_TypeDef *`类型。事实上，CANTP 作为中间件，并不关心底层的实现，因此对于底层的外设基准指针，不依赖ST官方给出的硬件抽象层。

-    (U8) mailbox   已完成发送的邮箱号。该参数对应触发该次发送完成中断的邮箱号，在 ISR 中需要明确给出该参数，以便内部能够正确维护发送状态机。

**返回值**：void.

描述：发送完成确认函数。**该函数不应由用户代码主动调用，而应由上层确保将其放入 HAL 的发送邮箱完成中断回调（如 `HAL_CAN_TxMailbox0CompleteCallback`）中（若使用HAL，例如使用配套的Cus_CAN库）**，用于通知 CAN TP 协议栈某一帧已成功从硬件发送到总线上。若不依赖于HAL，则上层需要确保每个发送邮箱对应的发送完成中断中，都有该函数并且传入正确的邮箱号。

**功能细节**：

​	该函数内部会遍历所有连接控制块，根据以下规则匹配对应的连接：

- 连接当前处于活跃状态（非 CONN_IDLE ）。
- 绑定的 CAN 设备（`BindCANDevice`）与传入的 `CanDevice` 一致。
- 上次发送使用的邮箱（`TxMailBoxIndex`）与传入的 `mailbox` 一致。

匹配成功后，根据设备当前所处的发送状态，执行对应的状态转换。

- **`CONN_TX_FF`**（首帧发送完成）：转换到 `CONN_TX_WAIT_FC`，启动 `N_Bs` 超时定时器，等待接收方的流控帧。
- **`CONN_TX_CF`**（连续帧发送完成）：检查是否所有数据已发送完毕。若当前 BS 块已发完，转换到 `CONN_TX_WAIT_FC` 等待下一个流控；否则清除 `N_As` 超时，等待 `STmin` 延时后继续发送下一帧。
- **`CONN_TX_SF`**（单帧发送完成）：转换到 `CONN_IDLE`，本次通信结束。
- **`CONN_TX_FC`**（流控帧发送完成）：转换到 `CONN_RX_WAIT_CF`，启动 `N_Cr` 超时定时器，等待发送方的连续帧。

**调用示例（基于HAL）**：

```c
void HAL_CAN_TxMailbox0CompleteCallback( CAN_HandleTypeDef *hcan )
{
    Cus_Cantp_TxConfirmation((void *)hcan->Instance, 1);			// 1 对应邮箱0. 
}

void HAL_CAN_TxMailbox1CompleteCallback(CAN_HandleTypeDef *hcan)
{
    Cus_Cantp_TxConfirmation((void *)hcan->Instance, CAN_TX_MAILBOX1);
}
```

**注意事项**：

1.由用户自行实现的 `SendFunc` 在请求发送成功后一定要将所用的实际邮箱号存入 `pConn->TxMailBoxIndex`! 否则本函数无法匹配到正确的连接。

2.目前版本下（Ver 2.0），本函数遍历连接池并读取修改连接块状态时无临界区保护，因此若用于多线程环境，建议自行引入临界区进行保护。

------

- `U8 Cus_Cantp_RecieveFrame( const U8 *data, U8 dlc, U32 canid )`

```c
U8 Cus_Cantp_RecieveFrame( const U8 *data, U8 dlc, U32 canid )
```

**参数**：

- (const U8 *) data：指向接收到的 CAN 帧数据段（8 字节）的指针。注意，本协议栈采用填充机制（数据段不足 8 字节默认使用 0xCC 填充到 8字节）。
- (U8) dlc：CAN 帧的数据长度码。**必须 >= 8**（经典 CAN），否则该帧不符合协议规定，将被忽略。dlc > 8 情况为 CAN FD 。
- (U32) canid： CAN 帧的标识符。**不能为 0**。函数内部会根据此 ID 匹配对应的连接控制块。

**返回值**：

-  0 (入口校验失败，也表征当前帧未被任何连接块接收处理)。
- \> 0（成功匹配并喂给对应连接的数量， 功能寻址模式下可能匹配多个连接）。

**描述**：**物理层到CANTP协议层的入口点**。上层喂帧总入口，该函数应在底层 CAN 接收中断或接收回调中调用，将一帧完整的 CAN 报文传递给 CAN TP 协议栈。函数内部会自动完成连接匹配和帧类型识别，并将帧分发给对应的处理函数。

**功能细节**：

​	该函数内部执行如下操作：

- 对入口参数进行校验，校验失败返回 0。
- 对整个连接池进行遍历，跳过非活跃状态的连接块。
- 对活跃的连接控制块进行 寻址模式检查（功能寻址，物理寻址）。
- 由于功能寻址可能同时匹配多个连接，匹配成功后**不会中断循环**，而是 `continue` 继续遍历。
- 对于物理寻址模式，通过 `Cus_Cantp_VerifyIDConn` 进行精确匹配（普通寻址从 CAN ID 中提取 TA 位段，拓展寻址从数据段第一个字节提取 TA）。
- 匹配成功后调用 `Cus_Cantp_RxIndication`，内部自动对帧类型进行识别（SF / FF / CF / FC），并分别转发到对应处理函数。

**调用示例**：

```c
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    CAN_RxHeaderTypeDef RxHeader;
    U8 buffer[8];

    if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &RxHeader, buffer) == HAL_OK)
    {
        Cus_Cantp_RecieveFrame(buffer, RxHeader.DLC, RxHeader.StdId);
    }
}
```

**注意事项**：

1.**帧独占行为**：一旦启用 CAN TP 并调用本函数，所有被匹配到的 CAN 帧都会被协议栈“吃掉”。不符合协议格式的帧（PCI 字节非法）将被静默丢弃，不会被上层感知。因此务必切记，启用 CANTP 后，后续所有CAN收发行为都请通过CANTP进行。

2.本函数**调用环境为中断上下文中**，其内部会调用到用户所注册的接收完成回调：`DataIndFunc`。因此上层在实现`DataIndFunc`时应尤其注意该回调在中断上下文中执行安全（不阻塞、短耗时）。

3.当前版本（Ver2.0）仅仅支持经典CAN(8帧数据段类型)，`dlc > 8` 的 CAN FD 帧处理逻辑尚未实现（入口已预留）。

------

- `Cus_CANTp_Conn_t *Cus_Cantp_CreateRxConnection( ... )`

```c
  Cus_CANTp_Conn_t *Cus_Cantp_CreateRxConnection( U8 ownAddr, 
                                                  U8 addrMode, 
                                                  U8 bs, 
                                                  U8 stmin, 
                                                  void *canDevice, 
                                                  U8 *bufferRx, 
                                                  U32 size, 
                                                  Cus_CanTP_CanSendFunc sendFunc, 
                                                  Cus_CanTP_DataIndication dataIncFunc, 
                                                  Cus_CanTP_ErrCallback errCallBack );
```

**参数**：

- (U8) ownAddr：**本机接收地址**。只有当收到的 CAN 帧中目标地址（TA）与该 `ownAddr` 匹配时，才会被该连接处理。
- (U8) addrMode：**寻址模式**。可选值有`CANTP_ADDR_MODE_COMMON`（普通寻址 默认），`CANTP_ADDR_MODE_EXT`（拓展寻址），`CANTP_ADDR_MODE_MIX`（混合寻址，当前未实现）。
- (U8) bs：**流控参数 BS（Block Size）**。`bs = 0` 表示不限制块大小，发送方可以一口气把所有连续帧发完。该参数仅在多帧传输中使用。
- (U8) stmin：**流控参数 STmin（Separation Time Minimum）**。值由接收方在流控帧中告知发送方，表示连续帧之间的最小时间间隔。`stmin = 0` 表示无间隔，发送方可以背靠背发送连续帧。单位为毫秒。
- (void *)canDevice：**绑定的 CAN 外设基址指针**。传入实际使用的 CAN 外设地址（如 `(void *)CAN1`）。该指针仅用于内部匹配（发送确认时查找对应设备），**协议栈不会解引用该指针**，因此可以传入任意类型的外设标识。
- (U8 *)bufferRx：**用户提供的接收缓冲区指针**。协议栈在接收多帧数据时，会逐步将数据拷贝到该缓冲区中。缓冲区大小必须足以容纳预期的最大传输数据长度，否则协议栈会发送 OVFLW 流控帧并中止传输。
- (U32) size：**接收缓冲区的总大小（字节）**。
- (Cus_CanTP_CanSendFunc) sendFunc：由用户实现的**底层发送回调函数**。**即使创建的是接收连接，也必须提供发送回调**，因为接收方在收到首帧后需要自动回复流控帧。该回调必须是**非阻塞**的（立即返回成功或失败）。
- (Cus_CanTP_DataIndication) dataIncFunc：由用户实现的**数据接收完成回调**。当一包完整的多帧数据接收完毕（接收方的连续帧全部到达），或收到单帧数据时，协议栈会调用此回调，将数据指针和长度通知上层。**该回调在 CAN 接收中断上下文中调用**，用户实现时必须**保证非阻塞、短耗时**。
- (Cus_CanTP_ErrCallback) errCallBack：**错误回调（可选，可传 NULL）**。当接收过程出现超时（如 N_Cr 超时，等待连续帧超时）时，协议栈会调用此回调通知上层。若传入 NULL，则超时后连接将被静默释放。

**返回值**：

- NULL：创建失败（连接池已满、缓冲区大小不足、参数无效、必要回调缺失等）。
- 非 NULL：（Cus_CANTp_Conn_t * ）成功分配并初始化连接控制块，返回其指针。

**描述**：该函数用于创建一个 CAN TP **接收连接**控制块。该函数从内部连接池中分配一个空闲连接，绑定底层 CAN 设备、注册接收缓冲区、配置寻址参数和流控参数，并设置上层回调函数。

**功能细节**：	无

**注意事项**：

1.**接收缓冲区必须足够大**：如果实际接收的数据长度超过 `size`，协议栈会在解析首帧（FF）时发现长度不匹配，主动发送 OVFLW 流控帧并中止传输，同时释放连接。此时上层不会收到 `dataIncFunc` 回调。

2.发送回调（`sendFunc`）与接收回调（`dataIncFunc`）为必要回调，若传入NULL则控制块将无法创建。

3.**关于 `ownAddr` 和 CAN ID 的关系**：`ownAddr` 是业务层的“本机地址”，CAN TP 协议栈内部会自动将其映射到 CAN ID 的对应位段（普通寻址）或数据段首字节（拓展寻址）。在**接收时，协议栈匹配的是 CAN 帧中实际携带的 TA 字段**。创建连接时，`ownAddr` 告诉协议栈“本机是 XX”，用于接收以及后续回复流控帧时构造正确的源地址。

**调用示例**：

```c
Cus_CANTp_Conn_t *pConn = Cus_Cantp_CreateRxConnection(0x12, CANTP_ADDR_MODE_COMMON, 0, 0, (void *)CAN1, recvBuffer, sizeof(recvBuffer), Cus_CanTP_canSendFunc_Asynchronous, indication, NULL);
    if ( !pConn )
    {
      for( ; ; );
    }
```

------

- `Cus_CANTp_Conn_t *Cus_Cantp_CreateTxConnection( ... )`

```c
  Cus_CANTp_Conn_t *Cus_Cantp_CreateTxConnection( U8 targetAddr, 
                                                  U8 sourceAddr, 
                                                  U8 addrMode, 
                                                  void *canDevice, 
                                                  Cus_CanTP_CanSendFunc sendFunc, 
                                                  Cus_CanTP_ErrCallback errCallback );
```

**参数**：

- (U8) targetAddr：**目标接收方地址**。即数据要发送到的那个节点的地址。在普通寻址模式下，该地址会被映射到 CAN ID 的 TA 字段；在拓展寻址模式下，映射到数据段第一个字节。
- (U8) sourceAddr：**本机发送方地址**。在普通寻址模式下，该地址会被映射到 CAN ID 的 SA 字段；在拓展寻址模式下，映射到 CAN ID 的对应位段。(Ps : **本机地址一定要正确填写**，接收方回复流控帧时，目标地址（TA）即为此地址。如果填写错误，流控帧将无法被本机正确接收，导致多帧传输超时失败。)
- (U8) addrMode：**寻址模式**。同上。
- (void *) canDevice：**绑定的 CAN 外设基址指针**。同上。
- (Cus_CanTP_CanSendFunc) sendFunc：**底层发送回调函数（必须）**。由用户自行提供实现。
- (Cus_CanTP_ErrCallback) errCallback：**错误回调（可选，可传 NULL）**。当发送过程出现超时（如 N_Bs 超时、N_As 超时）时，协议栈会调用此回调通知上层。若传入 NULL，则超时后连接将被**静默释放**。

**返回值**：

- NULL：创建失败（连接池已满、参数无效、必要回调缺失）。
- 非 NULL：（Cus_CANTp_Conn_t * ）成功分配并初始化连接控制块，返回其指针。

**描述**：创建一个 CANTP **发送连接控制块**。若本机在 CANTP 通讯网络中作为需要作为发送端（除回复流控外，还需要主动发起发送请求），则必须创建该发送控制块。该函数从内部连接池中分配一个空闲连接，绑定底层 CAN 设备、配置寻址参数，并设置发送回调函数。与接收连接不同，发送连接不需要用户提供接收缓冲区，因为**流控帧并不携带用户数据**，且接收和解析由协议栈内部自动完成。流控帧解析完毕后不会通知上层。

**功能细节**：	无

**注意事项**：

1.连接创建完毕后，通过 `Cus_Cantp_startTransmit()`请求发起一次 CANTP 传输。 协议栈会自动完成单帧或多帧发送。

2.**接收路径仍是必须**！虽然发送端不直接处理数据接收，但是由于需要接收流控，因此，底层 CAN 接收中断仍需要调用 `Cus_Cantp_RecieveFrame` 将收到的帧喂给协议栈，协议栈会自动匹配到该发送连接并处理流控帧。

**调用示例**：

```c
    Cus_CANTp_Conn_t *pConn = Cus_Cantp_CreateTxConnection(0x12, 0x18, CANTP_ADDR_MODE_COMMON, (void *)CAN1,Cus_CanTP_canSendFunc_Asynchronous, NULL);
    if ( !pConn )
    {
      for( ; ; );
    }
```

------

- `S8 Cus_Cantp_startTransmit( Cus_CANTp_Conn_t *pConn, const U8 *data, U32 len )`

```c
S8 Cus_Cantp_startTransmit( Cus_CANTp_Conn_t *pConn, const U8 *data, U32 len )
```

**参数**:

- (Cus_CANTp_Conn_t *) pConn：**发起请求的连接控制块**。该连接必须处于空闲状态 (`CONN_IDLE`)，否则函数将返回错误。
- (const U8 *) data：**待发送数据缓冲区**。
- (U32) len：**待发送数据的总长度（字节）**。不能为 0。

**返回值:**

-  <= 0：参数无效(-1) ， 连接对应的通道配置无效(-2)或发送请求提交失败(0)。
- 1：发送请求已提交（单帧或多帧首帧已成功请求 `SendFunc` 发出）。

**描述**：**作为发送端，请求一次 CAN TP 数据发送**。该函数会根据 `len` 的大小，结合当前连接的寻址模式，自动判断采用**单帧 (SF)** 还是**多帧 (FF/CF)** 传输。

**功能细节**：

​	函数内部先校验传入参数的合法性（连接块指针是否非NULL防止空指针解引用，发送数据长度是否非0）。获取连接块对应通道的配置表，通过配置表中的寻址模式计算单帧最大有效载荷。若 `len` 在单帧范围内，调用 `Cus_Cantp_SendSingleFrame` 将数据组装为单帧并通过 `SendFunc` 发出。若 `len` 超过单帧容量，调用 `Cus_Cantp_SendFirstFrame` 组装首帧并通过 `SendFunc` 发出，同时初始化多帧传输相关状态（总长度、剩余字节、SN 序列号等）。

​	首帧发送成功后，协议栈会进入等待流控帧（`CONN_TX_WAIT_FC`）状态，后续由 `Cus_Cantp_MainFunction` 和 `Cus_Cantp_TxConfirmation` 自动推动发送状态机完成连续帧的发送。

**注意事项**：

1.**连接控制块必须处于空闲状态**（`CONN_IDLE`），否则将返回0。

2.发送是**非阻塞**的，本函数及其内部调用函数，仅负责提交发送请求。实际硬件发送完成由中断回调 `Cus_Cantp_TxConfirmation` 通知。在多帧传输中，整个会话由协议栈自动推进，应用层无需轮询。

3.**重要！**启动多帧传输时，该函数内部会记录传入的数据源指针，后续的连续帧(CF)数据段构造将基于该地址进行。因此，**在传输完成之前（收到 `DataIndFunc` 或 `ErrCallback`，或连接回到 IDLE），应用层必须保证 `data` 指向的数据有效且不被修改**。如果数据是栈上临时缓冲区，务必确保在传输完成前不会出栈。

4.多帧传输过程中若发送传输问题（例如 对端SNCode校验失败等），在当前版本下(Ver2.0)会直接将连接置为 IDLE 并结束通信，不会自动重试，也不会通过错误回调通知。此行为在后续版本中可能改进。

**调用示例**：

```c
S8 ret = Cus_Cantp_startTransmit(pConn, data, sizeof(data));
```

---

*API Reference v2.0 结束*

------

## ⚠️注意事项

- **重要约束：CAN TP 启用后的帧管理**：

对于采用 Cus_CAN + CANTP 形式，当前版本下( Ver 2.0 )，一旦启用 CAN TP 协议栈并对某个 CAN ID 建立连接：**所有从硬件接收到的 CAN 帧都会被送入 CAN TP 层进行匹配处理**。一旦接收到的某帧不符合(ISO 15765-2)协议形式，**则由于缓冲区设计原因会被静默丢弃**！

​      因此一但启用CANTP，建议所有通信统一走CANTP协议，不要混用裸CAN数据帧和CANTP协议帧。

​											**( 待续 )**