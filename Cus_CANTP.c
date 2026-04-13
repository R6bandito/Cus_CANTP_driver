#include "Cus_CANTP.h"

static Cus_CANTp_Conn_t ConnPool[MAX_SUPPORT_CONN];

/*  ---------------------------------------------------  */
  Cus_CANTp_Conn_t *Cus_Cantp_GetIdleConn( void );
  void Cus_Cantp_ReleaseConn( Cus_CANTp_Conn_t *pConn );
  void Cus_Cantp_HeartTick( void );
  U32 Cus_Cantp_GetCanID( const Cus_CANTP_Cfg_t *pCfg );
  U32 Cus_Cantp_GetDataLengthFromFF( const U8 *frame );
  U8 Cus_Cantp_RecieveFrame( const U8 *data, U8 dlc, U32 canid );

  U8 Cus_Cantp_SendFirstFrame( Cus_CANTp_Conn_t *pConn, const U8 *data, U32 total_len );
  U8 Cus_Cantp_SendSingleFrame( Cus_CANTp_Conn_t *pConn, const U8 *data, U8 len );
  U8 Cus_Cantp_SendFlowControlFrame( Cus_CANTp_Conn_t *pConn, Cus_CANTP_FlowState_t fs );

  U8 Cus_Cantp_BuildSingleFrame( U8 *Buffer, const U8 *data, U8 len, U8 ChannelTabID );
  U8 Cus_Cantp_BuildFirstFrame( U8 *Buffer, const U8 *data, U32 total_len, U8 ChannelTabID );
  U8 Cus_Cantp_BuildConsecutiveFrame( U8 *Buffer, const U8 *data, Cus_CANTp_Conn_t *pConn ); 
  U8 Cus_Cantp_BuildFlowControlFrame( U8 *Buffer, Cus_CANTP_FlowState_t flow_State, Cus_CANTp_Conn_t *pConn );

  static void __cus_initial_Conn( Cus_CANTp_Conn_t *pConn );
  static void __cus_reset_conn_rx_state(Cus_CANTp_Conn_t *pConn);
  static void Cus_Cantp_RxIndication( Cus_CANTp_Conn_t *pConn, U32 canId, const U8 *data, U8 dlc );
  static U8 Cus_Cantp_VerifyIDConn(Cus_CANTp_Conn_t *pConn, U32 canId, const U8 *data);
  static void Cus_Cantp_ProcessSF(Cus_CANTp_Conn_t *pConn, const U8 *data, U8 dlc);
  static void Cus_Cantp_ProcessFF(Cus_CANTp_Conn_t *pConn, const U8 *data, U8 dlc);
  static void Cus_Cantp_ProcessCF(Cus_CANTp_Conn_t *pConn, const U8 *data, U8 dlc);
  static void Cus_Cantp_ProcessFC(Cus_CANTp_Conn_t *pConn, const U8 *data, U8 dlc);
/*  ---------------------------------------------------  */


void Cus_Cantp_ModuleInit( void )
{
  for (U8 i = 0; i < MAX_SUPPORT_CONN; i++) 
  {
    __cus_initial_Conn(&ConnPool[i]);
    ConnPool[i].ConnIndex = -1;
  }
}


Cus_CANTp_Conn_t *Cus_Cantp_GetIdleConn( void )
{
  for( U8 i = 0; i < MAX_SUPPORT_CONN; i++ )
  {
    if ( ConnPool[i].ConnIndex == -1 && ConnPool[i].CurrentState == CONN_IDLE )    // 找到空闲块. 返回地址.
    {
      memset(&ConnPool[i], 0, sizeof(Cus_CANTp_Conn_t));    
      __cus_initial_Conn( &ConnPool[i] );
      ConnPool[i].ConnIndex = i;
      return &ConnPool[i];
    }
  }

  return NULL;        // 所以通道都有活跃会话. 返回NULL.
}


void Cus_Cantp_ReleaseConn( Cus_CANTp_Conn_t *pConn )
{
  if ( pConn && pConn->ConnIndex < MAX_SUPPORT_CONN )
  {
    // 此处不清空结构体. 清空并初始化对应通道 将在 Cus_Cantp_GetIdleConn 中进行.
    pConn->CurrentState = CONN_IDLE;
    pConn->ConnIndex = -1;
  }
}


/* Cus_CANTP 的心跳节拍方法. 请将其放入 SystickHandler 中(或其余定时器 周期性调用). */
void Cus_Cantp_HeartTick( void )
{
  for( U8 i = 0; i < MAX_SUPPORT_CONN; i++ )
  {
    Cus_CANTp_Conn_t *pConn = &ConnPool[i];
    if ( pConn->CurrentState == CONN_IDLE ) continue;

    if ( pConn->Timer_N_Ar > 0 )  pConn->Timer_N_Ar--;
    if ( pConn->Timer_N_As > 0 )  pConn->Timer_N_As--;
    if ( pConn->Timer_N_Bs > 0 )  pConn->Timer_N_Bs--;
    if ( pConn->Timer_N_Cr > 0 )  pConn->Timer_N_Cr--;

    if ( pConn->Timer_StminDelayOnly > 0 )  pConn->Timer_StminDelayOnly--;
  }
}


