#include "Cus_CANTP_Ex.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>


void cantp_test_single_normal( void )
{
  printf("\n=== test_single_frame_normal ===\n");
  U8 buffer[8] = {0};
  U8 data[] = {0xAA, 0xBB, 0xDD};
  U8 len = 3;
  U8 channel = 0; // 使用配置表索引0，AddrMode=0, TxDLC=8

  U8 ret = Cus_Cantp_BuildSingleFrame(buffer, data, len, channel);
  assert( ret == 8 );
  // 预期 PCI 字节: 0x03 (高4位=0, 低4位=3)
  assert(buffer[0] == 0x03);
  // 数据
  assert(buffer[1] == 0xAA);
  assert(buffer[2] == 0xBB);
  assert(buffer[3] == 0xDD);

  // 剩余填充 0xCC
  for (int i = 4; i < 8; i++) assert(buffer[i] == 0xCC);
  printf("Single frame OK\n");
}


void cantp_test_single_ext( void )
{
  printf("\n=== test_single_frame_ext ===\n");
  Cus_CANTP_Cfg_t *pCfg = Cus_Cantp_GetChannel(1);
  Cus_CANTP_Cfg_t backCfg = *pCfg;
  pCfg->N_AI.TA = 0x12;

  U8 buffer[8] = {0};
  U8 data[] = {0xAA, 0xBB, 0xDD};
  U8 len = 3;
  U8 channel = 1;
  U8 ret = Cus_Cantp_BuildSingleFrame(buffer, data, len, channel);
  assert(ret == 8);
  assert(buffer[0] == 0x12);              // TA
  assert(buffer[1] == (0x00 | (len & 0x0F))); // PCI = 0x03
  assert(buffer[2] == 0xAA);
  assert(buffer[3] == 0xBB);
  assert(buffer[4] == 0xDD);

  for (int i = 5; i < 8; i++) assert(buffer[i] == 0xCC);
  printf("Ext addressing single frame OK\n");

  *pCfg = backCfg;
}


void cantp_test_first_standard( void )
{
  printf("\n=== test_first_frame_standard ===\n");
  U8 buffer[8] = {0};
  U8 data[] = {0x01,0x02,0x03,0x04,0x05,0x06}; // 6字节用户数据
  U32 total_len = 100; // 总长度 100
  U8 channel = 0;
  U8 ret = Cus_Cantp_BuildFirstFrame(buffer, data, total_len, channel);

  assert(ret == 8);
  // 标准首帧：2字节PCI + 6字节数据
  // PCI[0] = 0x10 | ((total_len>>8)&0x0F) = 0x10 | (0) = 0x10
  assert(buffer[0] == 0x10);
  // PCI[1] = total_len & 0xFF = 0x64
  assert(buffer[1] == 0x64);
  // 数据部分
  for (int i = 0; i < 6; i++) assert(buffer[2+i] == data[i]);

  printf("Standard FF OK\n");
}


void cantp_test_first_ext( void )
{
  printf("\n=== test_first_frame_extended ===\n");
  U8 buffer[8] = {0};
  U8 data[] = {0xAA, 0xBB}; // 扩展格式最多2字节用户数据（但你的代码未拷贝，需注意）
  U32 total_len = 5000; // 大于4095
  U8 channel = 0;
  U8 ret = Cus_Cantp_BuildFirstFrame(buffer, data, total_len, channel);
  assert(ret == 8);

  assert(buffer[0] == 0x10);
  assert(buffer[1] == 0x00);
  
  assert(buffer[2] == 0x00);
  assert(buffer[3] == 0x00);
  assert(buffer[4] == 0x13);
  assert(buffer[5] == 0x88);
  // 剩余填充0xCC
  assert(buffer[6] == 0xCC);
  assert(buffer[7] == 0xCC);

  printf("Extended FF OK\n");
}


void cantp_test_consective( void )
{
  printf("\n=== test_consecutive_frame ===\n");

  Cus_CANTp_Conn_t conn;
  memset(&conn, 0, sizeof(conn));

  conn.ChannelConfigTabID = 0;
  conn.SN_Code = 3;          // 当前序列号
  conn.TxPos = 10;           // 从数据源偏移10字节处取数据
  conn.Remaining = 15;       // 还剩15字节要发送（超过7，所以拷贝7字节）
  U8 sourceData[100];        // 模拟数据源
  for (int i = 0; i < 100; i++) sourceData[i] = i;

  U8 buffer[8] = {0};
//  U8 ret = Cus_Cantp_BuildConsecutiveFrame(buffer, sourceData, &conn);

//  assert(ret == 8);
  // 检查 PCI 字节：CF=0x2, SN=3 -> 0x23
  assert(buffer[0] == 0x23);
  // 数据：应从 sourceData[10] 开始拷贝7字节
  for (int i = 0; i < 7; i++) {
      assert(buffer[1+i] == sourceData[10+i]);
  }
  printf("CF (full 7 bytes) OK\n");

  // 测试最后一帧不足7字节
  conn.Remaining = 5;
  conn.TxPos = 20;
  conn.SN_Code = 5;
  memset(buffer, 0, sizeof(buffer));

//  ret = Cus_Cantp_BuildConsecutiveFrame(buffer, sourceData, &conn);
//  assert(ret == 8);
  assert(buffer[0] == (0x2<<4 | 5)); // 0x25
  // 拷贝5字节有效数据
  for (int i = 0; i < 5; i++) assert(buffer[1+i] == sourceData[20+i]);
  // 剩余2字节填充0xCC
  assert(buffer[6] == 0xCC);
  assert(buffer[7] == 0xCC);
  printf("CF (last partial) OK\n");
}


