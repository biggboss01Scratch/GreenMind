#ifndef _TOUCH_SERVICE_H
#define _TOUCH_SERVICE_H

#include "app_state.h"

typedef enum
{
	APP_TOUCH_EVENT_NONE=0,
	APP_TOUCH_EVENT_HOME,
	APP_TOUCH_EVENT_DETAIL,
	APP_TOUCH_EVENT_AI_PAGE,
	APP_TOUCH_EVENT_SYSTEM,
	APP_TOUCH_EVENT_AI_REQUEST,
	APP_TOUCH_EVENT_NETWORK_RETRY,
	APP_TOUCH_EVENT_CALIBRATE
} AppTouchEvent;

void TouchService_Init(void);
AppTouchEvent TouchService_Task(AppPage page);
void TouchService_Adjust(void);

#endif