U8 Cus_Cantp_SendFirstFrame( Cus_CANTp_Conn_t *pConn, const U8 *data, U32 total_len )
{
  if ( !pConn || !data || total_len == 0 || 
              pConn->ConnIndex >= MAX_SUPPORT_CONN || pConn->ConnIndex < 0 ||
                pConn->ChannelConfigTabID >= CHANNEL_CONFIG_TABLE_COUNT )   return 0;   // pConn的配置无效. 返回.

  if ( pConn->CurrentState != CONN_IDLE )   return 0;     // 连接正忙. 返回.

  if ( pConn->SendFunc == NULL )   return 0;          // 发送回调未被注册. 返回.

  Cus_CANTP_Cfg_t *pCfg = Cus_Cantp_GetChannel( pConn->ChannelConfigTabID );
  if ( !pCfg )    return 0;

  U8 Buffer[8] = { 0 };

  U8 Return = Cus_Cantp_BuildFirstFrame( Buffer, data, total_len, pConn->ChannelConfigTabID );
  if ( Return != 8 )  return 0;     // 帧构建失败. 返回.

  U32 canid = Cus_Cantp_GetCanID(pCfg);
  if ( canid == 0 )   return 0;   // ID构建失败.

  if ( pConn->SendFunc(pConn, canid, Buffer, 8) != 1 )
  {
    // 发送失败. 保持原状态返回.
    return 0;
  }

  U8 FF_payload = 0;
  if ( pCfg->TxDLC == 8 && total_len <= 4095 )  
  {
    FF_payload = (pCfg->AddrMode == NORMAL_ADDRESS_MODE) ? 6 : 5; // 常规长度数据包.根据寻址模式选择携带用户数据包大小
  }
  if ( pCfg->TxDLC == 8 && total_len > 4095 )
  {
    FF_payload = 0;   // 超长包首帧不携带任何用户数据.
  }

  // 首帧发送成功. 修改状态.
  pConn->TotalSize = total_len;
  pConn->TxBytes = FF_payload;
  pConn->Remaining = total_len - pConn->TxBytes;   
  pConn->TxPos = pConn->TxBytes;    // 由于发送数据的索引从0开始. 因此TxPos就等于TxBytes. 无需额外加1.
  pConn->SN_Code++;   // 首帧 SN 为0 后面首个CF SN为1.
  pConn->CurrentState = CONN_TX_FF;   // 等待底层发送确认.

  // 启动 N_Bs定时器.
  pConn->Timer_N_Bs = TIMER_NBS;

  return 1;
}


U8 Cus_Cantp_SendSingleFrame( Cus_CANTp_Conn_t *pConn, const U8 *data, U8 len )
{
  if ( !pConn || !data || pConn->ConnIndex >= MAX_SUPPORT_CONN || 
            pConn->ChannelConfigTabID >= CHANNEL_CONFIG_TABLE_COUNT )   return 0;

  if ( pConn->CurrentState != CONN_IDLE )   return 0;     // 忙状态.
  
  Cus_CANTP_Cfg_t *pCfg = Cus_Cantp_GetChannel(pConn->ChannelConfigTabID);
  if ( !pCfg )   return 0;

  // 实际上，此处已由上层判断该发送单帧. 此处再次进行审查.
  U8 ValidMaxlen;
  ValidMaxlen = (pCfg->AddrMode == NORMAL_ADDRESS_MODE) ? 7 : 6;  // EXT寻址模式下. SF帧经典CAN能承载的用户数据段为6个字节.
  if ( len > ValidMaxlen )    return 0;       // 单帧无法容纳. 返回.

  U8 buffer[8];
  U8 ret_dlc = Cus_Cantp_BuildSingleFrame(buffer, data, len, pConn->ChannelConfigTabID);
  if ( ret_dlc == 0 )   return 0;

  U32 Canid;
  Canid = Cus_Cantp_GetCanID(pCfg);
  if ( Canid == 0 )   return 0;
  
  if ( pConn->SendFunc && pConn->SendFunc(pConn, Canid, buffer, ret_dlc) != 0 )
  {
    // 发送成功.
    pConn->CurrentState = CONN_TX_SF;      // 状态变化为发送单帧. 等待确认.
    return 1;
  }

  return 0;
}


