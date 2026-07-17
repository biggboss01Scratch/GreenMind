#ifndef _KEY_SERVICE_H
#define _KEY_SERVICE_H

#include "system.h"

typedef enum
{
	APP_KEY_EVENT_NONE=0,
	APP_KEY_EVENT_PAGE,
	APP_KEY_EVENT_AI_REQUEST
} AppKeyEvent;

void KeyService_Init(void);
AppKeyEvent KeyService_Task(void);

#endif
