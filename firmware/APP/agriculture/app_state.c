#include "app_state.h"
#include "app_config.h"

void AppState_CopyText(char *destination,u16 size,const char *source)
{
	u16 i=0;
	if(size==0)return;
	while(i<size-1 && source[i]!='\0')
	{
		destination[i]=source[i];
		i++;
	}
	destination[i]='\0';
}

void AppState_Init(AppState *state)
{
	state->temperature=0;
	state->humidity=0;
	state->light=0;
	state->dht_valid=0;
	state->light_valid=0;
	state->dht_fail_count=0;
	state->wifi_connected=0;
	state->tcp_connected=0;
	state->gateway_ready=0;
	AppState_CopyText(state->wifi_status,sizeof(state->wifi_status),"STARTING");
	AppState_CopyText(state->sta_ip,sizeof(state->sta_ip),"-");
	state->page=APP_PAGE_HOME;
	state->ai_state=APP_AI_IDLE;
	state->ai_request_id=0;
	state->ai_wait_ticks=0;
	AppState_CopyText(state->ai_status,sizeof(state->ai_status),"IDLE");
	AppState_CopyText(state->ai_issue,sizeof(state->ai_issue),"-");
	AppState_CopyText(state->ai_watering,sizeof(state->ai_watering),"-");
	AppState_CopyText(state->ai_advice,sizeof(state->ai_advice),"-");
	AppState_CopyText(state->last_error,sizeof(state->last_error),"NONE");
	state->ui_dirty=1;
}

const char *AppState_LightLevel(u8 light)
{
	if(light<=APP_LIGHT_DARK_MAX)return "DARK";
	if(light<=APP_LIGHT_NORMAL_MAX)return "NORMAL";
	return "STRONG";
}

const char *AppState_AiStateText(AppAiState state)
{
	switch(state)
	{
		case APP_AI_SENDING:return "SENDING";
		case APP_AI_PREPARING:return "PREPARING";
		case APP_AI_THINKING:return "THINKING...";
		case APP_AI_VALIDATING:return "VALIDATING";
		case APP_AI_DONE:return "DONE";
		case APP_AI_TIMEOUT:return "TIMEOUT";
		case APP_AI_ERROR:return "ERROR";
		default:return "IDLE";
	}
}