U8 Cus_Cantp_SendFlowControlFrame( Cus_CANTp_Conn_t *pConn, Cus_CANTP_FlowState_t fs )
{
  if ( !pConn || pConn->ConnIndex >= MAX_SUPPORT_CONN || pConn->ChannelConfigTabID >= CHANNEL_CONFIG_TABLE_COUNT )    return 0;

  if ( pConn->CurrentState != CONN_RX_FF
      && pConn->CurrentState != CONN_RX_WAIT_CF
      && pConn->CurrentState != CONN_RX_CF )   return 0;    // 不处于接收流程中. 返回.

  U8 Buffer[8];
  U8 ret_dlc = Cus_Cantp_BuildFlowControlFrame(Buffer, fs, pConn);
  if ( ret_dlc == 0 )   return 0;

  Cus_CANTP_Cfg_t *pCfg = Cus_Cantp_GetChannel(pConn->ChannelConfigTabID);
  if ( !pCfg )  return 0;
  U32 Canid = Cus_Cantp_GetCanID(pCfg);
  if ( Canid == 0 )   return 0;

  if ( pConn->SendFunc && pConn->SendFunc(pConn, Canid, Buffer, ret_dlc) != 0 )
  {
    // 发送完成.
    if ( fs == FLOW_CTS )
    {
      // 更新状态. 启动N_Cr定时器.
      pConn->CurrentState = CONN_RX_WAIT_CF;
      pConn->RemainingBS = pConn->BS;
      pConn->Timer_N_Cr = TIMER_NCR;
    }
    else if ( fs == FLOW_OVFLW || fs == FLOW_WAIT )
    {
      // 传输中止. 此处为了简化，将FLOW_WAIT状态并入FLOW_OVFLW进行处理.
      pConn->CurrentState = CONN_IDLE;
    }
  }

  return 1;
}



/* ****************************************************************************** */
/* ****************************************************************************** */

U8 Cus_Cantp_BuildSingleFrame( U8 *Buffer, const U8 *data, U8 len, U8 ChannelTabID )
{
  if ( !Buffer || !data || len == 0 || ChannelTabID >= CHANNEL_CONFIG_TABLE_COUNT )  return 0;

  Cus_CANTP_Cfg_t *pCfg = Cus_Cantp_GetChannel(ChannelTabID);
  if ( !pCfg )  return 0;

  U8 maxDataLen = 0;
  U8 pciOffset = 0;
  U8 BufferPos = 0;

  switch (pCfg->AddrMode)
  {
  case NORMAL_ADDRESS_MODE:   // 普通模式.
    {
      if ( pCfg->TxDLC == 8 ) 
      {
        maxDataLen = 8 - 1;     // 经典CAN为8字节数据段. 如果TxDLC < 8. 依然填充到8字节.
        if ( len > maxDataLen )   return 0;   // 单帧放不下. 返回.   
        Buffer[BufferPos++] = (0x00 | (len & 0x0F));    // PCI 信息块写入.
        for( U8 i = 0; i < len; i++ )
        {
          Buffer[BufferPos] = data[i];
          BufferPos++;
        }
        if ( len < maxDataLen )     // 发送的数据不足一个单帧所能承载的数据.剩余位填充为0.
        {
          for( U8 i = BufferPos; i < pCfg->TxDLC; i++ )
          {
            Buffer[i] = 0xCC;
          }
        }
        return pCfg->TxDLC;     
      }
      break;
    }

  case EXT_ADDRESS_MODE:
    {
      pciOffset = 1;        // 拓展寻址下. 地址信息占一个字节，PCI信息偏移到第二个字节.
      BufferPos += 2;       // 提前移动bufferpos，前面两个字节分别划给 地址信息 和 PCI.
      if ( pCfg->TxDLC == 8 )   // 经典CAN.
      {
        maxDataLen = 8 - 2; // 第一个字节是地址，第二个字节是PCI.
        if ( len > maxDataLen )   return 0;
        // 地址信息写入. 将TA(8位)放入数据场第一个字节.
        Buffer[0] = (pCfg->N_AI.TA & 0xFF);
        Buffer[pciOffset] = (0x00 | (len & 0x0F));  // PCI写入.
        for( U8 i = 0; i < len; i++ )
        {
          Buffer[BufferPos] = data[i];
          BufferPos++;
        }
        if ( len < maxDataLen )
        {
          for( U8 i = BufferPos; i < pCfg->TxDLC; i++ )
          {
            Buffer[i] = 0xCC;
            BufferPos++;    // 为了统一. 此处也作自增.
          }
        }
        return pCfg->TxDLC;
      }
      break;
    }
  
  default:
    break;
  }

  return 0;
}


