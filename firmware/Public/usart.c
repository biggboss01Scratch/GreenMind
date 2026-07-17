#include "usart.h"		 
#include "app_config.h"
#include <string.h>

int fputc(int ch,FILE *p)  //函数默认的，在使用printf函数时自动调用
{
	USART_SendData(USART1,(u8)ch);	
	while(USART_GetFlagStatus(USART1,USART_FLAG_TXE)==RESET);
	return ch;
}

//串口1中断服务程序
//注意,读取USARTx->SR能避免莫名其妙的错误   	
u8 USART1_RX_BUF[USART1_REC_LEN];     //接收缓冲,最大USART_REC_LEN个字节.
//接收状态
//bit15，	接收完成标志
//bit14，	接收到0x0d
//bit13~0，	接收到的有效字节数目
u16 USART1_RX_STA=0;       //接收状态标记


/*******************************************************************************
* 函 数 名         : USART1_Init
* 函数功能		   : USART1初始化函数
* 输    入         : bound:波特率
* 输    出         : 无
*******************************************************************************/ 
void USART1_Init(u32 bound)
{
   //GPIO端口设置
	GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;
	
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA,ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1,ENABLE);
 
	
	/*  配置GPIO的模式和IO口 */
	GPIO_InitStructure.GPIO_Pin=GPIO_Pin_9;//TX			   //串口输出PA9
	GPIO_InitStructure.GPIO_Speed=GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Mode=GPIO_Mode_AF_PP;	    //复用推挽输出
	GPIO_Init(GPIOA,&GPIO_InitStructure);  /* 初始化串口输入IO */
	GPIO_InitStructure.GPIO_Pin=GPIO_Pin_10;//RX			 //串口输入PA10
	GPIO_InitStructure.GPIO_Mode=GPIO_Mode_IN_FLOATING;		  //模拟输入
	GPIO_Init(GPIOA,&GPIO_InitStructure); /* 初始化GPIO */
	

	//USART1 初始化设置
	USART_InitStructure.USART_BaudRate = bound;//波特率设置
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;//字长为8位数据格式
	USART_InitStructure.USART_StopBits = USART_StopBits_1;//一个停止位
	USART_InitStructure.USART_Parity = USART_Parity_No;//无奇偶校验位
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;//无硬件数据流控制
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;	//收发模式
	USART_Init(USART1, &USART_InitStructure); //初始化串口1
	
	USART_Cmd(USART1, ENABLE);  //使能串口1 
	
	USART_ClearFlag(USART1, USART_FLAG_TC);
		
	USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);//开启相关中断

	//Usart1 NVIC 配置
	NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;//串口1中断通道
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority=3;//抢占优先级3
	NVIC_InitStructure.NVIC_IRQChannelSubPriority =3;		//子优先级3
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;			//IRQ通道使能
	NVIC_Init(&NVIC_InitStructure);	//根据指定的参数初始化VIC寄存器、	
}

/*******************************************************************************
* 函 数 名         : USART1_IRQHandler
* 函数功能		   : USART1中断函数
* 输    入         : 无
* 输    出         : 无
*******************************************************************************/ 
void USART1_IRQHandler(void)                	//串口1中断服务程序
{
	u8 r;
	if(USART_GetITStatus(USART1, USART_IT_RXNE) != RESET)  //接收中断
	{
		r =USART_ReceiveData(USART1);//(USART1->DR);	//读取接收到的数据
		if((USART1_RX_STA&0x8000)==0)//接收未完成
		{
			if(USART1_RX_STA&0x4000)//接收到了0x0d
			{
				if(r!=0x0a)USART1_RX_STA=0;//接收错误,重新开始
				else USART1_RX_STA|=0x8000;	//接收完成了 
			}
			else //还没收到0X0D
			{	
				if(r==0x0d)USART1_RX_STA|=0x4000;
				else
				{
					USART1_RX_BUF[USART1_RX_STA&0X3FFF]=r;
					USART1_RX_STA++;
					if(USART1_RX_STA>(USART1_REC_LEN-1))USART1_RX_STA=0;//接收数据错误,重新开始接收	  
				}		 
			}
		}   		 
	} 
}

