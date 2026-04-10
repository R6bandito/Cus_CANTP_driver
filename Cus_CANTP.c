#include "Cus_CANTP.h"

static Cus_CANTp_Conn_t ConnPool[MAX_SUPPORT_CONN] = { 0 };

/*  ---------------------------------------------------  */
  Cus_CANTp_Conn_t *Cus_Cantp_GetIdleConn( void );
  void Cus_Cantp_ReleaseConn( Cus_CANTp_Conn_t *pConn );
  void Cus_Cantp_HeartTick( void );
  U32 Cus_Cantp_GetCanID( const Cus_CANTP_Cfg_t *pCfg );
  U32 Cus_Cantp_GetDataLengthFromFF( const U8 *frame );

  U8 Cus_Cantp_SendFirstFrame( Cus_CANTp_Conn_t *pConn, const U8 *data, U32 total_len );

  U8 Cus_Cantp_BuildSingleFrame( U8 *Buffer, const U8 *data, U8 len, U8 ChannelTabID );
  U8 Cus_Cantp_BuildFirstFrame( U8 *Buffer, const U8 *data, U32 total_len, U8 ChannelTabID );
  U8 Cus_Cantp_BuildConsecutiveFrame( U8 *Buffer, const U8 *data, Cus_CANTp_Conn_t *pConn ); 
  U8 Cus_Cantp_BuildFlowControlFrame( U8 *Buffer, Cus_CANTP_FlowState_t flow_State, Cus_CANTp_Conn_t *pConn );

  static void __cus_initial_Conn( Cus_CANTp_Conn_t *pConn );
/*  ---------------------------------------------------  */


Cus_CANTp_Conn_t *Cus_Cantp_GetIdleConn( void )
{
  for( U8 i = 0; i < MAX_SUPPORT_CONN; i++ )
  {
    if ( ConnPool[i].CurrentState == CONN_IDLE )    // 找到空闲块. 返回地址.
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

  U8 BufferPos = 0;
  U8 maxDataLen = 8 - 1;

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
    for( U8 i = BufferPos; i <= maxDataLen; i++ )
    {
      Buffer[i] = 0xCC;
      BufferPos++;
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
  pConn->TxPos = 0;
  pConn->TxMailBoxIndex = 0xFF;
  pConn->BindCANDevice = NULL;
}

