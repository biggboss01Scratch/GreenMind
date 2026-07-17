#include "system.h"
#include "SysTick.h"
#include "usart.h"
#include "app_config.h"
#include "app_state.h"
#include "protocol.h"
#include "sensor_service.h"
#include "key_service.h"
#include "touch_service.h"
#include "ui_service.h"
#include <stdio.h>
#include <string.h>

#define STA_SCAN_INTERVAL_TICKS  1000
#define STA_SCAN_TIMEOUT_TICKS   1500
#define STA_JOIN_TIMEOUT_TICKS   3000
#define STA_QUERY_TIMEOUT_TICKS  500

typedef enum
{
	STA_SCAN_START=0,
	STA_SCAN_WAIT,
	STA_JOIN_START,
	STA_JOIN_WAIT,
	STA_IP_START,
	STA_IP_WAIT,
	STA_ONLINE_WAIT,
	STA_CHECK_START,
	STA_CHECK_WAIT,
	STA_RETRY_WAIT
} StaState;

typedef struct
{
	u8 link_id;
	u16 length;
	u8 data[WIFI_TCP_TX_BUFFER_SIZE];
} AppTxFrame;

static AppTxFrame g_tx_queue[APP_TX_QUEUE_SIZE];
static u8 g_tx_head=0;
static u8 g_tx_tail=0;
static u8 g_hello_pending=0;
static u8 g_sta_retry_token=0;

#define WIFI_WAIT_OK       1
#define WIFI_WAIT_ERROR    2
#define WIFI_WAIT_TIMEOUT  3

static void WiFi_ModuleEnable_Init(void)
{
	GPIO_InitTypeDef gpio;
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA,ENABLE);
	gpio.GPIO_Pin=GPIO_Pin_4|GPIO_Pin_15;
	gpio.GPIO_Speed=GPIO_Speed_50MHz;
	gpio.GPIO_Mode=GPIO_Mode_Out_PP;
	GPIO_Init(GPIOA,&gpio);
	GPIO_SetBits(GPIOA,GPIO_Pin_4|GPIO_Pin_15);
}

static void App_SetWifiStatus(AppState *app,const char *status,u8 connected)
{
	if(strcmp(app->wifi_status,status)!=0 || app->wifi_connected!=connected)
	{
		AppState_CopyText(app->wifi_status,sizeof(app->wifi_status),status);
		app->wifi_connected=connected;
		app->ui_dirty=1;
		printf("[WIFI] %s\r\n",status);
	}
}

static void App_SetAiState(AppState *app,AppAiState state)
{
	if(app->ai_state!=state)
	{
		app->ai_state=state;
		app->ui_dirty=1;
		printf("[AI] %s (request=%u)\r\n",AppState_AiStateText(state),
		       (unsigned int)app->ai_request_id);
	}
}

static u8 WiFi_SendAndWait(char *command,u16 timeout_ms)
{
	WiFi_AT_Flag=WIFI_AT_WAITING;
	WiFi_SendString(command);
	while(timeout_ms--)
	{
		if(WiFi_AT_Flag==WIFI_AT_OK)
		{
			WiFi_AT_Flag=WIFI_AT_IDLE;
			return WIFI_WAIT_OK;
		}
		if(WiFi_AT_Flag==WIFI_AT_ERROR)
		{
			WiFi_AT_Flag=WIFI_AT_IDLE;
			return WIFI_WAIT_ERROR;
		}
		delay_ms(1);
	}
	WiFi_AT_Flag=WIFI_AT_IDLE;
	return WIFI_WAIT_TIMEOUT;
}

static void WiFi_StartAsync(char *command)
{
	WiFi_AT_Flag=WIFI_AT_WAITING;
	WiFi_SendString(command);
}

