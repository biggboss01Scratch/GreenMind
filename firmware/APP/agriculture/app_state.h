#ifndef _APP_STATE_H
#define _APP_STATE_H

#include "system.h"

#define APP_STATE_TEXT_SMALL  16
#define APP_STATE_TEXT_MEDIUM 24

typedef enum
{
	APP_PAGE_HOME=0,
	APP_PAGE_DETAIL,
	APP_PAGE_AI,
	APP_PAGE_SYSTEM,
	APP_PAGE_COUNT
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
