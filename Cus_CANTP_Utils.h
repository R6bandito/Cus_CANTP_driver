#ifndef __CUS_CANTP_UTILS_H__
#define __CUS_CANTP_UTILS_H__


#include <stdint.h>
#include <string.h>
#include "stm32f1xx.h"


/* *************************************** */
  #define CUS_CAN_UTILS_IdTYPE_STD          (0x00)               // 标准11位ID.
  #define CUS_CAN_UTILS_IdTYPE_EXTI         (0x01)               // 拓展29位ID.

  #define CUS_CAN_UTILS_FIFO0               (0x00)
  #define CUS_CAN_UTILS_FIFO1               (0x01)

  #define CUS_CAN_UTILS_TXMAILBOX_NONE      (0xFF) 
  #define CUS_CAN_UTILS_TXMAILBOX_0         (0x00)
  #define CUS_CAN_UTILS_TXMAILBOX_1         (0x01)
  #define CUS_CAN_UTILS_TXMAILBOX_2         (0x02)

  #define CUS_CAN_UTILS_TIMEOUT_REQ_MS      (50)                 // 同步发送请求超时时间.
  #define CUS_CAN_UTILS_TIMEOUT_TX_MS       (50)                 // 同步发送动作超时时间.
/* *************************************** */


typedef struct Cus_Can_Frame
{
  uint8_t ID_Type;
  uint8_t RTR;
  uint8_t DLC;
  uint32_t ID;

} Cus_Can_Frame_t;



/* *************************************************************************** */
  int8_t Cus_CAN_Utils_RecevieCANFrame( CAN_TypeDef *canDevice, uint8_t *buffer, uint32_t buffer_size, Cus_Can_Frame_t *rxFrame, uint8_t FIFOx );
  int8_t Cus_CAN_Utils_SendCANFrame_Async( CAN_TypeDef *canDevice, uint8_t *buffer, uint32_t buffer_size, Cus_Can_Frame_t *txFrame, uint8_t TxMailBoxIndex, uint8_t *pUsedMailBox );
  int8_t Cus_CAN_Utils_SendCANFrame_Synch( CAN_TypeDef *canDevice, uint8_t *buffer, uint32_t buffer_size, Cus_Can_Frame_t *txFrame, uint8_t TxMailBoxIndex, uint8_t *pUsedMailBox );

  void Cus_CAN_Utils_IncreaseTick( void );

/* *************************************************************************** */


#endif // __CUS_CANTP_UTILS_H__
