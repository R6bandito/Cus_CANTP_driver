#ifndef __CUS_CANTP_H__
#define __CUS_CANTP_H__

#ifndef __Cus_CANTP_XzzwY7a9BBCTQ7__
  #define __Cus_CANTP_XzzwY7a9BBCTQ7__
#endif // __Cus_CANTP_XzzwY7a9BBCTQ7__

#include <stdlib.h>
#include <string.h>
#include "Cus_CANTP_cfg.h"

/*  ---------------------------------------------------  */
typedef struct Cus_CANTP_Conn Cus_CANTp_Conn_t;
  typedef unsigned char U8;
  typedef unsigned int U32;
  typedef unsigned short U16; 

  typedef signed char S8;
  typedef signed short S16;
  typedef signed short S32;

  typedef U8 (*Cus_CanTP_CanSendFunc)( Cus_CANTp_Conn_t *pConn, U32 canId, const U8* data, U16 dlc );
  typedef U8 (*Cus_CanTP_CanRecvFunc)( Cus_CANTp_Conn_t *pConn, U32 *pcanId, U8 *pData, U8 *pDlc );
  typedef void (*Cus_CanTP_DataIndication)( Cus_CANTp_Conn_t *pConn, const U8 *data, U32 len );

  #ifndef NULL
    #define NULL ((void *)0)
  #endif // NULL
/*  ---------------------------------------------------  */


/*  ---------------------------------------------------  */
  #define MAX_SUPPORT_CONN             (4U)

  #define NORMAL_ADDRESS_MODE          (0U)
  #define EXT_ADDRESS_MODE             (1U)
  #define MIXED_ADDRESS_MODE           (2U)

  #define TIMER_NBS                    (200UL)
  #define TIMER_NAS                    (100UL)
  #define TIMER_NAR                    (200UL)
  #define TIMER_NCR                    (100UL)

/*  ---------------------------------------------------  */


typedef enum 
{
  // 空闲状态.
  CONN_IDLE = 0,

  CONN_TX_SF,     // 发送单帧. 等待确认.
  CONN_TX_FF,     // 发送首帧. 等待确认.
  CONN_TX_CF,     // 发送连续帧. 等待确认.
  CONN_TX_WAIT_FC,  // 等待流控帧.

  CONN_RX_FF,       // 接收首帧. 预备发送流控.
  CONN_RX_CF,       // 正在接收连续帧.
  CONN_RX_WAIT_CF,  // 已发送流控，等待连续帧.
  CONN_RX_SF

} Cus_CANTP_State_t;


typedef enum 
{
  FLOW_CTS,
  FLOW_WAIT,
  FLOW_OVFLW

} Cus_CANTP_FlowState_t;


struct Cus_CANTP_Conn
{
  U32 CAN_ID;               // 会话ID.
  Cus_CANTP_State_t CurrentState;     // 当前状态.

  U32 TxBytes;              // 已经发送的数据(字节).
  U32 RxBytes;              // 已接收到的数据(字节).
  U8 SN_Code;               // SN序列码.
  U32 TotalSize;            // 该此会话 数据总长度.
  U32 Remaining;            // 剩余要发送的字节.

  U8 STmin;                 // 流控参数. STmin.
  U16 Timer_StminDelayOnly; // STmin 延迟计数器.
  U8 BS;                    // 流控参数. BS.
  U16 RemainingBS;           // 当前块内还可以连续发送的 CF 数量.

  U32 Timer_N_As;           // 发送方帧发送确认超时计时器.
  U32 Timer_N_Bs;           // 发送方等待流控帧超时计时器.
  U32 Timer_N_Ar;           // 接收方帧发送确认超时计时器.
  U32 Timer_N_Cr;           // 接收方等待连续帧超时计时器.

  U8 *pRecvBuffer;          // 接收缓冲区.
  U32 RecvBuffer_Size;      // 缓冲区大小.
  U32 RxPos;                // 已接收数据长度(RecvBuf中下一个写入位置).
  U32 TxPos;                // 发送偏移量.

  S8 ConnIndex;             // 所属资源池ID.
  U8 ChannelConfigTabID;    // 通道配置表ID.

  U8 TxMailBoxIndex;        // 通信所用的发送邮箱号.(0/1/2) FF表示初始化误状态. 
  void *BindCANDevice;      // 绑定的底层CAN设备. (请通过类型转换将其转换为 CAN_TypeDef * 形式).

  Cus_CanTP_CanSendFunc SendFunc;         // 底层 CAN 帧发送.(自实现).异步
  Cus_CanTP_CanRecvFunc RecvFunc;         // 底层 CAN 帧接收.
  Cus_CanTP_DataIndication DataIndFunc;   // 上层数据通知回调.

};


/*  ---------------------------------------------------  */
void Cus_Cantp_HeartTick( void );

/*  ---------------------------------------------------  */

/*  ---------------------------------------------------  */
  Cus_CANTp_Conn_t *Cus_Cantp_GetIdleConn( void );
  void Cus_Cantp_ReleaseConn( Cus_CANTp_Conn_t *pConn );
  void Cus_Cantp_ModuleInit( void );
  U8 Cus_Cantp_RecieveFrame( const U8 *data, U8 dlc, U32 canid );   // 上层喂帧总API.
  U32 Cus_Cantp_GetCanID( const Cus_CANTP_Cfg_t *pCfg );   // 得到实际发送的 CAN ID.
  U32 Cus_Cantp_GetDataLengthFromFF( const U8 *frame );

  U8 Cus_Cantp_SendFirstFrame( Cus_CANTp_Conn_t *pConn, const U8 *data, U32 total_len );    // 发送FF帧.
  U8 Cus_Cantp_SendSingleFrame( Cus_CANTp_Conn_t *pConn, const U8 *data, U8 len );          // 发送SF帧.
  U8 Cus_Cantp_SendFlowControlFrame( Cus_CANTp_Conn_t *pConn, Cus_CANTP_FlowState_t fs );   // 发送FC帧.

  U8 Cus_Cantp_BuildSingleFrame( U8 *Buffer, const U8 *data, U8 len, U8 ChannelTabID );   // SF帧组装.
  U8 Cus_Cantp_BuildFirstFrame( U8 *Buffer, const U8 *data, U32 total_len, U8 ChannelTabID );   // FF帧组装.
  U8 Cus_Cantp_BuildConsecutiveFrame( U8 *Buffer, const U8 *data, Cus_CANTp_Conn_t *pConn );    // CF帧组装.
  U8 Cus_Cantp_BuildFlowControlFrame( U8 *Buffer, Cus_CANTP_FlowState_t flow_State, Cus_CANTp_Conn_t *pConn );  // FC帧组装.
/*  ---------------------------------------------------  */



#endif // __CUS_CANTP_H__
