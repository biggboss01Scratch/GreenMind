#ifndef _APP_STATE_H
#define _APP_STATE_H

#include "system.h"

#define APP_STATE_TEXT_SMALL  16
#define APP_STATE_TEXT_MEDIUM 24
#define APP_PLANT_LIST_MAX     6
#define APP_PLANT_SOURCE_TEXT  8

typedef enum
{
	APP_PAGE_HOME=0,
	APP_PAGE_DETAIL,
	APP_PAGE_AI,
	APP_PAGE_SYSTEM,
	APP_PAGE_COUNT,
	APP_PAGE_PLANT_LIBRARY,
	APP_PAGE_PLANT_PROFILE
} AppPage;

typedef enum
{
	APP_AI_IDLE=0,
	APP_AI_SENDING,
	APP_AI_PREPARING,
	APP_AI_THINKING,
	APP_AI_VALIDATING,
	APP_AI_DONE,
	APP_AI_TIMEOUT,
	APP_AI_ERROR
} AppAiState;

typedef enum
{
	APP_PLANT_DATA_IDLE=0,
	APP_PLANT_DATA_LOADING,
	APP_PLANT_DATA_READY,
	APP_PLANT_DATA_ERROR
} AppPlantDataState;

typedef struct
{
	u8 valid;
	char species_id[APP_STATE_TEXT_SMALL];
	char display_name[APP_STATE_TEXT_SMALL];
	char source_type[APP_PLANT_SOURCE_TEXT];
} AppPlantListItem;

typedef struct
{
	u8 temperature;
	u8 humidity;
	u8 light;
	u8 dht_valid;
	u8 light_valid;
	u8 dht_fail_count;

	u8 wifi_connected;
	u8 tcp_connected;
	u8 gateway_ready;
	char wifi_status[APP_STATE_TEXT_MEDIUM];
	char sta_ip[16];
	char device_id[APP_STATE_TEXT_SMALL];
	char species_id[APP_STATE_TEXT_SMALL];
	char plant_name[APP_STATE_TEXT_SMALL];
	AppPlantDataState plant_list_state;
	AppPlantDataState plant_detail_state;
	u8 plant_list_count;
	u8 plant_list_expected;
	AppPlantListItem plant_items[APP_PLANT_LIST_MAX];
	char plant_detail_species_id[APP_STATE_TEXT_SMALL];
	char plant_detail_name[APP_STATE_TEXT_SMALL];
	char plant_detail_source[APP_PLANT_SOURCE_TEXT];
	u8 plant_temp_min;
	u8 plant_temp_max;
	u8 plant_humidity_min;
	u8 plant_humidity_max;
	u8 plant_light_min;
	u8 plant_light_max;
	u8 plant_selection_pending;
	u8 plant_ui_revision;
	char plant_error[APP_STATE_TEXT_MEDIUM];
	u16 asset_request_id;
	u8 asset_revision;
	u8 asset_retry_count;
	char asset_status[APP_STATE_TEXT_SMALL];
	char asset_species_id[APP_STATE_TEXT_SMALL];
	char asset_state[APP_STATE_TEXT_SMALL];
	char asset_error[APP_STATE_TEXT_MEDIUM];

	AppPage page;
	AppAiState ai_state;
	u16 ai_request_id;
	u16 ai_wait_ticks;
	char ai_status[APP_STATE_TEXT_SMALL];
	char ai_issue[APP_STATE_TEXT_MEDIUM];
	char ai_watering[APP_STATE_TEXT_MEDIUM];
	char ai_advice[APP_STATE_TEXT_MEDIUM];
	char last_error[APP_STATE_TEXT_MEDIUM];

	u8 ui_dirty;
} AppState;

void AppState_Init(AppState *state);
void AppState_CopyText(char *destination,u16 size,const char *source);
const char *AppState_LightLevel(u8 light);
const char *AppState_AiStateText(AppAiState state);

#endif
