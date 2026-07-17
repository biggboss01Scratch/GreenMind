#include "system.h"
#include "SysTick.h"
#include "usart.h"
#include "app_config.h"
#include "app_state.h"
#include "protocol.h"
#include "asset_service.h"
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

static void App_ClearPlantItems(AppState *app)
{
	u8 i;
	app->plant_list_count=0;
	for(i=0;i<APP_PLANT_LIST_MAX;i++)
	{
		app->plant_items[i].valid=0;
		app->plant_items[i].species_id[0]='\0';
		app->plant_items[i].display_name[0]='\0';
		app->plant_items[i].source_type[0]='\0';
	}
}

static void App_ResetAiForPlant(AppState *app)
{
	app->ai_state=APP_AI_IDLE;
	app->ai_wait_ticks=0;
	AppState_CopyText(app->ai_status,sizeof(app->ai_status),"IDLE");
	AppState_CopyText(app->ai_issue,sizeof(app->ai_issue),"-");
	AppState_CopyText(app->ai_watering,sizeof(app->ai_watering),"-");
	AppState_CopyText(app->ai_advice,sizeof(app->ai_advice),"-");
}

static u8 App_QueuePlantListRequest(const AppState *app)
{
	u8 frame[WIFI_TCP_TX_BUFFER_SIZE];
	u16 length;
	length=Protocol_BuildPlantListRequest(frame,sizeof(frame),app->device_id);
	if(length==0)return 0;
	return AppTx_Enqueue(frame,length);
}

static const char *App_DesiredAssetState(const AppState *app)
{
	if(app->ai_state==APP_AI_DONE)
	{
		if(strcmp(app->ai_status,"DANGER")==0)return "DANGER";
		if(strcmp(app->ai_status,"WARN")==0)return "ATTENTION";
	}
	return "NORMAL";
}

static void App_SetBuiltInAsset(AppState *app,const char *state)
{
	AssetService_Cancel("BUILTIN ACTIVE");
	AppState_CopyText(app->asset_status,sizeof(app->asset_status),"BUILTIN");
	AppState_CopyText(app->asset_species_id,sizeof(app->asset_species_id),
	                  "pothos");
	AppState_CopyText(app->asset_state,sizeof(app->asset_state),state);
	AppState_CopyText(app->asset_error,sizeof(app->asset_error),"NONE");
	app->asset_retry_count=0;
	app->asset_revision++;
	app->ui_dirty=1;
}

static u8 App_StartAssetRequest(AppState *app,const char *state,u8 reset_retry)
{
	u8 frame[WIFI_TCP_TX_BUFFER_SIZE];
	u16 length;

	if(strcmp(app->species_id,"pothos")==0)
	{
		App_SetBuiltInAsset(app,state);
		return 1;
	}
	if(strcmp(app->species_id,"cactus")!=0)
	{
		AssetService_Cancel("CACTUS TEST ONLY");
		AppState_CopyText(app->asset_status,sizeof(app->asset_status),
		                  "PLACEHOLDER");
		AppState_CopyText(app->asset_species_id,
		                  sizeof(app->asset_species_id),app->species_id);
		AppState_CopyText(app->asset_state,sizeof(app->asset_state),state);
		AppState_CopyText(app->asset_error,sizeof(app->asset_error),
		                  "CACTUS TEST ONLY");
		app->asset_retry_count=0;
		app->asset_revision++;
		app->ui_dirty=1;
		return 1;
	}
	if(!app->gateway_ready)
	{
		AppState_CopyText(app->asset_status,sizeof(app->asset_status),"ERROR");
		AppState_CopyText(app->asset_error,sizeof(app->asset_error),
		                  "GATEWAY OFFLINE");
		app->asset_revision++;
		app->ui_dirty=1;
		return 0;
	}
	if((AssetService_GetState()==ASSET_TRANSFER_REQUESTING ||
	    AssetService_GetState()==ASSET_TRANSFER_RECEIVING) &&
	   strcmp(AssetService_GetSpeciesId(),app->species_id)==0 &&
	   strcmp(AssetService_GetAssetState(),state)==0)
		return 1;
	if(AssetService_HasImage(app->species_id,state))
	{
		AppState_CopyText(app->asset_status,sizeof(app->asset_status),"READY");
		AppState_CopyText(app->asset_species_id,sizeof(app->asset_species_id),
		                  app->species_id);
		AppState_CopyText(app->asset_state,sizeof(app->asset_state),state);
		AppState_CopyText(app->asset_error,sizeof(app->asset_error),"NONE");
		app->asset_revision++;
		app->ui_dirty=1;
		return 1;
	}

	app->asset_request_id++;
	length=Protocol_BuildAssetRequest(frame,sizeof(frame),app->asset_request_id,
	                                  app->species_id,state);
	if(length==0 || !AppTx_Enqueue(frame,length))
	{
		AppState_CopyText(app->asset_status,sizeof(app->asset_status),"ERROR");
		AppState_CopyText(app->asset_error,sizeof(app->asset_error),
		                  "ASSET TX FULL");
		app->asset_revision++;
		app->ui_dirty=1;
		return 0;
	}
	if(reset_retry)app->asset_retry_count=0;
	AssetService_StartRequest(app->asset_request_id,app->species_id,state);
	AppState_CopyText(app->asset_status,sizeof(app->asset_status),"LOADING");
	AppState_CopyText(app->asset_species_id,sizeof(app->asset_species_id),
	                  app->species_id);
	AppState_CopyText(app->asset_state,sizeof(app->asset_state),state);
	AppState_CopyText(app->asset_error,sizeof(app->asset_error),"NONE");
	app->asset_revision++;
	app->ui_dirty=1;
	printf("[ASSET] REQUEST id=%u species=%s state=%s retry=%u\r\n",
	       (unsigned int)app->asset_request_id,app->species_id,state,
	       (unsigned int)app->asset_retry_count);
	return 1;
}