#define WIFI_LINE_BUFFER_SIZE  64
#define WIFI_TX_PROMPT_TIMEOUT_TICKS  200
#define WIFI_TX_RESULT_TIMEOUT_TICKS  500

#define WIFI_TX_EVENT_NONE    0
#define WIFI_TX_EVENT_PROMPT  1
#define WIFI_TX_EVENT_SENT    2
#define WIFI_TX_EVENT_ERROR   3

#define WIFI_TX_STATE_IDLE         0
#define WIFI_TX_STATE_WAIT_PROMPT  1
#define WIFI_TX_STATE_WAIT_RESULT  2

volatile u8 WiFi_AT_Flag=0;
volatile u8 WiFi_RGB_Command=WIFI_CMD_NONE;
volatile u8 WiFi_TargetFound=0;
volatile u8 WiFi_STA_IP_Valid=0;
char WiFi_STA_IP[16]={0};

volatile u8 WiFi_TCP_LinkValid=0;
volatile u8 WiFi_TCP_LinkId=0;
volatile u8 WiFi_TCP_ClientEvent=WIFI_TCP_CLIENT_NONE;
volatile u8 WiFi_TCP_RxReady=0;
volatile u8 WiFi_TCP_RxLinkId=0;
volatile u16 WiFi_TCP_RxLength=0;
volatile u8 WiFi_TCP_TxResult=WIFI_TCP_TX_RESULT_NONE;
volatile u16 WiFi_TCP_RxDropped=0;
u8 WiFi_TCP_RxBuffer[WIFI_TCP_RX_BUFFER_SIZE];

static u8 WiFi_TCP_StreamBuffer[WIFI_TCP_STREAM_BUFFER_SIZE];
static volatile u16 WiFi_TCP_StreamHead=0;
static volatile u16 WiFi_TCP_StreamTail=0;

static char WiFi_LineBuffer[WIFI_LINE_BUFFER_SIZE];
static u8 WiFi_LineLength=0;
static u8 WiFi_LineOverflow=0;

static u8 WiFi_TCP_TxBuffer[WIFI_TCP_TX_BUFFER_SIZE];
static u16 WiFi_TCP_TxLength=0;
static u8 WiFi_TCP_TxLinkId=0;
static u8 WiFi_TCP_TxPending=0;
static u8 WiFi_TCP_TxState=WIFI_TX_STATE_IDLE;
static u16 WiFi_TCP_TxTimer=0;
static volatile u8 WiFi_TCP_TxEvent=WIFI_TX_EVENT_NONE;

static void WiFi_TCP_PushRx(u8 value)
{
	u16 next=(u16)((WiFi_TCP_StreamHead+1)%WIFI_TCP_STREAM_BUFFER_SIZE);
	if(next==WiFi_TCP_StreamTail)
	{
		WiFi_TCP_RxDropped++;
		return;
	}
	WiFi_TCP_StreamBuffer[WiFi_TCP_StreamHead]=value;
	WiFi_TCP_StreamHead=next;
}

static void WiFi_ParseTarget(u8 value)
{
	static const u8 target[]=APP_WIFI_SSID;
	static u8 match=0;

	if(value==target[match])
	{
		match++;
		if(match==sizeof(target)-1)
		{
			WiFi_TargetFound=1;
			match=0;
		}
	}
	else match=(value==target[0])?1:0;
}

static void WiFi_ParseStaIP(u8 value)
{
	static const u8 prefix[]="+CIFSR:STAIP,\"";
	static u8 match=0;
	static u8 collecting=0;
	static u8 index=0;

	if(collecting)
	{
		if(value=='\"')
		{
			WiFi_STA_IP[index]='\0';
			WiFi_STA_IP_Valid=(index>0)?1:0;
			collecting=0;
			match=0;
		}
		else if(((value>='0')&&(value<='9'))||value=='.')
		{
			if(index<sizeof(WiFi_STA_IP)-1)WiFi_STA_IP[index++]=value;
		}
		else
		{
			collecting=0;
			match=0;
		}
	}
	else if(value==prefix[match])
	{
		match++;
		if(match==sizeof(prefix)-1)
		{
			match=0;
			collecting=1;
			index=0;
			WiFi_STA_IP_Valid=0;
		}
	}
	else match=(value=='+')?1:0;
}