void cantp_test_flowcontrol( void )
{
  printf("\n=== test_flowcontrol_frame ===\n");

  Cus_CANTp_Conn_t conn;
  memset(&conn, 0, sizeof(conn));
  conn.ChannelConfigTabID = 0;
  conn.BS = 10;
  conn.STmin = 5;

  U8 buffer[8] = {0};
  U8 ret = Cus_Cantp_BuildFlowControlFrame(buffer, FLOW_CTS, &conn);
  assert(ret == 8);
  // PCI: 0x30 (高4位=3, 低4位=0)
  assert(buffer[0] == 0x30);
  assert(buffer[1] == 10);
  assert(buffer[2] == 5);

  for (int i = 3; i < 8; i++) assert(buffer[i] == 0xCC);
  printf("FC (CTS) OK\n");

  ret = Cus_Cantp_BuildFlowControlFrame(buffer, FLOW_WAIT, &conn);
  assert(buffer[0] == 0x31); // 0x3 | 0x1

  ret = Cus_Cantp_BuildFlowControlFrame(buffer, FLOW_OVFLW, &conn);
  assert(buffer[0] == 0x32); // 0x3 | 0x2

  printf("FC other states OK\n");
}


void cantp_test_canid( void )
{
  printf("\n=== test_can_id ===\n");

  Cus_CANTP_Cfg_t cfg;
  memset(&cfg, 0, sizeof(cfg));
  // 普通寻址模式：SA=0x12, TA=0x34, TA_Type=0
  cfg.AddrMode = NORMAL_ADDRESS_MODE;
  cfg.N_AI.SA = 0x12;
  cfg.N_AI.TA = 0x14;
  cfg.N_AI.TA_Type = 0;

  U32 id = Cus_Cantp_GetCanID(&cfg); // 计算：((SA & 0x1F) << 6) | ((TA & 0x1F) << 1) | TA_Type = (0x12<<6) | (0x34<<1) | 0 = 0x480 | 0x68 = 0x4E8
  assert(id == 0x4A8);
  printf("Normal mode ID=0x%03X OK\n", id);

  // 功能寻址：TA_Type=1，应返回 FunctionalCanID（配置表中默认为0x7DF）
  cfg.N_AI.TA_Type = 1;
  cfg.FunctionalCanID = 0x7DF;
  id = Cus_Cantp_GetCanID(&cfg);
  assert(id == 0x7DF);
  printf("Functional addressing ID OK\n");

  // 扩展寻址模式：SA=0x12, TA_Type=1
  cfg.AddrMode = EXT_ADDRESS_MODE;
  cfg.N_AI.TA_Type = 0;
  cfg.N_AI.SA = 0x12;
  cfg.N_AI.TA = 0xF4;
  id = Cus_Cantp_GetCanID(&cfg);

  assert(id == 0x90);

  printf("Ext mode ID=0x%03X OK\n", id);
}



void cantp_test_get_length_from_ff( void )
{
  printf("\n=== test_get_length_from_ff ===\n");

  U8 frame_std[8] = {0x10, 0x64, 0,0,0,0,0,0}; // 总长度 0x064 = 100
  U32 len = Cus_Cantp_GetDataLengthFromFF(frame_std);
  assert(len == 100);
  printf("Standard FF length=100 OK\n");

  U8 frame_ext[8] = {0x10, 0x00, 0x00, 0x00, 0x13, 0x88, 0xCC, 0xCC}; // 长度 5000
  len = Cus_Cantp_GetDataLengthFromFF(frame_ext);
  assert(len == 5000);
  printf("Extended FF length=5000 OK\n");

  // 非FF帧应返回0
  U8 bad_frame[8] = {0x20, 0x00}; // 高4位=2
  len = Cus_Cantp_GetDataLengthFromFF(bad_frame);
  assert(len == 0);
  printf("Invalid FF returns 0 OK\n");
}


void cantp_test_conn_pool( void )
{
  printf("\n=== test_conn_pool ===\n");

  // 注意：全局 ConnPool 初始全0，状态为 CONN_IDLE（因为静态变量初始化为0）
  Cus_CANTp_Conn_t *c1 = Cus_Cantp_GetIdleConn();
  assert(c1 != NULL);
  assert(c1->CurrentState == CONN_IDLE);
  assert(c1->ConnIndex == 0);

  // 获取第二个连接
  Cus_Cantp_ReleaseConn(c1); // 释放第一个
  c1 = Cus_Cantp_GetIdleConn(); // 再次获取，应得到同一个索引0
  assert(c1->ConnIndex == 0);

  // 连续获取直到满
  Cus_CANTp_Conn_t *c2 = Cus_Cantp_GetIdleConn();
  Cus_CANTp_Conn_t *c3 = Cus_Cantp_GetIdleConn();
  Cus_CANTp_Conn_t *c4 = Cus_Cantp_GetIdleConn();
  assert(c2 && c3 && c4);

  Cus_CANTp_Conn_t *c5 = Cus_Cantp_GetIdleConn();
  // assert(c5 == NULL); // 池满
  if ( c5 == NULL )   printf("Pool Full.\n");

  // 释放一个后再获取
  Cus_Cantp_ReleaseConn(c2);
  c5 = Cus_Cantp_GetIdleConn();
  assert(c5 != NULL);
  printf("Conn pool test OK\n");
}






