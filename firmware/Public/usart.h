#ifndef _usart_H
#define _usart_H

#include "system.h" 
#include "stdio.h" 

#define USART1_REC_LEN		200  	//定义最大接收字节数 200

extern u8  USART1_RX_BUF[USART1_REC_LEN]; //接收缓冲,最大USART_REC_LEN个字节.末字节为换行符 
extern u16 USART1_RX_STA;         		//接收状态标记
extern volatile u8 WiFi_AT_Flag;
extern volatile u8 WiFi_RGB_Command;
extern volatile u8 WiFi_TargetFound;
extern volatile u8 WiFi_STA_IP_Valid;
extern char WiFi_STA_IP[16];

#define WIFI_TCP_RX_BUFFER_SIZE  128
#define WIFI_TCP_TX_BUFFER_SIZE  160
#define WIFI_TCP_STREAM_BUFFER_SIZE  1024

extern volatile u8 WiFi_TCP_LinkValid;
extern volatile u8 WiFi_TCP_LinkId;
extern volatile u8 WiFi_TCP_ClientEvent;
extern volatile u8 WiFi_TCP_RxReady;
extern volatile u8 WiFi_TCP_RxLinkId;
extern volatile u16 WiFi_TCP_RxLength;
extern volatile u8 WiFi_TCP_TxResult;
extern volatile u16 WiFi_TCP_RxDropped;
extern u8 WiFi_TCP_RxBuffer[WIFI_TCP_RX_BUFFER_SIZE];

#define WIFI_CMD_NONE  0
#define WIFI_CMD_ON    1
#define WIFI_CMD_OFF   2

#define WIFI_AT_IDLE     0
#define WIFI_AT_WAITING  1
#define WIFI_AT_OK       5
#define WIFI_AT_ERROR    6
#define WIFI_AT_TCP_SEND 7

#define WIFI_TCP_CLIENT_NONE       0
#define WIFI_TCP_CLIENT_CONNECTED  1
#define WIFI_TCP_CLIENT_CLOSED     2

#define WIFI_TCP_TX_RESULT_NONE   0
#define WIFI_TCP_TX_RESULT_OK     1
#define WIFI_TCP_TX_RESULT_ERROR  2


void USART1_Init(u32 bound);
void USART3_Init(u32 bound);
void WiFi_SendByte(u8 byte);
void WiFi_SendString(char *string);
u8 WiFi_TCP_QueueSend(u8 link_id,const u8 *data,u16 length);
u8 WiFi_TCP_TxBusy(void);
void WiFi_TCP_TxTask(void);
u16 WiFi_TCP_Read(u8 *destination,u16 max_length);
void WiFi_TCP_ClearRx(void);


#endif