static void WiFi_ResetLineParser(void)
{
	WiFi_LineLength=0;
	WiFi_LineOverflow=0;
}

static void WiFi_ParseLine(u8 value)
{
	u8 link_id;

	if(value=='>')
	{
		if(WiFi_AT_Flag==WIFI_AT_TCP_SEND)
			WiFi_TCP_TxEvent=WIFI_TX_EVENT_PROMPT;
		WiFi_ResetLineParser();
		return;
	}

	if(value=='\r')return;
	if(value!='\n')
	{
		if(WiFi_LineLength<WIFI_LINE_BUFFER_SIZE-1)
			WiFi_LineBuffer[WiFi_LineLength++]=(char)value;
		else WiFi_LineOverflow=1;
		return;
	}

	if(!WiFi_LineOverflow)
	{
		WiFi_LineBuffer[WiFi_LineLength]='\0';

		if(strcmp(WiFi_LineBuffer,"OK")==0)
		{
			if(WiFi_AT_Flag==WIFI_AT_WAITING)WiFi_AT_Flag=WIFI_AT_OK;
		}
		else if(strcmp(WiFi_LineBuffer,"SEND OK")==0)
		{
			if(WiFi_AT_Flag==WIFI_AT_TCP_SEND)
				WiFi_TCP_TxEvent=WIFI_TX_EVENT_SENT;
		}
		else if(strcmp(WiFi_LineBuffer,"ERROR")==0 ||
		        strcmp(WiFi_LineBuffer,"FAIL")==0 ||
		        strcmp(WiFi_LineBuffer,"SEND FAIL")==0 ||
		        strncmp(WiFi_LineBuffer,"busy",4)==0)
		{
			if(WiFi_AT_Flag==WIFI_AT_WAITING)WiFi_AT_Flag=WIFI_AT_ERROR;
			else if(WiFi_AT_Flag==WIFI_AT_TCP_SEND)
				WiFi_TCP_TxEvent=WIFI_TX_EVENT_ERROR;
		}
		else if(WiFi_LineLength>=3 && WiFi_LineBuffer[0]>='0' &&
		        WiFi_LineBuffer[0]<='4' && WiFi_LineBuffer[1]==',')
		{
			link_id=(u8)(WiFi_LineBuffer[0]-'0');
			if(strcmp(&WiFi_LineBuffer[2],"CONNECT")==0)
			{
				WiFi_TCP_LinkId=link_id;
				WiFi_TCP_LinkValid=1;
				WiFi_TCP_ClientEvent=WIFI_TCP_CLIENT_CONNECTED;
			}
			else if(strcmp(&WiFi_LineBuffer[2],"CLOSED")==0)
			{
				if(WiFi_TCP_LinkValid && WiFi_TCP_LinkId==link_id)
					WiFi_TCP_LinkValid=0;
				WiFi_TCP_ClientEvent=WIFI_TCP_CLIENT_CLOSED;
				if(WiFi_AT_Flag==WIFI_AT_TCP_SEND)
					WiFi_TCP_TxEvent=WIFI_TX_EVENT_ERROR;
			}
		}
	}
	WiFi_ResetLineParser();
}

/* Return 1 when the byte belongs to a +IPD header or payload. */
static u8 WiFi_ParseIPD(u8 value)
{
	static const u8 prefix[]="+IPD,";
	static u8 prefix_match=0;
	static u8 state=0;
	static u8 header_field=0;
	static u16 header_value=0;
	static u8 link_id=0;
	static u16 payload_length=0;
	static u16 payload_count=0;

	if(state==0)
	{
		if(value==prefix[prefix_match])
		{
			prefix_match++;
			if(prefix_match==sizeof(prefix)-1)
			{
				prefix_match=0;
				state=1;
				header_field=0;
				header_value=0;
				WiFi_ResetLineParser();
				return 1;
			}
		}
		else prefix_match=(value=='+')?1:0;
		return 0;
	}

	if(state==1)
	{
		if(value>='0' && value<='9')
			header_value=header_value*10+(value-'0');
		else if(value==',' && header_field==0)
		{
			link_id=(u8)header_value;
			header_value=0;
			header_field=1;
		}
		else if(value==':')
		{
			if(header_field==0)link_id=0;
			payload_length=header_value;
			payload_count=0;
			state=(payload_length>0)?2:0;
		}
		else
		{
			state=0;
			prefix_match=0;
		}
		return 1;
	}

	WiFi_TCP_PushRx(value);
	payload_count++;
	if(payload_count>=payload_length)
	{
		if(!WiFi_TCP_LinkValid || WiFi_TCP_LinkId!=link_id)
		{
			WiFi_TCP_LinkId=link_id;
			WiFi_TCP_LinkValid=1;
			if(WiFi_TCP_ClientEvent==WIFI_TCP_CLIENT_NONE)
				WiFi_TCP_ClientEvent=WIFI_TCP_CLIENT_CONNECTED;
		}
		state=0;
	}
	return 1;
}