static s8 WiFi_ConsumeAsyncResult(void)
{
	if(WiFi_AT_Flag==WIFI_AT_OK)
	{
		WiFi_AT_Flag=WIFI_AT_IDLE;
		return 1;
	}
	if(WiFi_AT_Flag==WIFI_AT_ERROR)
	{
		WiFi_AT_Flag=WIFI_AT_IDLE;
		return -1;
	}
	return 0;
}

static u8 WiFi_ConfigStep(AppState *app,char *stage,char *command,u16 timeout_ms)
{
	char status[APP_STATE_TEXT_MEDIUM];
	u8 result;

	sprintf(status,"INIT:%s",stage);
	App_SetWifiStatus(app,status,0);
	UiService_Render(app);
	app->ui_dirty=0;
	printf("[WIFI INIT] %s SEND\r\n",stage);

	result=WiFi_SendAndWait(command,timeout_ms);
	if(result==WIFI_WAIT_OK)
	{
		printf("[WIFI INIT] %s OK\r\n",stage);
		return 1;
	}

	if(result==WIFI_WAIT_ERROR)
	{
		sprintf(status,"ERROR:%s",stage);
		App_SetWifiStatus(app,status,0);
		sprintf(status,"ESP ERROR %s",stage);
		AppState_CopyText(app->last_error,sizeof(app->last_error),status);
		printf("[WIFI INIT] %s ERROR RESPONSE\r\n",stage);
	}
	else
	{
		sprintf(status,"TIMEOUT:%s",stage);
		App_SetWifiStatus(app,status,0);
		sprintf(status,"ESP TIMEOUT %s",stage);
		AppState_CopyText(app->last_error,sizeof(app->last_error),status);
		printf("[WIFI INIT] %s TIMEOUT\r\n",stage);
	}

	UiService_Render(app);
	app->ui_dirty=0;
	return 0;
}

static u8 WiFi_ConfigServer(AppState *app)
{
	char command[96];

	App_SetWifiStatus(app,"ESP STARTING",0);
	UiService_Render(app);
	app->ui_dirty=0;
	delay_ms(3000);

	if(!WiFi_ConfigStep(app,"AT","AT\r\n",2000))return 0;
	if(!WiFi_ConfigStep(app,"CWMODE","AT+CWMODE_DEF=3\r\n",5000))return 0;
	sprintf(command,"AT+CWSAP_DEF=\"%s\",\"%s\",11,4\r\n",
	        APP_WIFI_AP_SSID,APP_WIFI_AP_PASSWORD);
	if(!WiFi_ConfigStep(app,"CWSAP",command,5000))return 0;
	if(!WiFi_ConfigStep(app,"CIPAP","AT+CIPAP_DEF=\"192.168.4.1\"\r\n",5000))return 0;
	if(!WiFi_ConfigStep(app,"CIPMUX","AT+CIPMUX=1\r\n",5000))return 0;
	sprintf(command,"AT+CIPSERVER=1,%u\r\n",(unsigned int)APP_WIFI_TCP_PORT);
	if(!WiFi_ConfigStep(app,"CIPSERVER",command,5000))return 0;

	WiFi_AT_Flag=WIFI_AT_IDLE;
	WiFi_STA_IP_Valid=0;
	App_SetWifiStatus(app,"SEARCHING",0);
	return 1;
}

static void WiFi_RequestRetry(AppState *app,u8 *wifi_ready)
{
	if(!(*wifi_ready))
	{
		AppState_CopyText(app->last_error,sizeof(app->last_error),"RETRY ESP CONFIG");
		app->ui_dirty=1;
		*wifi_ready=WiFi_ConfigServer(app);
		if(!(*wifi_ready))
			AppState_CopyText(app->last_error,sizeof(app->last_error),
			                  "ESP CONFIG FAILED");
		return;
	}

	if(app->wifi_connected)
	{
		if(app->gateway_ready)
			AppState_CopyText(app->last_error,sizeof(app->last_error),"NONE");
		else
			AppState_CopyText(app->last_error,sizeof(app->last_error),
			                  "WAIT FOR GATEWAY");
		app->ui_dirty=1;
		return;
	}

	g_sta_retry_token++;
	App_SetWifiStatus(app,"RETRY REQUESTED",0);
	AppState_CopyText(app->last_error,sizeof(app->last_error),"NONE");
	app->ui_dirty=1;
}