U8 Cus_Cantp_BuildFirstFrame( U8 *Buffer, const U8 *data, U32 total_len, U8 ChannelTabID )
{
  if ( !Buffer || !data || ChannelTabID >= CHANNEL_CONFIG_TABLE_COUNT )   return 0;

  Cus_CANTP_Cfg_t *pCfg = Cus_Cantp_GetChannel( ChannelTabID );
  if ( !pCfg )  return 0;

  if ( ( pCfg->TxDLC == 8 && total_len <= 7 && pCfg->AddrMode == NORMAL_ADDRESS_MODE )
        || ( pCfg->TxDLC == 8 && total_len <= 6 && pCfg->AddrMode != NORMAL_ADDRESS_MODE ) )  return 0;   // 经典CAN情况. 可以走单帧发送. 不构造FF帧.

  if ( (pCfg->TxDLC > 8 && total_len <= pCfg->TxDLC - 1) )    return 0;   // CAN FD情况 同样一帧就能发完. 走单帧发送，不构造FF帧.

  U8 maxDataLen = 0;
  U8 BufferPos = 0;
  U8 pciOffset = 0;

  switch (pCfg->AddrMode)
  {
  case NORMAL_ADDRESS_MODE:
    {
      pciOffset = 0;        // 普通寻址模式. PCI无偏移.
      if ( pCfg->TxDLC == 8 && total_len <= 4095 )   // 经典CAN情况.
      {
        maxDataLen = 8 - 2;     // FF PCI占2字节.
        U8 byte0_high4Bit = (0x01 << 4) & 0xF0;
        U8 byte0_low4Bit = (total_len >> 8) & 0x0F;  // FF_DL的高四位.
        Buffer[0] = byte0_high4Bit | byte0_low4Bit;   // 写入PCI第一个字节.
        BufferPos++;

        U8 byte1_8bit = (total_len & 0xFF);   // 写入PCI第二个字节.
        Buffer[1] = byte1_8bit;
        BufferPos++;

        for( U8 i = 0; i < maxDataLen; i++ )    // 剩余还能带6个字节用户数据.
        {
          Buffer[BufferPos] = data[i];
          BufferPos++;
        }
        return pCfg->TxDLC;
      }

      if ( pCfg->TxDLC == 8 && total_len > 4095 )   // 超长数据包.
      {
        maxDataLen = 0;     // 4字节 FF_DL, 2字节 PCI. 剩余字节不用作负载用户数据.
        Buffer[BufferPos++] = (0x01UL << 4);  
        Buffer[BufferPos++] = (0x00);    

        // 按大端字节序 放入FF_DL.
        Buffer[BufferPos++] = (total_len >> 24) & 0xFF;
        Buffer[BufferPos++] = (total_len >> 16) & 0xFF;
        Buffer[BufferPos++] = (total_len >> 8) & 0xFF;
        Buffer[BufferPos++] = total_len & 0xFF;

        Buffer[BufferPos++] = 0xCC;   // 剩余两字节不携带用户数据 默认填充.
        Buffer[BufferPos++] = 0xCC;

        return pCfg->TxDLC;
      }

      break;
    }

  default:
    break;
  }

  return 0;
}


U8 Cus_Cantp_BuildConsecutiveFrame( U8 *Buffer, const U8 *data, Cus_CANTp_Conn_t *pConn )
{
  if ( !Buffer || !data || !pConn || pConn->ConnIndex < 0 )   return 0;

  Cus_CANTP_Cfg_t *pCfg = Cus_Cantp_GetChannel(pConn->ChannelConfigTabID);
  if ( !pCfg )  return 0;

  if ( pCfg->TxDLC != 8 )   return 0;     // 该 API 为经典CAN 相关API. 使用 CANFD，请调用 Cus_Cantp_BuildConsecutiveFrame_CanFD.

  U8 pciOffset = (pCfg->AddrMode == NORMAL_ADDRESS_MODE) ? 1 : 2;
  U8 BufferPos = 0;
  U8 maxDataLen = 8 - pciOffset;

  if ( pCfg->AddrMode == EXT_ADDRESS_MODE )
  {
    Buffer[BufferPos++] = pCfg->N_AI.TA;  // 拓展寻址模式. 将TA写进第一个字节.
  }

  U8 byte0_high4Bit = (((0x01 << 1) << 4) & 0xF0);   // CF帧类型识别码.
  U8 byte0_low4Bit = (pConn->SN_Code & 0x0FUL);   // SN.

  Buffer[BufferPos++] = byte0_high4Bit | byte0_low4Bit;

  U8 copylen = (pConn->Remaining <= maxDataLen) ? pConn->Remaining : maxDataLen;
  for( U8 i = 0; i < copylen; i++ )
  {
    Buffer[BufferPos++] = data[(pConn->TxPos) + i];   // 注意！此处只使用 Cus_CANTp_Conn_t 的数据，而不更新状态！ 所有状态均在发送/接收API中统一更新.
  }

  if ( copylen != maxDataLen )  
  {                               // 用默认值填补到一个标准8字节数据段.
    while ( BufferPos < 8 ) 
    {
      Buffer[BufferPos++] = 0xCC;
    }
  }

  if ( BufferPos != 8 )       // BufferPos 错位. 帧构造内部可能已经出现未知错乱. 清空帧后退出.(极低概率).
  {
    memset(Buffer, 0xCC, 8);
    return 0;
  }

  return pCfg->TxDLC;
}