void USART3_Init(u32 bound)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA|RCC_APB2Periph_GPIOB,ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3,ENABLE);

	/* PA4/PA15 keep the board WiFi module enabled and out of reset. */
	GPIO_InitStructure.GPIO_Pin=GPIO_Pin_4|GPIO_Pin_15;
	GPIO_InitStructure.GPIO_Speed=GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Mode=GPIO_Mode_Out_PP;
	GPIO_Init(GPIOA,&GPIO_InitStructure);
	GPIO_SetBits(GPIOA,GPIO_Pin_4|GPIO_Pin_15);

	/* USART3: PB10 is MCU TX, PB11 is MCU RX. */
	GPIO_InitStructure.GPIO_Pin=GPIO_Pin_10;
	GPIO_InitStructure.GPIO_Mode=GPIO_Mode_AF_PP;
	GPIO_Init(GPIOB,&GPIO_InitStructure);
	GPIO_InitStructure.GPIO_Pin=GPIO_Pin_11;
	GPIO_InitStructure.GPIO_Mode=GPIO_Mode_IN_FLOATING;
	GPIO_Init(GPIOB,&GPIO_InitStructure);

	USART_InitStructure.USART_BaudRate=bound;
	USART_InitStructure.USART_WordLength=USART_WordLength_8b;
	USART_InitStructure.USART_StopBits=USART_StopBits_1;
	USART_InitStructure.USART_Parity=USART_Parity_No;
	USART_InitStructure.USART_HardwareFlowControl=USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Mode=USART_Mode_Rx|USART_Mode_Tx;
	USART_Init(USART3,&USART_InitStructure);
	USART_Cmd(USART3,ENABLE);
	USART_ClearFlag(USART3,USART_FLAG_TC);
	USART_ITConfig(USART3,USART_IT_RXNE,ENABLE);

	NVIC_InitStructure.NVIC_IRQChannel=USART3_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority=3;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority=2;
	NVIC_InitStructure.NVIC_IRQChannelCmd=ENABLE;
	NVIC_Init(&NVIC_InitStructure);
}

void WiFi_SendByte(u8 byte)
{
	USART_SendData(USART3,byte);
	while(USART_GetFlagStatus(USART3,USART_FLAG_TXE)==RESET);
}

void WiFi_SendString(char *string)
{
	while(*string)
	{
		WiFi_SendByte((u8)*string);
		string++;
	}
}

u8 WiFi_TCP_QueueSend(u8 link_id,const u8 *data,u16 length)
{
	u16 i;

	if(length==0 || length>WIFI_TCP_TX_BUFFER_SIZE)return 0;
	if(WiFi_TCP_TxPending || WiFi_TCP_TxState!=WIFI_TX_STATE_IDLE)return 0;
	if(!WiFi_TCP_LinkValid || WiFi_TCP_LinkId!=link_id)return 0;

	for(i=0;i<length;i++)WiFi_TCP_TxBuffer[i]=data[i];
	WiFi_TCP_TxLength=length;
	WiFi_TCP_TxLinkId=link_id;
	WiFi_TCP_TxPending=1;
	WiFi_TCP_TxResult=WIFI_TCP_TX_RESULT_NONE;
	return 1;
}

u8 WiFi_TCP_TxBusy(void)
{
	return (WiFi_TCP_TxPending || WiFi_TCP_TxState!=WIFI_TX_STATE_IDLE)?1:0;
}

