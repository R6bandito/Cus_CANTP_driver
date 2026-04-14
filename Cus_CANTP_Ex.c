#include "Cus_CANTP_Ex.h"


#define TEST_DATA_LEN   100
static uint8_t test_data[TEST_DATA_LEN];
static uint8_t rx_buffer[512];
static volatile uint8_t rx_done = 0;

// 数据指示回调
void MyDataIndication(Cus_CANTp_Conn_t *pConn, const U8 *data, U32 len) 
{
  printf("Received %lu bytes: ", len);
  for(U32 i=0; i<len; i++) printf("%02X ", data[i]);
  printf("\r\n");
}

// void MyDataIndication(Cus_CANTp_Conn_t *pConn, const uint8_t *data, uint32_t len) 
// {
//   if (len == TEST_DATA_LEN && memcmp(data, test_data, len) == 0) 
//   {
//       rx_done = 1;
//   } else {
//       // 数据错误
//   }
// }

// 错误回调
void MyErrCallback(Cus_CANTp_Conn_t *pConn, U8 err) 
{
  printf("Error %d on conn %d\r\n", err, pConn->ConnIndex);
}


void CANTP_Single_Test( void )
{
  // 初始化 CAN（环回模式）
  Cus_CAN_GPIO_t g = {GPIOA, GPIO_PIN_11, GPIO_PIN_12, 0};
  Cus_CAN_QuickSetup(CAN1, &g);
  Cus_CAN_Device_t *pDev = Cus_CAN_getControlBlock(CAN1);

  // 注册接收环形缓冲区
  static Cus_CAN_RxMsg_t rx_ring[60];
  pDev->registerRxBuffer(pDev, rx_ring, sizeof(rx_ring));
  pDev->EnableInterrupt(pDev, CAN_IT_RX_FIFO0_MSG_PENDING);
  pDev->EnableInterrupt(pDev, CAN_IT_TX_MAILBOX_EMPTY);

  // CAN TP 初始化
  Cus_Cantp_ModuleInit();
  Cus_Cantp_Register_Func_Callback(Cus_CanTP_canSendFunc_Asynchronous, MyDataIndication, MyErrCallback);

  // 配置通道 0（普通寻址，经典CAN）
  Cus_Cantp_Config_ChannelMain_Info(NORMAL_ADDRESS_MODE, 8, 0x7DF, 0);
  Cus_Cantp_Config_ChannelNAI_Info(0x12, 0x34, 0, 0, 0);  // TA=0x12, SA=0x34

  // 为连接索引 0 注册接收缓冲区（作为接收方）
  static uint8_t rx_buffer[512];
  Cus_Cantp_Register_RecvBuffer(rx_buffer, sizeof(rx_buffer), CONN_INDEX_1);

  // 发送测试单帧
  const uint8_t test_data[] = {0x11, 0x22, 0x33, 0x44, 0x55};
  uint8_t ret = Cus_Cantp_Transmit(0, CONN_INDEX_1, test_data, sizeof(test_data), (void*)CAN1);
  if (!ret) printf("Transmit failed\r\n");

  while(1)
  {
    HAL_Delay(1);
  }
}


void test_multiframe(void) 
{
  // 初始化 CAN（环回模式）
  Cus_CAN_GPIO_t g = {GPIOA, GPIO_PIN_11, GPIO_PIN_12, 0};
  Cus_CAN_QuickSetup(CAN1, &g);
  Cus_CAN_Device_t *pDev = Cus_CAN_getControlBlock(CAN1);

  // 注册接收环形缓冲区
  static Cus_CAN_RxMsg_t rx_ring[60];
  pDev->registerRxBuffer(pDev, rx_ring, sizeof(rx_ring));
  pDev->EnableInterrupt(pDev, CAN_IT_RX_FIFO0_MSG_PENDING);
  pDev->EnableInterrupt(pDev, CAN_IT_TX_MAILBOX_EMPTY);

  // CAN TP 初始化
  Cus_Cantp_ModuleInit();
  Cus_Cantp_Register_Func_Callback(Cus_CanTP_canSendFunc_Asynchronous, MyDataIndication, MyErrCallback);

  // 配置通道 0（普通寻址，经典CAN）
  Cus_Cantp_Config_ChannelMain_Info(NORMAL_ADDRESS_MODE, 8, 0x7DF, 0);
  Cus_Cantp_Config_ChannelNAI_Info(0x12, 0x34, 0, 0, 0);  // TA=0x12, SA=0x34

  // 填充测试数据
  for (int i = 0; i < TEST_DATA_LEN; i++) test_data[i] = i & 0xFF;

  // 获取发送连接（索引 0）
  Cus_CANTp_Conn_t *pTxConn = Cus_Cantp_GetIdleConn();
  if (!pTxConn) 
  {
      printf("Failed to get TX connection!\r\n");
      return;
  }
  pTxConn->BindCANDevice = (void*)CAN1;
  pTxConn->SendFunc = Cus_CanTP_canSendFunc_Asynchronous;
  pTxConn->DataIndFunc = MyDataIndication;
  pTxConn->ChannelConfigTabID = 0;
  pTxConn->BS = 5;      // 设置流控块大小
  pTxConn->STmin = 100;   // 最小间隔时间
  Cus_Cantp_Register_RecvBuffer(rx_buffer, sizeof(rx_buffer), pTxConn->ConnIndex);

  // // 获取接收连接（索引 1）
  // Cus_CANTp_Conn_t *pRxConn = Cus_Cantp_GetIdleConn();
  // if (!pRxConn) 
  // {
  //     printf("Failed to get RX connection!\r\n");
  //     Cus_Cantp_ReleaseConn(pTxConn);
  //     return;
  // }
  // pRxConn->BindCANDevice = (void*)CAN1;
  // pRxConn->RecvFunc = Cus_CanTP_canRecvFunc_Asynchronous;
  // pRxConn->DataIndFunc = MyDataIndication;
  // pRxConn->ErrCallBack = MyErrCallback;
  // pRxConn->ChannelConfigTabID = 0;



  rx_done = 0;

  uint8_t ret = Cus_Cantp_SendFirstFrame(pTxConn, test_data, TEST_DATA_LEN);
  if (ret != 1) 
  {
      printf("SendFirstFrame failed!\r\n");
      Cus_Cantp_ReleaseConn(pTxConn);
      return;
  }

  // 等待接收完成（超时保护）
  uint32_t timeout = 5000; // 5秒
  while (!rx_done && timeout--) {
      HAL_Delay(1);
  }
  if (rx_done) {
      printf("Multi-frame test PASSED!\r\n");
  } else {
      printf("Multi-frame test FAILED (timeout or data mismatch)\r\n");
  }
}