U8 Cus_Cantp_BuildFlowControlFrame( U8 *Buffer, Cus_CANTP_FlowState_t flow_State, Cus_CANTp_Conn_t *pConn )
{
  if ( !Buffer || !pConn || pConn->ConnIndex < 0 )  return 0;

  U8 BufferPos = 0;
  U8 byte0_high4Bit = (((0x03UL) << 4) & 0xF0UL );
  U8 byte0_low4Bit = 0;
  switch (flow_State)
  {
    case FLOW_CTS:  byte0_low4Bit = 0x00;   break;
    case FLOW_WAIT: byte0_low4Bit = (0x01 & 0x0F); break;
    case FLOW_OVFLW:  byte0_low4Bit = ((0x01 << 1) & 0x0F); break;
  
    default:  return 0; 
  }

  Buffer[BufferPos++] = byte0_high4Bit | byte0_low4Bit;
  Buffer[BufferPos++] = (pConn->BS & 0xFF);
  Buffer[BufferPos++] = (pConn->STmin & 0xFF);

  while( BufferPos < 8 )
  {
    // FC帧不携带用户数据. 剩余字节全部默认填充.
    Buffer[BufferPos++] = 0xCC;
  }

  return Cus_Cantp_GetChannel(pConn->ChannelConfigTabID)->TxDLC;
}

/* ****************************************************************************** */
/* ****************************************************************************** */



U32 Cus_Cantp_GetDataLengthFromFF( const U8 *frame )
{
  if ( !frame )   return 0;

  U8 byte0 = frame[0];
  U8 byte1 = frame[1];

  if ( (byte0 >> 4) != 0x01 )   return 0;         // 帧识别码错误. 该帧不是FF帧.

  if ( (byte0 == (0x01 << 4)) && (byte1 == 0) )   // 超长数据包形式.( > 4095 )
  {
    // 扩展格式：4 字节大端长度
    return ((U32)frame[2] << 24) |
    ((U32)frame[3] << 16) |
    ((U32)frame[4] << 8)  |
    (U32)frame[5];
  }

  return  (U32)(((U16)(byte0 & 0x0F) << 8) | (U16)byte1); 
}


U32 Cus_Cantp_GetCanID( const Cus_CANTP_Cfg_t *pCfg )
{
  if ( !pCfg )    return 0;

  if ( pCfg->N_AI.TA_Type != 0 && pCfg->N_AI.TA_Type != 1 )   return 0;   // TA_Type参数错误.

  if ( pCfg->N_AI.TA_Type != 0 )  return pCfg->FunctionalCanID;           // 地址类型为功能寻址. 直接返回FunctionalID.

  if ( pCfg->AddrMode == NORMAL_ADDRESS_MODE )
  {
    // 普通寻址模式. N_AI映射到 CANID 段.
    // 0位为 TA_Type. 1~6位为 TA. 7 ~ 11位为 SA.共组成11位标准CANID.
    U32 RealCANID = (((U32)pCfg->N_AI.SA & 0x1FUL) << 6 ) | (((U32)pCfg->N_AI.TA & 0x1FUL) << 1) | ((U32)pCfg->N_AI.TA_Type & 0x01UL);
    return RealCANID & 0x7FFUL;     // 确保地址不超过11位范围.
  }

  if ( pCfg->AddrMode == EXT_ADDRESS_MODE )
  {
    // 拓展寻址模式. 8位TA移入数据段. 剩余8位SA + 1位TAType.
    // 11位ID分配: SA(8位) + TAType(1位) + 2填充(填充为0).
    // 拓展寻址. 待实现.
    U32 RealCANID = ((U32)pCfg->N_AI.SA << 3) | ((U32)(pCfg->N_AI.TA_Type & 0x01UL) << 2) & 0xFCUL; 
    RealCANID &= 0x7FFUL;

    return RealCANID;
  }

  if ( pCfg->AddrMode == MIXED_ADDRESS_MODE )
  {
    // 混合寻址. 待实现.
    return 0;
  }

  return 0;
}



