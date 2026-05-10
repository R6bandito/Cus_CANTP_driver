#include "Cus_CANTP_Utils.h"

/* ******************************* */
  static volatile uint32_t g_cus_canUtils_heartTick;
/* ******************************* */


/* ************************************ Utils ************************************* */
  int8_t Cus_CAN_Utils_RecevieCANFrame( CAN_TypeDef *canDevice, uint8_t *buffer, uint32_t buffer_size, Cus_Can_Frame_t *rxFrame, uint8_t FIFOx );
  int8_t Cus_CAN_Utils_SendCANFrame_Async( CAN_TypeDef *canDevice, uint8_t *buffer, uint32_t buffer_size, Cus_Can_Frame_t *txFrame, uint8_t TxMailBoxIndex, uint8_t *pUsedMailBox );
  int8_t Cus_CAN_Utils_SendCANFrame_Synch( CAN_TypeDef *canDevice, uint8_t *buffer, uint32_t buffer_size, Cus_Can_Frame_t *txFrame, uint8_t TxMailBoxIndex, uint8_t *pUsedMailBox );

  void Cus_CAN_Utils_IncreaseTick( void );
  static uint32_t Cus_CAN_Utils_GetTick( void );
  static void Cus_CAN_Utils_Delay( uint32_t delay_ms );
/* ************************************ Utils ************************************* */


#define BASSIC_ASSERT( cond )                                     \
    do {                                                            \
        if ( !(cond) )                                            \
        {                                                           \
          printf("ASSERT_FAILED: %s : %d", __FILE__, __LINE__);   \
          __asm volatile("bkpt 0"); /* ARM Cortex-M */              \
          while(1);                                               \
        }                                                           \
    } while(0)


int8_t Cus_CAN_Utils_RecevieCANFrame( CAN_TypeDef *canDevice, uint8_t *buffer, uint32_t buffer_size, Cus_Can_Frame_t *rxFrame, uint8_t FIFOx )
{
  if ( !buffer || !rxFrame || !canDevice )    return -1;

  /* 缓冲区最起码要能够完整装入一帧 8 字节CAN报文 */
  BASSIC_ASSERT(buffer_size >= 8);

  volatile uint32_t *reg_RFxR = (FIFOx == CUS_CAN_UTILS_FIFO0) ? (uint32_t *)&canDevice->RF0R : (uint32_t *)&canDevice->RF1R;
  CAN_FIFOMailBox_TypeDef *mbox = (FIFOx == CUS_CAN_UTILS_FIFO0) ? &canDevice->sFIFOMailBox[CUS_CAN_UTILS_FIFO0] 
                                                                    : &canDevice->sFIFOMailBox[CUS_CAN_UTILS_FIFO1];

  /* 检查接收邮箱中是否有报文待处理 */
  if ( (*reg_RFxR & CAN_RF0R_FMP0_Msk) == 0 )   return -2; 

  /* 提取报文ID类型 */
  rxFrame->ID_Type = mbox->RIR & CAN_RI0R_IDE_Msk;

  /* 提取报文RTR类型 */
  rxFrame->RTR = mbox->RIR & CAN_RI0R_RTR_Msk; 

  if ( rxFrame->ID_Type == CUS_CAN_UTILS_IdTYPE_STD )
  {
    /* 提取 11 位标准ID */
    rxFrame->ID = (mbox->RIR >> 21) & 0x7FF;
  }
  else if ( rxFrame->ID_Type == CUS_CAN_UTILS_IdTYPE_EXTI )
  {
    /* 提取 29位拓展ID */
    rxFrame->ID = (mbox->RIR >> 3) & 0x1FFFFFFF;
  }

  /* 提取报文长度DLC */
  rxFrame->DLC = mbox->RDTR & CAN_RDT0R_DLC_Msk;

  /* 提取数据段 */
  uint32_t RDLR_Data = mbox->RDLR;
  uint32_t RDHR_Data = mbox->RDHR;

  memcpy(&buffer[0], &RDLR_Data, 4);
  memcpy(&buffer[4], &RDHR_Data, 4);

  /* 对RFOM位置1，释放邮箱 */
  *reg_RFxR |= CAN_RF0R_RFOM0_Msk;

  return 0; 
}