static void WiFi_StaTask(AppState *app)
{
	static StaState state=STA_SCAN_START;
	static u16 timer=0;
	static u8 retry_seen=0;
	char command[96];
	s8 result;

	if(retry_seen!=g_sta_retry_token && WiFi_AT_Flag==WIFI_AT_IDLE)
	{
		retry_seen=g_sta_retry_token;
		state=STA_SCAN_START;
		timer=0;
	}

	switch(state)
	{
		case STA_SCAN_START:
			if(WiFi_AT_Flag!=WIFI_AT_IDLE)break;
			WiFi_TargetFound=0;
			App_SetWifiStatus(app,"SCANNING",0);
			WiFi_StartAsync("AT+CWLAP\r\n");
			timer=STA_SCAN_TIMEOUT_TICKS;
			state=STA_SCAN_WAIT;
			break;

		case STA_SCAN_WAIT:
			result=WiFi_ConsumeAsyncResult();
			if(result==1 && WiFi_TargetFound)state=STA_JOIN_START;
			else if(result!=0 || timer==0)
			{
				App_SetWifiStatus(app,"TARGET NOT FOUND",0);
				timer=STA_SCAN_INTERVAL_TICKS;
				state=STA_RETRY_WAIT;
			}
			else timer--;
			break;

		case STA_JOIN_START:
			if(WiFi_AT_Flag!=WIFI_AT_IDLE)break;
			WiFi_STA_IP_Valid=0;
			App_SetWifiStatus(app,"CONNECTING",0);
			sprintf(command,"AT+CWJAP_DEF=\"%s\",\"%s\"\r\n",
			        APP_WIFI_SSID,APP_WIFI_PASSWORD);
			WiFi_StartAsync(command);
			timer=STA_JOIN_TIMEOUT_TICKS;
			state=STA_JOIN_WAIT;
			break;

		case STA_JOIN_WAIT:
			result=WiFi_ConsumeAsyncResult();
			if(result==1)state=STA_IP_START;
			else if(result!=0 || timer==0)
			{
				App_SetWifiStatus(app,"CONNECT FAILED",0);
				timer=STA_SCAN_INTERVAL_TICKS;
				state=STA_RETRY_WAIT;
			}
			else timer--;
			break;

		case STA_IP_START:
			if(WiFi_AT_Flag!=WIFI_AT_IDLE)break;
			WiFi_STA_IP_Valid=0;
			WiFi_StartAsync("AT+CIFSR\r\n");
			timer=STA_QUERY_TIMEOUT_TICKS;
			state=STA_IP_WAIT;
			break;

		case STA_IP_WAIT:
			result=WiFi_ConsumeAsyncResult();
			if(result==1)
			{
				App_SetWifiStatus(app,"CONNECTED",1);
				if(WiFi_STA_IP_Valid)
					AppState_CopyText(app->sta_ip,sizeof(app->sta_ip),WiFi_STA_IP);
				app->ui_dirty=1;
				timer=STA_SCAN_INTERVAL_TICKS;
				state=STA_ONLINE_WAIT;
			}
			else if(result<0 || timer==0)
			{
				App_SetWifiStatus(app,"IP QUERY FAILED",0);
				timer=STA_SCAN_INTERVAL_TICKS;
				state=STA_RETRY_WAIT;
			}
			else timer--;
			break;

		case STA_ONLINE_WAIT:
			if(timer==0)state=STA_CHECK_START;
			else timer--;
			break;

		case STA_CHECK_START:
			if(WiFi_AT_Flag!=WIFI_AT_IDLE)break;
			WiFi_TargetFound=0;
			WiFi_StartAsync("AT+CWJAP?\r\n");
			timer=STA_QUERY_TIMEOUT_TICKS;
			state=STA_CHECK_WAIT;
			break;

		case STA_CHECK_WAIT:
			result=WiFi_ConsumeAsyncResult();
			if(result==1 && WiFi_TargetFound)
			{
				timer=STA_SCAN_INTERVAL_TICKS;
				state=STA_ONLINE_WAIT;
			}
			else if(result!=0 || timer==0)
			{
				App_SetWifiStatus(app,"DISCONNECTED",0);
				app->gateway_ready=0;
				timer=STA_SCAN_INTERVAL_TICKS;
				state=STA_RETRY_WAIT;
			}
			else timer--;
			break;

		case STA_RETRY_WAIT:
			if(timer==0)state=STA_SCAN_START;
			else timer--;
			break;
	}
}