static void Cus_Cantp_RxIndication( Cus_CANTp_Conn_t *pConn, U32 canId, const U8 *data, U8 dlc )
{
  if ( !data || dlc < 8 || !pConn 
        || pConn->ChannelConfigTabID >= CHANNEL_CONFIG_TABLE_COUNT 
          || pConn->ConnIndex >= MAX_SUPPORT_CONN )   return;   // 由于填充机制. 所有小于8的帧都将自动填充到8字节. 因此dlc必须 >= 8.

  if ( dlc == 8 )
  {
    // 经典CAN处理方式. 非CAN FD.
    Cus_CANTP_Cfg_t *pCfg = Cus_Cantp_GetChannel(pConn->ChannelConfigTabID);
    if ( !pCfg )  return;
    U8 Return = Cus_Cantp_VerifyIDConn(pConn, canId, data);    
    if ( Return == 0 )    return;       // 接收到的帧ID 与 目标通信模块的帧ID不匹配. 返回.

    U8 pciIndex = (pCfg->AddrMode == NORMAL_ADDRESS_MODE) ? 0 : 1;
    U8 byte0_high4Bit = (data[pciIndex] & 0xF0); // 取出Byte0 高四位(帧类型识别码).
    if ( byte0_high4Bit == 0x00 )  
    {
      pConn->CurrentState = CONN_RX_SF;
      Cus_Cantp_ProcessSF(pConn, data, dlc);   // SF帧 转发给SF处理机制.
    }

    if ( byte0_high4Bit == 0x01 )
    {
      pConn->CurrentState = CONN_RX_FF;
      Cus_Cantp_ProcessFF(pConn, data, dlc);   // FF帧 转发给FF处理机制.
    }

    if ( byte0_high4Bit == (0x01 << 1) )
    {
      pConn->CurrentState = CONN_RX_CF;
      Cus_Cantp_ProcessCF(pConn, data, dlc);    // CF帧 转发给CF处理机制.
    }

    if ( byte0_high4Bit == (0x03) )
    {
      Cus_Cantp_ProcessFC(pConn, data, dlc);    // FC帧 转发给流控处理.
    }
  }
  else if ( dlc > 8 )
  {
    // CAN FD处理方式.
  }
}



static U8 Cus_Cantp_VerifyIDConn(Cus_CANTp_Conn_t *pConn, U32 canId, const U8 *data)
{
  if ( !pConn || pConn->ConnIndex >= MAX_SUPPORT_CONN 
                || pConn->ChannelConfigTabID >= CHANNEL_CONFIG_TABLE_COUNT || !data )   return 0;

  Cus_CANTP_Cfg_t *pCfg = Cus_Cantp_GetChannel(pConn->ChannelConfigTabID);
  if ( !pCfg )  return 0;

  U32 expectedID = Cus_Cantp_GetCanID(pCfg);    // 计算当前连接预期的 CAN ID.
  if ( expectedID == 0 )  return 0;

  // 比对 CAN ID（注意：功能寻址时 expectedCanId 为 FunctionalCanID）
  if ( expectedID != canId )  return 0;   // CAN ID数据长 不匹配. 直接返回.

  if ( pCfg->AddrMode == EXT_ADDRESS_MODE )
  {
    // 对于拓展寻址. 还应该额外检查数据段第一个字节(TA).
    if ( data[0] != pCfg->N_AI.TA )   return 0;
  }

  return 1;
}



static void Cus_Cantp_ProcessSF(Cus_CANTp_Conn_t *pConn, const U8 *data, U8 dlc)
{
  if ( !pConn || pConn->ConnIndex >= MAX_SUPPORT_CONN || pConn->ChannelConfigTabID >= CHANNEL_CONFIG_TABLE_COUNT )  return;

  if ( pConn->CurrentState != CONN_RX_SF )  return;   // 不处于 CONN_RX_SF 状态. 返回.

  (void)dlc;    // 在不实现 CAN FD的情况下，dlc参数冗余. 此处仅为后续可能的拓展留出接口.

  Cus_CANTP_Cfg_t *pCfg = Cus_Cantp_GetChannel(pConn->ChannelConfigTabID);
  if ( !pCfg )  return;

  U8 sf_len;
  const U8 *payload = NULL;
  switch (pCfg->AddrMode)
  {
  case NORMAL_ADDRESS_MODE:   
    {
      sf_len = (data[0] & 0x0F);  
      if ( sf_len == 0 || sf_len > 7 )  return;     // 无效长度 直接返回.
      payload = &data[1];   // PCI 信息字节在 Byte0.
      pConn->CurrentState = CONN_IDLE;              // SF帧通信结束. 控制块回到IDLE状态.
      pConn->DataIndFunc(pConn, payload, sf_len);   // 通知上层取数据.
      __cus_reset_conn_rx_state(pConn);             // SF 单帧通信结束. 打扫控制块状态.
      break;
    }
  
  case EXT_ADDRESS_MODE:
    {
      sf_len = (data[1] & 0x0F);      // EXT模式下. 数据段中 Byte0存的是TA. 因此SF_DL从Byte1开始.
      if ( sf_len == 0 || sf_len > 6 )  return;     // 无效长度 返回.
      payload = &data[2];   // Byte0是 地址信息. Byte1是 PCI信息. 数据段从Byte2开始.
      pConn->CurrentState = CONN_IDLE;
      pConn->DataIndFunc(pConn, payload, sf_len);   
      __cus_reset_conn_rx_state(pConn);
      break;
    }
  default:  return;
  }
}



