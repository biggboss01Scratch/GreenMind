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
	u8 i;
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
	AppState_CopyText(state->device_id,sizeof(state->device_id),APP_DEVICE_ID);
	AppState_CopyText(state->species_id,sizeof(state->species_id),
	                  APP_DEFAULT_SPECIES_ID);
	AppState_CopyText(state->plant_name,sizeof(state->plant_name),
	                  APP_DEFAULT_PLANT_NAME);
	state->plant_list_state=APP_PLANT_DATA_IDLE;
	state->plant_detail_state=APP_PLANT_DATA_IDLE;
	state->plant_list_count=0;
	state->plant_list_expected=0;
	for(i=0;i<APP_PLANT_LIST_MAX;i++)
	{
		state->plant_items[i].valid=0;
		state->plant_items[i].species_id[0]='\0';
		state->plant_items[i].display_name[0]='\0';
		state->plant_items[i].source_type[0]='\0';
	}
	state->plant_detail_species_id[0]='\0';
	state->plant_detail_name[0]='\0';
	state->plant_detail_source[0]='\0';
	state->plant_temp_min=0;
	state->plant_temp_max=0;
	state->plant_humidity_min=0;
	state->plant_humidity_max=0;
	state->plant_light_min=0;
	state->plant_light_max=0;
	state->plant_selection_pending=0;
	state->plant_ui_revision=0;
	AppState_CopyText(state->plant_error,sizeof(state->plant_error),"NONE");
	state->asset_request_id=0;
	state->asset_revision=0;
	state->asset_retry_count=0;
	AppState_CopyText(state->asset_status,sizeof(state->asset_status),"BUILTIN");
	AppState_CopyText(state->asset_species_id,
	                  sizeof(state->asset_species_id),APP_DEFAULT_SPECIES_ID);
	AppState_CopyText(state->asset_state,sizeof(state->asset_state),"NORMAL");
	AppState_CopyText(state->asset_error,sizeof(state->asset_error),"NONE");
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