static u8 AppTx_Enqueue(const u8 *data,u16 length)
{
	u8 next;
	u16 i;
	if(!WiFi_TCP_LinkValid || length==0 || length>WIFI_TCP_TX_BUFFER_SIZE)return 0;
	next=(u8)((g_tx_head+1)%APP_TX_QUEUE_SIZE);
	if(next==g_tx_tail)return 0;
	g_tx_queue[g_tx_head].link_id=WiFi_TCP_LinkId;
	g_tx_queue[g_tx_head].length=length;
	for(i=0;i<length;i++)g_tx_queue[g_tx_head].data[i]=data[i];
	g_tx_head=next;
	return 1;
}

static u8 AppTx_EnqueueText(const char *text)
{
	return AppTx_Enqueue((const u8 *)text,(u16)strlen(text));
}

static void AppTx_Clear(void)
{
	g_tx_head=0;
	g_tx_tail=0;
}

static void AppTx_Task(void)
{
	if(g_tx_tail==g_tx_head || WiFi_TCP_TxBusy())return;
	if(WiFi_TCP_QueueSend(g_tx_queue[g_tx_tail].link_id,
	                     g_tx_queue[g_tx_tail].data,
	                     g_tx_queue[g_tx_tail].length))
		g_tx_tail=(u8)((g_tx_tail+1)%APP_TX_QUEUE_SIZE);
}

static u8 App_AiActive(const AppState *app)
{
	return (app->ai_state==APP_AI_SENDING || app->ai_state==APP_AI_PREPARING ||
	        app->ai_state==APP_AI_THINKING || app->ai_state==APP_AI_VALIDATING)?1:0;
}