static void Cus_Cantp_ProcessFF(Cus_CANTp_Conn_t *pConn, const U8 *data, U8 dlc)
{
  if ( !pConn || pConn->ChannelConfigTabID >= CHANNEL_CONFIG_TABLE_COUNT 
               || pConn->ConnIndex >= MAX_SUPPORT_CONN || !data )     return;

  if ( pConn->CurrentState != CONN_RX_FF )    return;   // 状态不正确. 返回.

  (void)dlc;

  Cus_CANTP_Cfg_t *pCfg = Cus_Cantp_GetChannel(pConn->ChannelConfigTabID);
  if ( !pCfg )  
  {
    pConn->CurrentState = CONN_IDLE; 
    return;
  }

  U32 total_length = Cus_Cantp_GetDataLengthFromFF(data);
  if ( !total_length ) 
  {
    pConn->CurrentState = CONN_IDLE; 
    return;
  } 

  if ( !pConn->pRecvBuffer || pConn->RecvBuffer_Size < total_length )
  {
    // 缓冲区不足. 发送OVFLW. 并结束掉通信.
    Cus_Cantp_SendFlowControlFrame(pConn, FLOW_OVFLW);
    pConn->CurrentState = CONN_IDLE;    // 通信已经结束.恢复IDLE状态.
    return;
  }

  U8 ff_payload;
  const U8 *payload = NULL;
  if ( total_length <= 4095 )
  {
    ff_payload = (pCfg->AddrMode == NORMAL_ADDRESS_MODE) ? 6 : 5;   // 经典CAN下 根据寻址模式不同,携带 6/5 字节负载.
    payload = (pCfg->AddrMode == NORMAL_ADDRESS_MODE) ? &data[2] : &data[3];
  }
  else if ( total_length > 4095 )
  {
    // 超长数据包形式.
    ff_payload = 0;   // 超长包形式不携带用户数据负载.
  }

  if ( payload != NULL )
  {
    // 将FF帧中数据拷贝入缓冲区后. 更新状态.
    memcpy(pConn->pRecvBuffer, payload, ff_payload);
  }
  pConn->TotalSize = total_length;
  pConn->RxBytes = ff_payload;
  pConn->RxPos = ff_payload;
  pConn->SN_Code++;     // 接收到FF的SN为0. 下一帧期望的CF SNCODE应该为1.
  pConn->Remaining = pConn->TotalSize - pConn->RxBytes;

  Cus_Cantp_SendFlowControlFrame(pConn, FLOW_CTS);    // 发送流控.(内部置起 CONN_RX_WAIT_CF)
}


static void Cus_Cantp_ProcessCF(Cus_CANTp_Conn_t *pConn, const U8 *data, U8 dlc)
{
  if ( !pConn || pConn->ChannelConfigTabID >= CHANNEL_CONFIG_TABLE_COUNT 
               || pConn->ConnIndex >= MAX_SUPPORT_CONN || !data )    return;

  if ( pConn->CurrentState != CONN_RX_CF )    return;     // 状态不正确.
  (void) dlc;           // 冗余参数. 为后续留出接口.

  Cus_CANTP_Cfg_t *pCfg = Cus_Cantp_GetChannel(pConn->ChannelConfigTabID);
  if ( !pCfg )  
  {
    pConn->CurrentState = CONN_IDLE;
    return;
  }

  U8 pciOffset = (pCfg->AddrMode == NORMAL_ADDRESS_MODE) ?  1 : 2;
  U8 SnCode;
  if ( pciOffset == 1 )
  {
    SnCode = (data[0] & 0x0F);     // 普通寻址模式. PCI在第一个字节.获取低四位的SN码.
  }
  else if ( pciOffset == 2 )
  {
    SnCode = (data[1] & 0x0F);    // 拓展寻址模式. PCI在第2个字节.获取低四位的SN码.
  }
  else 
  {
    pConn->CurrentState = CONN_IDLE;
    return;
  }

  if ( pConn->SN_Code != SnCode )   // SN校验出错. 当前帧SN码与接收机期望SN码不相同. 中止接收，更新状态返回.
  {
    Cus_Cantp_SendFlowControlFrame(pConn, FLOW_OVFLW);    // 通知发送方中止发送. 内部置 IDLE 状态.
    __cus_reset_conn_rx_state(pConn);     // 重置接收状态.     

    return;
  }

  // SN校验成功. 接收该帧数据.
  U8 *pData = &data[pciOffset];
  U8 data_len = 8 - pciOffset;    // 除去PCI（可能的TA）后,剩余数据段大小.
  U8 copylen = ( pConn->Remaining <= data_len ) ? (pConn->Remaining) : data_len;
  memcpy(&pConn->pRecvBuffer[pConn->RxPos], pData, copylen);

  // 更新接收后状态.
  pConn->RxBytes += copylen;
  pConn->RxPos += copylen;
  pConn->Remaining -= copylen;
  pConn->SN_Code++;   // 下一帧期望的SNCODE.

  if ( pConn->BS > 0 && pConn->RemainingBS > 0 )  pConn->RemainingBS--;   // 块计数器处理.

  // 判断是否接收完成.
  if ( pConn->Remaining == 0 )
  {
    // 所有数据已接收完毕. 通知上层.
    if ( pConn->DataIndFunc )
    {
      pConn->DataIndFunc(pConn, pConn->pRecvBuffer, pConn->RxBytes);
    }
    pConn->CurrentState = CONN_IDLE;
    __cus_reset_conn_rx_state(pConn);
    return;
  }

  // 未接收完毕.
  if ( pConn->BS > 0 && pConn->RemainingBS == 0 )
  {
    // 需要向接收方发送新的流控帧.
    // Ps: Cus_Cantp_SendFlowControlFrame 内部会设置 CONN_RX_WAIT_CF状态 以及重新启动 Ncr 定时器.
    Cus_Cantp_SendFlowControlFrame(pConn, FLOW_CTS);    // 向接收方发送新的FC来允许下一个块.
  }
  else 
  {
    // 单个CF接收完毕. 仍在同一块内，重置定时器.继续等待下一CF.
    pConn->Timer_N_Cr = TIMER_NCR;
  }
}