u16 WiFi_TCP_Read(u8 *destination,u16 max_length)
{
	u16 count=0;
	while(count<max_length && WiFi_TCP_StreamTail!=WiFi_TCP_StreamHead)
	{
		destination[count++]=WiFi_TCP_StreamBuffer[WiFi_TCP_StreamTail];
		WiFi_TCP_StreamTail=(u16)((WiFi_TCP_StreamTail+1)%WIFI_TCP_STREAM_BUFFER_SIZE);
	}
	return count;
}

void WiFi_TCP_ClearRx(void)
{
	WiFi_TCP_StreamTail=WiFi_TCP_StreamHead;
}

static void WiFi_TCP_FinishTx(u8 result)
{
	WiFi_TCP_TxPending=0;
	WiFi_TCP_TxState=WIFI_TX_STATE_IDLE;
	WiFi_TCP_TxEvent=WIFI_TX_EVENT_NONE;
	WiFi_TCP_TxResult=result;
	if(WiFi_AT_Flag==WIFI_AT_TCP_SEND)WiFi_AT_Flag=WIFI_AT_IDLE;
}

/* Call every 10 ms from the main loop. */
void WiFi_TCP_TxTask(void)
{
	char command[32];
	u16 i;

	if(WiFi_TCP_TxState==WIFI_TX_STATE_IDLE)
	{
		if(!WiFi_TCP_TxPending || WiFi_AT_Flag!=WIFI_AT_IDLE)return;
		if(!WiFi_TCP_LinkValid || WiFi_TCP_LinkId!=WiFi_TCP_TxLinkId)
		{
			WiFi_TCP_FinishTx(WIFI_TCP_TX_RESULT_ERROR);
			return;
		}

		WiFi_TCP_TxEvent=WIFI_TX_EVENT_NONE;
		WiFi_AT_Flag=WIFI_AT_TCP_SEND;
		sprintf(command,"AT+CIPSEND=%u,%u\r\n",
		        (unsigned int)WiFi_TCP_TxLinkId,
		        (unsigned int)WiFi_TCP_TxLength);
		WiFi_SendString(command);
		WiFi_TCP_TxTimer=WIFI_TX_PROMPT_TIMEOUT_TICKS;
		WiFi_TCP_TxState=WIFI_TX_STATE_WAIT_PROMPT;
		return;
	}

	if(WiFi_TCP_TxState==WIFI_TX_STATE_WAIT_PROMPT)
	{
		if(WiFi_TCP_TxEvent==WIFI_TX_EVENT_PROMPT)
		{
			WiFi_TCP_TxEvent=WIFI_TX_EVENT_NONE;
			for(i=0;i<WiFi_TCP_TxLength;i++)WiFi_SendByte(WiFi_TCP_TxBuffer[i]);
			WiFi_TCP_TxTimer=WIFI_TX_RESULT_TIMEOUT_TICKS;
			WiFi_TCP_TxState=WIFI_TX_STATE_WAIT_RESULT;
		}
		else if(WiFi_TCP_TxEvent==WIFI_TX_EVENT_ERROR || WiFi_TCP_TxTimer==0)
			WiFi_TCP_FinishTx(WIFI_TCP_TX_RESULT_ERROR);
		else WiFi_TCP_TxTimer--;
		return;
	}

	if(WiFi_TCP_TxEvent==WIFI_TX_EVENT_SENT)
		WiFi_TCP_FinishTx(WIFI_TCP_TX_RESULT_OK);
	else if(WiFi_TCP_TxEvent==WIFI_TX_EVENT_ERROR || WiFi_TCP_TxTimer==0)
		WiFi_TCP_FinishTx(WIFI_TCP_TX_RESULT_ERROR);
	else WiFi_TCP_TxTimer--;
}

void USART3_IRQHandler(void)
{
	u8 value;

	if(USART_GetITStatus(USART3,USART_IT_RXNE)==RESET)return;
	value=(u8)USART_ReceiveData(USART3);

	if(!WiFi_ParseIPD(value))
	{
		WiFi_ParseTarget(value);
		WiFi_ParseStaIP(value);
		WiFi_ParseLine(value);
	}
	USART_ClearITPendingBit(USART3,USART_IT_RXNE);
}