static void App_HandleProtocolEvent(AppState *app,const ProtocolEvent *event)
{
	if(event->type==PROTOCOL_EVENT_HELLO_ACK)
	{
		app->gateway_ready=1;
		AppState_CopyText(app->last_error,sizeof(app->last_error),"NONE");
		app->ui_dirty=1;
		printf("[GATEWAY] READY\r\n");
	}
	else if(event->type==PROTOCOL_EVENT_PING)
		AppTx_EnqueueText("V1|PONG\r\n");
	else if(event->type==PROTOCOL_EVENT_ACK &&
	        event->request_id==app->ai_request_id && App_AiActive(app))
	{
		App_SetAiState(app,APP_AI_PREPARING);
		app->ai_wait_ticks=APP_AI_TIMEOUT_TICKS;
	}
	else if(event->type==PROTOCOL_EVENT_AI_STAGE &&
	        event->request_id==app->ai_request_id && App_AiActive(app))
	{
		if(strcmp(event->status,"PREPARING")==0)App_SetAiState(app,APP_AI_PREPARING);
		else if(strcmp(event->status,"THINKING")==0)App_SetAiState(app,APP_AI_THINKING);
		else if(strcmp(event->status,"VALIDATING")==0)App_SetAiState(app,APP_AI_VALIDATING);
		app->ai_wait_ticks=APP_AI_TIMEOUT_TICKS;
	}
	else if(event->type==PROTOCOL_EVENT_AI_RESULT &&
	        event->request_id==app->ai_request_id && App_AiActive(app))
	{
		AppState_CopyText(app->ai_status,sizeof(app->ai_status),event->status);
		AppState_CopyText(app->ai_issue,sizeof(app->ai_issue),event->issue);
		AppState_CopyText(app->ai_watering,sizeof(app->ai_watering),event->watering);
		AppState_CopyText(app->ai_advice,sizeof(app->ai_advice),event->advice);
		AppState_CopyText(app->last_error,sizeof(app->last_error),"NONE");
		app->ai_wait_ticks=0;
		app->page=APP_PAGE_AI;
		App_SetAiState(app,APP_AI_DONE);
		printf("[AI] RESULT status=%s issue=%s water=%s advice=%s\r\n",
		       event->status,event->issue,event->watering,event->advice);
	}
	else if(event->type==PROTOCOL_EVENT_ERROR &&
	        (event->request_id==0 || event->request_id==app->ai_request_id))
	{
		AppState_CopyText(app->last_error,sizeof(app->last_error),event->error);
		app->ai_wait_ticks=0;
		app->page=APP_PAGE_AI;
		App_SetAiState(app,APP_AI_ERROR);
		printf("[AI] ERROR %s\r\n",event->error);
	}
	else if(event->type==PROTOCOL_EVENT_BAD_FRAME)
	{
		AppState_CopyText(app->last_error,sizeof(app->last_error),event->error);
		app->ui_dirty=1;
		printf("[PROTOCOL] %s\r\n",event->error);
	}
}

static void AppTcp_Task(AppState *app)
{
	u8 chunk[64];
	u16 length;
	u8 reads=0;
	ProtocolEvent event;
	static u16 previous_dropped=0;
	static u16 previous_event_dropped=0;

	if(WiFi_TCP_ClientEvent==WIFI_TCP_CLIENT_CONNECTED)
	{
		WiFi_TCP_ClientEvent=WIFI_TCP_CLIENT_NONE;
		WiFi_TCP_ClearRx();
		Protocol_Init();
		previous_event_dropped=0;
		AppTx_Clear();
		app->tcp_connected=1;
		app->gateway_ready=0;
		g_hello_pending=1;
		app->ui_dirty=1;
		printf("[TCP] CLIENT CONNECTED id=%u\r\n",(unsigned int)WiFi_TCP_LinkId);
	}
	else if(WiFi_TCP_ClientEvent==WIFI_TCP_CLIENT_CLOSED)
	{
		WiFi_TCP_ClientEvent=WIFI_TCP_CLIENT_NONE;
		WiFi_TCP_ClearRx();
		Protocol_Init();
		AppTx_Clear();
		g_hello_pending=0;
		app->tcp_connected=0;
		app->gateway_ready=0;
		if(App_AiActive(app))
		{
			AppState_CopyText(app->last_error,sizeof(app->last_error),"GATEWAY LOST");
			App_SetAiState(app,APP_AI_ERROR);
		}
		app->ui_dirty=1;
		printf("[TCP] CLIENT CLOSED\r\n");
	}

	if(g_hello_pending && AppTx_EnqueueText("V1|HELLO|STM32|READY\r\n"))
		g_hello_pending=0;

	while(reads<4)
	{
		length=WiFi_TCP_Read(chunk,sizeof(chunk));
		if(length==0)break;
		Protocol_InputBytes(chunk,length);
		reads++;
	}
	while(Protocol_GetEvent(&event))App_HandleProtocolEvent(app,&event);

	if(Protocol_GetDroppedEventCount()!=previous_event_dropped)
	{
		previous_event_dropped=Protocol_GetDroppedEventCount();
		AppState_CopyText(app->last_error,sizeof(app->last_error),"EVENT OVERFLOW");
		if(App_AiActive(app))
		{
			app->ai_wait_ticks=0;
			App_SetAiState(app,APP_AI_ERROR);
		}
		app->ui_dirty=1;
		printf("[PROTOCOL] EVENT OVERFLOW count=%u\r\n",
		       (unsigned int)previous_event_dropped);
	}

	if(WiFi_TCP_TxResult==WIFI_TCP_TX_RESULT_ERROR)
	{
		AppState_CopyText(app->last_error,sizeof(app->last_error),"TCP SEND FAILED");
		app->ui_dirty=1;
		printf("[TCP] SEND FAILED\r\n");
	}
	WiFi_TCP_TxResult=WIFI_TCP_TX_RESULT_NONE;

	if(WiFi_TCP_RxDropped!=previous_dropped)
	{
		previous_dropped=WiFi_TCP_RxDropped;
		AppState_CopyText(app->last_error,sizeof(app->last_error),"RX OVERFLOW");
		app->ui_dirty=1;
		printf("[TCP] RX OVERFLOW count=%u\r\n",(unsigned int)previous_dropped);
	}
}