int8_t Cus_CAN_Utils_SendCANFrame_Async( CAN_TypeDef *canDevice, uint8_t *buffer, uint32_t buffer_size, Cus_Can_Frame_t *txFrame, uint8_t TxMailBoxIndex, uint8_t *pUsedMailBox )
{
  if ( !canDevice || !buffer || !txFrame )    return -1;

  BASSIC_ASSERT((buffer_size) != 0 && (buffer_size >= txFrame->DLC));
  BASSIC_ASSERT(TxMailBoxIndex == CUS_CAN_UTILS_TXMAILBOX_NONE || 
    TxMailBoxIndex <= CUS_CAN_UTILS_TXMAILBOX_2);

  CAN_TxMailBox_TypeDef *txmbox;
  if ( TxMailBoxIndex != CUS_CAN_UTILS_TXMAILBOX_NONE )
  {
    /* 指定了发送邮箱 */
    if ( canDevice->TSR & (CAN_TSR_TME0_Msk << TxMailBoxIndex) )
    {
      /* 指定发送邮箱空闲 */
      txmbox = &canDevice->sTxMailBox[TxMailBoxIndex];
      if ( pUsedMailBox )   *pUsedMailBox = TxMailBoxIndex;
    }
    else 
    {
      /* 指定邮箱非空闲. 直接返回(不阻塞) */
      return -2;
    }

  }
  else 
  {
    /* 未指定发送邮箱 */
    if ( canDevice->TSR & CAN_TSR_TME0_Msk )
    {
      /* 邮箱0空闲, 使用邮箱0 */
      txmbox = &canDevice->sTxMailBox[CUS_CAN_UTILS_TXMAILBOX_0]; 
      if ( pUsedMailBox )   *pUsedMailBox = CUS_CAN_UTILS_TXMAILBOX_0;
    }
    else if ( canDevice->TSR & CAN_TSR_TME1_Msk )
    {
      /* 邮箱1空闲, 使用邮箱1 */
      txmbox = &canDevice->sTxMailBox[CUS_CAN_UTILS_TXMAILBOX_1];
      if ( pUsedMailBox )   *pUsedMailBox = CUS_CAN_UTILS_TXMAILBOX_1;
    }
    else if ( canDevice->TSR & CAN_TSR_TME2_Msk )
    {
      /* 邮箱2空闲, 使用邮箱2 */
      txmbox = &canDevice->sTxMailBox[CUS_CAN_UTILS_TXMAILBOX_2];
      if ( pUsedMailBox )   *pUsedMailBox = CUS_CAN_UTILS_TXMAILBOX_2;
    }
    else 
    {
      /* 无空闲邮箱. 该次发送请求失败，返回. */
      return -2;
    }
  }

  /* 填充邮箱 */
  txmbox->TIR = 0;
  txmbox->TIR |= txFrame->ID_Type & CAN_TI0R_IDE_Msk;
  txmbox->TIR |= txFrame->RTR & CAN_TI0R_RTR_Msk;

  if ( txFrame->ID_Type == CUS_CAN_UTILS_IdTYPE_STD )
  {
    /* 填充11位标准ID */
    txmbox->TIR |= (txFrame->ID & 0x7FF) << 21;
  }
  else if ( txFrame->ID_Type == CUS_CAN_UTILS_IdTYPE_EXTI )
  {
    /* 填充29位拓展ID */
    txmbox->TIR |= (txFrame->ID & 0x1FFFFFFF) << 3;
  }

  txmbox->TDTR = 0;
  txmbox->TDTR |= txFrame->DLC & CAN_TDT0R_DLC_Msk;

  /* 填充数据段 */
  uint32_t tdlr_val, tdhr_val;
  memcpy(&tdlr_val, buffer, 4);
  memcpy(&tdhr_val, buffer + 4, 4);
  txmbox->TDLR = tdlr_val;
  txmbox->TDHR = tdhr_val;

  /* 置传输请求 */
  txmbox->TIR |= CAN_TI0R_TXRQ_Msk;

  return 0;
}


int8_t Cus_CAN_Utils_SendCANFrame_Synch( CAN_TypeDef *canDevice, uint8_t *buffer, uint32_t buffer_size, Cus_Can_Frame_t *txFrame, uint8_t TxMailBoxIndex, uint8_t *pUsedMailBox )
{  
  uint8_t usedMb;

  /* 记录当前时间戳 */
  uint32_t startTick_Req = Cus_CAN_Utils_GetTick();

  while(1)
  {
    int8_t hReturn = Cus_CAN_Utils_SendCANFrame_Async(canDevice, buffer, buffer_size, txFrame, TxMailBoxIndex, &usedMb);
    if ( hReturn < 0 )  
    {
      if ( (Cus_CAN_Utils_GetTick() - startTick_Req) > CUS_CAN_UTILS_TIMEOUT_REQ_MS )   
      {
        /* 直到超时都没有请求成功 */
        return -1;
      }

      /* 留出足够时间等待硬件 再重试 */
      Cus_CAN_Utils_Delay(5);
      continue;
    }

    /* 发送请求提交完成 阻塞等待硬件成功将报文发送至总线 */
    uint32_t startTick_Tx = Cus_CAN_Utils_GetTick();
    uint32_t offset = CAN_TSR_TXOK0_Msk << ((usedMb) * 8);
    while( !(canDevice->TSR & offset) )
    {
      /* 发送动作未完成 */
      if ( Cus_CAN_Utils_GetTick() - startTick_Tx > CUS_CAN_UTILS_TIMEOUT_TX_MS )   return -2;

      Cus_CAN_Utils_Delay(5);
    }

    break;
  }

  if ( pUsedMailBox )   *pUsedMailBox = usedMb;

  return 0;
}


void Cus_CAN_Utils_IncreaseTick( void )
{
  uint32_t tmp = g_cus_canUtils_heartTick;
  g_cus_canUtils_heartTick = tmp + 1;
}


static uint32_t Cus_CAN_Utils_GetTick( void )
{
  return g_cus_canUtils_heartTick;
}


static void Cus_CAN_Utils_Delay( uint32_t delay_ms )
{
  if ( delay_ms == 0 )    return;

  uint32_t startTick = Cus_CAN_Utils_GetTick();

  while( (Cus_CAN_Utils_GetTick() - startTick) < delay_ms ) 
  {
    /* 空循环 */
  }
}