static void App_HandleAssetFailure(AppState *app,const char *error)
{
	char state[APP_STATE_TEXT_SMALL];
	AppState_CopyText(state,sizeof(state),AssetService_GetAssetState());
	if(app->gateway_ready && app->asset_retry_count<1 &&
	   strcmp(app->species_id,"pothos")!=0 && state[0]!='\0')
	{
		app->asset_retry_count++;
		printf("[ASSET] RETRY error=%s\r\n",error);
		if(App_StartAssetRequest(app,state,0))return;
	}
	AppState_CopyText(app->asset_status,sizeof(app->asset_status),"ERROR");
	AppState_CopyText(app->asset_error,sizeof(app->asset_error),error);
	AppState_CopyText(app->last_error,sizeof(app->last_error),error);
	app->asset_revision++;
	app->ui_dirty=1;
	printf("[ASSET] ERROR %s\r\n",error);
}

static void App_HandleProtocolEvent(AppState *app,const ProtocolEvent *event)
{
	u8 i;
	if(event->type==PROTOCOL_EVENT_HELLO_ACK)
	{
		app->gateway_ready=1;
		AppState_CopyText(app->last_error,sizeof(app->last_error),"NONE");
		app->ui_dirty=1;
		printf("[GATEWAY] READY\r\n");
		App_ClearPlantItems(app);
		app->plant_list_state=APP_PLANT_DATA_LOADING;
		if(App_QueuePlantListRequest(app))
			printf("[PLANT] STARTUP SYNC device=%s\r\n",app->device_id);
		else
		{
			app->plant_list_state=APP_PLANT_DATA_ERROR;
			AppState_CopyText(app->plant_error,sizeof(app->plant_error),
			                  "SYNC QUEUE FAILED");
		}
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
		App_StartAssetRequest(app,App_DesiredAssetState(app),1);
		printf("[AI] RESULT status=%s issue=%s water=%s advice=%s\r\n",
		       event->status,event->issue,event->watering,event->advice);
	}
	else if(event->type==PROTOCOL_EVENT_PLANT_LIST_BEGIN)
	{
		App_ClearPlantItems(app);
		app->plant_list_expected=event->item_count;
		if(app->plant_list_expected>APP_PLANT_LIST_MAX)
			app->plant_list_expected=APP_PLANT_LIST_MAX;
		app->plant_list_state=APP_PLANT_DATA_LOADING;
		AppState_CopyText(app->plant_error,sizeof(app->plant_error),"NONE");
		if(strcmp(app->species_id,event->species_id)!=0)
		{
			AppState_CopyText(app->species_id,sizeof(app->species_id),
			                  event->species_id);
			AppState_CopyText(app->plant_name,sizeof(app->plant_name),
			                  event->display_name);
			App_ResetAiForPlant(app);
			app->ui_dirty=1;
			printf("[PLANT] CURRENT SYNC species=%s\r\n",app->species_id);
			App_StartAssetRequest(app,"NORMAL",1);
		}
		printf("[PLANT] LIST BEGIN count=%u\r\n",
		       (unsigned int)event->item_count);
	}
	else if(event->type==PROTOCOL_EVENT_PLANT_ITEM)
	{
		i=event->item_index;
		if(i<APP_PLANT_LIST_MAX)
		{
			app->plant_items[i].valid=1;
			AppState_CopyText(app->plant_items[i].species_id,
			                  sizeof(app->plant_items[i].species_id),
			                  event->species_id);
			AppState_CopyText(app->plant_items[i].display_name,
			                  sizeof(app->plant_items[i].display_name),
			                  event->display_name);
			AppState_CopyText(app->plant_items[i].source_type,
			                  sizeof(app->plant_items[i].source_type),
			                  event->source_type);
			if((u8)(i+1)>app->plant_list_count)
				app->plant_list_count=(u8)(i+1);
		}
	}
	else if(event->type==PROTOCOL_EVENT_PLANT_LIST_END)
	{
		if(app->plant_list_count>0)
		{
			app->plant_list_state=APP_PLANT_DATA_READY;
			AppState_CopyText(app->plant_error,sizeof(app->plant_error),"NONE");
		}
		else
		{
			app->plant_list_state=APP_PLANT_DATA_ERROR;
			AppState_CopyText(app->plant_error,sizeof(app->plant_error),
			                  "EMPTY PLANT LIST");
		}
		app->plant_ui_revision++;
		app->ui_dirty=1;
		App_StartAssetRequest(app,App_DesiredAssetState(app),1);
		printf("[PLANT] LIST READY received=%u announced=%u\r\n",
		       (unsigned int)app->plant_list_count,
		       (unsigned int)event->item_count);
	}
	else if(event->type==PROTOCOL_EVENT_PLANT_DETAIL)
	{
		AppState_CopyText(app->plant_detail_species_id,
		                  sizeof(app->plant_detail_species_id),
		                  event->species_id);
		AppState_CopyText(app->plant_detail_name,
		                  sizeof(app->plant_detail_name),
		                  event->display_name);
		AppState_CopyText(app->plant_detail_source,
		                  sizeof(app->plant_detail_source),
		                  event->source_type);
		app->plant_temp_min=event->temp_min;
		app->plant_temp_max=event->temp_max;
		app->plant_humidity_min=event->humidity_min;
		app->plant_humidity_max=event->humidity_max;
		app->plant_light_min=event->light_min;
		app->plant_light_max=event->light_max;
		app->plant_detail_state=APP_PLANT_DATA_READY;
		app->plant_selection_pending=0;
		AppState_CopyText(app->plant_error,sizeof(app->plant_error),"NONE");
		app->plant_ui_revision++;
		app->ui_dirty=1;
		printf("[PLANT] DETAIL %s T=%u-%u H=%u-%u L=%u-%u\r\n",
		       app->plant_detail_species_id,
		       (unsigned int)app->plant_temp_min,
		       (unsigned int)app->plant_temp_max,
		       (unsigned int)app->plant_humidity_min,
		       (unsigned int)app->plant_humidity_max,
		       (unsigned int)app->plant_light_min,
		       (unsigned int)app->plant_light_max);
	}
	else if(event->type==PROTOCOL_EVENT_PLANT_SELECTED)
	{
		AppState_CopyText(app->species_id,sizeof(app->species_id),
		                  event->species_id);
		AppState_CopyText(app->plant_name,sizeof(app->plant_name),
		                  event->display_name);
		app->plant_selection_pending=0;
		AppState_CopyText(app->plant_error,sizeof(app->plant_error),"NONE");
		AppState_CopyText(app->last_error,sizeof(app->last_error),"NONE");
		App_ResetAiForPlant(app);
		app->plant_ui_revision++;
		app->page=APP_PAGE_HOME;
		app->ui_dirty=1;
		App_StartAssetRequest(app,"NORMAL",1);
		printf("[PLANT] SELECTED device=%s species=%s\r\n",
		       app->device_id,app->species_id);
	}
	else if(event->type==PROTOCOL_EVENT_PLANT_ERROR)
	{
		app->plant_selection_pending=0;
		if(app->page==APP_PAGE_PLANT_LIBRARY)
			app->plant_list_state=APP_PLANT_DATA_ERROR;
		if(app->page==APP_PAGE_PLANT_PROFILE)
			app->plant_detail_state=APP_PLANT_DATA_ERROR;
		AppState_CopyText(app->plant_error,sizeof(app->plant_error),
		                  event->error);
		AppState_CopyText(app->last_error,sizeof(app->last_error),
		                  event->error);
		app->plant_ui_revision++;
		app->ui_dirty=1;
		printf("[PLANT] ERROR %s\r\n",event->error);
	}
	else if(event->type==PROTOCOL_EVENT_ASSET_BEGIN)
	{
		if(event->transfer_id!=AssetService_GetTransferId())
			printf("[ASSET] IGNORE STALE BEGIN id=%u\r\n",
			       (unsigned int)event->transfer_id);
		else if(!AssetService_Begin(event->transfer_id,event->species_id,
		                       event->asset_state,event->asset_width,
		                       event->asset_height,event->byte_size,
		                       event->chunk_count,event->crc32))
			App_HandleAssetFailure(app,AssetService_GetError());
		else
			printf("[ASSET] BEGIN id=%u species=%s state=%s chunks=%u\r\n",
			       (unsigned int)event->transfer_id,event->species_id,
			       event->asset_state,(unsigned int)event->chunk_count);
	}
	else if(event->type==PROTOCOL_EVENT_ASSET_CHUNK)
	{
		if(event->transfer_id!=AssetService_GetTransferId())
		{
		}
		else if(!AssetService_AppendChunk(event->transfer_id,event->sequence,
		                             event->asset_hex))
			App_HandleAssetFailure(app,AssetService_GetError());
	}
	else if(event->type==PROTOCOL_EVENT_ASSET_END)
	{
		if(event->transfer_id!=AssetService_GetTransferId())
			printf("[ASSET] IGNORE STALE END id=%u\r\n",
			       (unsigned int)event->transfer_id);
		else if(AssetService_End(event->transfer_id,event->chunk_count,event->crc32))
		{
			AppState_CopyText(app->asset_status,sizeof(app->asset_status),"READY");
			AppState_CopyText(app->asset_species_id,
			                  sizeof(app->asset_species_id),
			                  AssetService_GetSpeciesId());
			AppState_CopyText(app->asset_state,sizeof(app->asset_state),
			                  AssetService_GetAssetState());
			AppState_CopyText(app->asset_error,sizeof(app->asset_error),"NONE");
			AppState_CopyText(app->last_error,sizeof(app->last_error),"NONE");
			app->asset_revision++;
			app->ui_dirty=1;
			printf("[ASSET] READY id=%u species=%s state=%s\r\n",
			       (unsigned int)event->transfer_id,app->asset_species_id,
			       app->asset_state);
		}
		else App_HandleAssetFailure(app,AssetService_GetError());
	}
	else if(event->type==PROTOCOL_EVENT_ASSET_ERROR)
	{
		if(event->transfer_id==AssetService_GetTransferId())
		{
			AssetService_Fail(event->transfer_id,event->error);
			App_HandleAssetFailure(app,event->error);
		}
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
	static ProtocolEvent event;
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
		app->plant_selection_pending=0;
		if(app->plant_list_state==APP_PLANT_DATA_LOADING)
			app->plant_list_state=APP_PLANT_DATA_ERROR;
		if(app->plant_detail_state==APP_PLANT_DATA_LOADING)
			app->plant_detail_state=APP_PLANT_DATA_ERROR;
		if(app->page==APP_PAGE_PLANT_LIBRARY ||
		   app->page==APP_PAGE_PLANT_PROFILE)
		{
			AppState_CopyText(app->plant_error,sizeof(app->plant_error),
			                  "GATEWAY LOST");
			app->plant_ui_revision++;
		}
		if(AssetService_GetState()==ASSET_TRANSFER_REQUESTING ||
		   AssetService_GetState()==ASSET_TRANSFER_RECEIVING)
		{
			AssetService_Cancel("GATEWAY LOST");
			AppState_CopyText(app->asset_status,sizeof(app->asset_status),
			                  "ERROR");
			AppState_CopyText(app->asset_error,sizeof(app->asset_error),
			                  "GATEWAY LOST");
			app->asset_revision++;
		}
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
	length=Protocol_BuildPlantAiRequest(frame,sizeof(frame),app->ai_request_id,
	                                   app->device_id,app->species_id,
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
	printf("[AI] REQUEST device=%s plant=%s T=%u H=%u L=%u %s\r\n",
	       app->device_id,app->species_id,
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

static void App_SetPlantRequestError(AppState *app,AppPlantDataState *state,
	                                  const char *error)
{
	*state=APP_PLANT_DATA_ERROR;
	app->plant_selection_pending=0;
	AppState_CopyText(app->plant_error,sizeof(app->plant_error),error);
	AppState_CopyText(app->last_error,sizeof(app->last_error),error);
	app->plant_ui_revision++;
	app->ui_dirty=1;
}

static void App_RequestPlantList(AppState *app)
{
	app->page=APP_PAGE_PLANT_LIBRARY;
	app->plant_list_state=APP_PLANT_DATA_LOADING;
	app->plant_list_expected=0;
	App_ClearPlantItems(app);
	AppState_CopyText(app->plant_error,sizeof(app->plant_error),"NONE");
	app->plant_ui_revision++;
	app->ui_dirty=1;

	if(!app->gateway_ready)
	{
		App_SetPlantRequestError(app,&app->plant_list_state,"GATEWAY OFFLINE");
		return;
	}
	if(!App_QueuePlantListRequest(app))
	{
		App_SetPlantRequestError(app,&app->plant_list_state,"TX QUEUE FULL");
		return;
	}
	printf("[PLANT] LIST REQUEST device=%s\r\n",app->device_id);
}

static void App_RequestPlantDetail(AppState *app,u8 index)
{
	u8 frame[WIFI_TCP_TX_BUFFER_SIZE];
	u16 length;

	if(index>=APP_PLANT_LIST_MAX || !app->plant_items[index].valid)return;
	app->page=APP_PAGE_PLANT_PROFILE;
	app->plant_detail_state=APP_PLANT_DATA_LOADING;
	app->plant_selection_pending=0;
	AppState_CopyText(app->plant_detail_species_id,
	                  sizeof(app->plant_detail_species_id),
	                  app->plant_items[index].species_id);
	AppState_CopyText(app->plant_detail_name,sizeof(app->plant_detail_name),
	                  app->plant_items[index].display_name);
	AppState_CopyText(app->plant_detail_source,
	                  sizeof(app->plant_detail_source),
	                  app->plant_items[index].source_type);
	AppState_CopyText(app->plant_error,sizeof(app->plant_error),"NONE");
	app->plant_ui_revision++;
	app->ui_dirty=1;

	if(!app->gateway_ready)
	{
		App_SetPlantRequestError(app,&app->plant_detail_state,"GATEWAY OFFLINE");
		return;
	}
	length=Protocol_BuildPlantDetailRequest(frame,sizeof(frame),
	                                        app->plant_detail_species_id);
	if(length==0 || !AppTx_Enqueue(frame,length))
	{
		App_SetPlantRequestError(app,&app->plant_detail_state,"TX QUEUE FULL");
		return;
	}
	printf("[PLANT] DETAIL REQUEST species=%s\r\n",
	       app->plant_detail_species_id);
}

static void App_SelectPlant(AppState *app)
{
	u8 frame[WIFI_TCP_TX_BUFFER_SIZE];
	u16 length;

	if(app->plant_detail_state!=APP_PLANT_DATA_READY ||
	   app->plant_selection_pending)return;
	if(strcmp(app->species_id,app->plant_detail_species_id)==0)
	{
		app->page=APP_PAGE_HOME;
		app->ui_dirty=1;
		return;
	}
	if(!app->gateway_ready)
	{
		App_SetPlantRequestError(app,&app->plant_detail_state,"GATEWAY OFFLINE");
		return;
	}
	length=Protocol_BuildPlantSelect(frame,sizeof(frame),app->device_id,
	                                app->plant_detail_species_id);
	if(length==0 || !AppTx_Enqueue(frame,length))
	{
		App_SetPlantRequestError(app,&app->plant_detail_state,"TX QUEUE FULL");
		return;
	}
	app->plant_selection_pending=1;
	AppState_CopyText(app->plant_error,sizeof(app->plant_error),"NONE");
	app->plant_ui_revision++;
	app->ui_dirty=1;
	printf("[PLANT] SELECT REQUEST device=%s species=%s\r\n",
	       app->device_id,app->plant_detail_species_id);
}

int main(void)
{
	static AppState app;
	AppKeyEvent key_event;
	AppTouchEvent touch_event;
	u8 wifi_ready;
	u16 ui_timer=0;
	u16 log_timer=200;

	SysTick_Init(72);
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
	USART1_Init(115200);
	AppState_Init(&app);
	AssetService_Init();
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

	printf("\r\n[BOOT] GreenMind firmware start device=%s plant=%s\r\n",
	       app.device_id,app.species_id);
	wifi_ready=WiFi_ConfigServer(&app);
	if(!wifi_ready)printf("[WIFI INIT] STOPPED - SEE FAILED STAGE\r\n");

	while(1)
	{
		WiFi_TCP_TxTask();
		AppTx_Task();
		AppTcp_Task(&app);
		if(wifi_ready)WiFi_StaTask(&app);
		SensorService_Task(&app);
		if(AssetService_Task())
			App_HandleAssetFailure(&app,AssetService_GetError());

		key_event=KeyService_Task();
		if(key_event==APP_KEY_EVENT_PAGE)
		{
			if((u8)app.page>=(u8)(APP_PAGE_COUNT-1))
				app.page=APP_PAGE_HOME;
			else app.page=(AppPage)((u8)app.page+1);
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
		else if(touch_event==APP_TOUCH_EVENT_PLANT_LIBRARY)
		{
			printf("[TOUCH] PLANT LIBRARY\r\n");
			App_RequestPlantList(&app);
		}
		else if(touch_event>=APP_TOUCH_EVENT_PLANT_ITEM_0 &&
		        touch_event<=APP_TOUCH_EVENT_PLANT_ITEM_5)
		{
			u8 plant_index=(u8)(touch_event-APP_TOUCH_EVENT_PLANT_ITEM_0);
			printf("[TOUCH] PLANT ITEM %u\r\n",(unsigned int)plant_index);
			App_RequestPlantDetail(&app,plant_index);
		}
		else if(touch_event==APP_TOUCH_EVENT_PLANT_BACK)
		{
			if(app.page==APP_PAGE_PLANT_PROFILE)
				app.page=APP_PAGE_PLANT_LIBRARY;
			else app.page=APP_PAGE_HOME;
			app.ui_dirty=1;
			printf("[TOUCH] PLANT BACK\r\n");
		}
		else if(touch_event==APP_TOUCH_EVENT_PLANT_REFRESH)
		{
			printf("[TOUCH] PLANT REFRESH\r\n");
			App_RequestPlantList(&app);
		}
		else if(touch_event==APP_TOUCH_EVENT_PLANT_USE)
		{
			printf("[TOUCH] PLANT USE\r\n");
			App_SelectPlant(&app);
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
			printf("[STATUS] T=%u H=%u L=%u DHT=%s WIFI=%s TCP=%s GW=%s AI=%s IMG=%s/%s\r\n",
			       (unsigned int)app.temperature,(unsigned int)app.humidity,
			       (unsigned int)app.light,app.dht_valid?"OK":"ERROR",
			       app.wifi_status,app.tcp_connected?"UP":"DOWN",
			       app.gateway_ready?"READY":"OFFLINE",
			       AppState_AiStateText(app.ai_state),
			       app.asset_status,app.asset_state);
			log_timer=200;
		}
		else log_timer--;

		delay_ms(APP_TASK_PERIOD_MS);
	}
}