static void App_RequestAi(AppState *app)
{
	u8 frame[WIFI_TCP_TX_BUFFER_SIZE];
	u16 length;

	app->page=APP_PAGE_AI;
	if(!app->dht_valid || !app->light_valid)
	{
		AppState_CopyText(app->last_error,sizeof(app->last_error),"SENSOR INVALID");
		App_SetAiState(app,APP_AI_ERROR);
		app->ui_dirty=1;
		return;
	}
	if(!app->gateway_ready)
	{
		AppState_CopyText(app->last_error,sizeof(app->last_error),"GATEWAY OFFLINE");
		App_SetAiState(app,APP_AI_ERROR);
		app->ui_dirty=1;
		return;
	}
	if(App_AiActive(app))
	{
		AppState_CopyText(app->last_error,sizeof(app->last_error),"AI BUSY");
		app->ui_dirty=1;
		return;
	}

	app->ai_request_id++;
	length=Protocol_BuildAiRequest(frame,sizeof(frame),app->ai_request_id,
	                              app->temperature,app->humidity,app->light,
	                              AppState_LightLevel(app->light));
	if(length==0 || !AppTx_Enqueue(frame,length))
	{
		AppState_CopyText(app->last_error,sizeof(app->last_error),"TX QUEUE FULL");
		App_SetAiState(app,APP_AI_ERROR);
		app->ui_dirty=1;
		return;
	}

	AppState_CopyText(app->ai_status,sizeof(app->ai_status),"PENDING");
	AppState_CopyText(app->ai_issue,sizeof(app->ai_issue),"-");
	AppState_CopyText(app->ai_watering,sizeof(app->ai_watering),"-");
	AppState_CopyText(app->ai_advice,sizeof(app->ai_advice),"-");
	AppState_CopyText(app->last_error,sizeof(app->last_error),"NONE");
	app->ai_wait_ticks=APP_AI_TIMEOUT_TICKS;
	App_SetAiState(app,APP_AI_SENDING);
	printf("[AI] REQUEST T=%u H=%u L=%u %s\r\n",
	       (unsigned int)app->temperature,(unsigned int)app->humidity,
	       (unsigned int)app->light,AppState_LightLevel(app->light));
}

static void App_AiTimeoutTask(AppState *app)
{
	if(!App_AiActive(app))return;
	if(app->ai_wait_ticks>0)app->ai_wait_ticks--;
	if(app->ai_wait_ticks==0)
	{
		AppState_CopyText(app->last_error,sizeof(app->last_error),"MODEL TIMEOUT");
		App_SetAiState(app,APP_AI_TIMEOUT);
	}
}