static void Cus_Cantp_ProcessFC(Cus_CANTp_Conn_t *pConn, const U8 *data, U8 dlc)
{
  if ( !pConn || pConn->ChannelConfigTabID >= CHANNEL_CONFIG_TABLE_COUNT 
               || pConn->ConnIndex >= MAX_SUPPORT_CONN || !data )   return;

  (void) dlc;   // FC帧不携带用户数据. 所以无论是经典CAN还是CAN FD处理方式是一样的. 此处dlc参数仅作为预留拓展接口占位.

  if ( pConn->CurrentState != CONN_TX_WAIT_FC )   return;   // 状态不对返回. 接收流控只针对作为发送方(Sender)而言.

  Cus_CANTP_Cfg_t *pCfg = Cus_Cantp_GetChannel(pConn->ChannelConfigTabID);
  if ( !pCfg )  return;

  U8 pciOffset = (pCfg->AddrMode == NORMAL_ADDRESS_MODE) ? 1 : 2;
  S8 FlowState = -1;
  if ( pciOffset == 1 )
  {
    // 普通寻址模式 PCI信息字节在第一个字节.
    FlowState = (data[0] & 0x0F);
  }
  else if ( pciOffset == 2 )
  {
    // 拓展寻址模式 PCI信息字节在第二个字节.
    FlowState = (data[1] & 0x0F); 
  } 
  else return;

  pConn->BS = data[pciOffset];    // 取出 流控帧 中携带的BS.
  pConn->RemainingBS = (pConn->BS == 0) ? 0xFFFF : pConn->BS;   
  pConn->STmin = data[pciOffset + 1];   // 取出 流控帧 中携带的STmin.

  switch ((Cus_CANTP_FlowState_t)FlowState)
  {
  case FLOW_CTS:  pConn->CurrentState = CONN_TX_CF; break;    // 转换状态. 开启CF发送.

  case FLOW_OVFLW: pConn->CurrentState = CONN_IDLE; break;    // 中止传输.

  case FLOW_WAIT: pConn->CurrentState = CONN_IDLE;  break;    // 此处为简化处理，暂时将WAIT与OVFLW合并处理.
  
  default:    return;
  }
}



static void __cus_initial_Conn( Cus_CANTp_Conn_t *pConn )
{
  if ( !pConn )   return;

  pConn->BS = 0;
  pConn->STmin = 0;

  pConn->CAN_ID = 0;
  pConn->RxBytes = 0;
  pConn->TxBytes = 0;
  pConn->Remaining = 0;
  pConn->RemainingBS = 0;
  pConn->SN_Code = 0;
  pConn->RxPos = 0;
  pConn->Timer_StminDelayOnly = 0;
  pConn->pRecvBuffer = NULL;
  pConn->RecvBuffer_Size = 0;
  pConn->ConnIndex = -1;      // 先显示设置为-1. 而后由池进行分配.

  pConn->Timer_N_Ar = 0;
  pConn->Timer_N_As = 0;
  pConn->Timer_N_Bs = 0;
  pConn->Timer_N_Cr = 0;

  pConn->CurrentState = CONN_IDLE;
  pConn->ChannelConfigTabID = 0;
  pConn->SendFunc = NULL;
  pConn->RecvFunc = NULL;
  pConn->DataIndFunc = NULL;
  pConn->TxPos = 0;
  pConn->TxMailBoxIndex = 0xFF;
  pConn->BindCANDevice = NULL;
}


/* 接收方通信信息状态重置API */
static void __cus_reset_conn_rx_state(Cus_CANTp_Conn_t *pConn)
{
  if ( !pConn )  return;

  pConn->TotalSize = 0;
  pConn->Remaining = 0;
  pConn->RemainingBS = 0;
  pConn->RxBytes = 0;
  pConn->RxPos = 0;
  pConn->SN_Code = 0;
  pConn->Timer_N_Ar = 0;
  pConn->Timer_N_Cr = 0;
}