int main(void)
{
	AppState app;
	AppKeyEvent key_event;
	AppTouchEvent touch_event;
	u8 wifi_ready;
	u16 ui_timer=0;
	u16 log_timer=200;

	SysTick_Init(72);
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
	USART1_Init(115200);
	AppState_Init(&app);
	WiFi_ModuleEnable_Init();
	UiService_Init();
	TouchService_Init();
	UiService_Render(&app);
	app.ui_dirty=0;
	USART3_Init(115200);
	KeyService_Init();
	SensorService_Init(&app);
	Protocol_Init();
	AppTx_Clear();

	printf("\r\n[BOOT] Agriculture MVP firmware start\r\n");
	wifi_ready=WiFi_ConfigServer(&app);
	if(!wifi_ready)printf("[WIFI INIT] STOPPED - SEE FAILED STAGE\r\n");

	while(1)
	{
		WiFi_TCP_TxTask();
		AppTx_Task();
		AppTcp_Task(&app);
		if(wifi_ready)WiFi_StaTask(&app);
		SensorService_Task(&app);

		key_event=KeyService_Task();
		if(key_event==APP_KEY_EVENT_PAGE)
		{
			app.page=(AppPage)(((u8)app.page+1)%APP_PAGE_COUNT);
			app.ui_dirty=1;
			printf("[KEY] PAGE\r\n");
		}
		else if(key_event==APP_KEY_EVENT_AI_REQUEST)
		{
			printf("[KEY] AI REQUEST\r\n");
			App_RequestAi(&app);
		}

		touch_event=TouchService_Task(app.page);
		if(touch_event==APP_TOUCH_EVENT_HOME)
		{
			app.page=APP_PAGE_HOME;
			app.ui_dirty=1;
			printf("[TOUCH] HOME\r\n");
		}
		else if(touch_event==APP_TOUCH_EVENT_DETAIL)
		{
			app.page=APP_PAGE_DETAIL;
			app.ui_dirty=1;
			printf("[TOUCH] DETAIL\r\n");
		}
		else if(touch_event==APP_TOUCH_EVENT_AI_PAGE)
		{
			app.page=APP_PAGE_AI;
			app.ui_dirty=1;
			printf("[TOUCH] AI PAGE\r\n");
		}
		else if(touch_event==APP_TOUCH_EVENT_SYSTEM)
		{
			app.page=APP_PAGE_SYSTEM;
			app.ui_dirty=1;
			printf("[TOUCH] SYSTEM\r\n");
		}
		else if(touch_event==APP_TOUCH_EVENT_AI_REQUEST)
		{
			printf("[TOUCH] AI REQUEST\r\n");
			App_RequestAi(&app);
		}
		else if(touch_event==APP_TOUCH_EVENT_NETWORK_RETRY)
		{
			printf("[TOUCH] NETWORK RETRY\r\n");
			WiFi_RequestRetry(&app,&wifi_ready);
		}
		else if(touch_event==APP_TOUCH_EVENT_CALIBRATE)
		{
			printf("[TOUCH] CALIBRATE\r\n");
			TouchService_Adjust();
			UiService_Invalidate();
			app.ui_dirty=1;
		}

		App_AiTimeoutTask(&app);
		if(app.ui_dirty && ui_timer==0)
		{
			UiService_Render(&app);
			app.ui_dirty=0;
			ui_timer=APP_UI_REFRESH_MIN_TICKS;
		}
		else if(ui_timer>0)ui_timer--;

		if(log_timer==0)
		{
			printf("[STATUS] T=%u H=%u L=%u DHT=%s WIFI=%s TCP=%s GW=%s AI=%s\r\n",
			       (unsigned int)app.temperature,(unsigned int)app.humidity,
			       (unsigned int)app.light,app.dht_valid?"OK":"ERROR",
			       app.wifi_status,app.tcp_connected?"UP":"DOWN",
			       app.gateway_ready?"READY":"OFFLINE",
			       AppState_AiStateText(app.ai_state));
			log_timer=200;
		}
		else log_timer--;

		delay_ms(APP_TASK_PERIOD_MS);
	}
}
