#ifndef _AGRI_PROTOCOL_H
#define _AGRI_PROTOCOL_H

#include "system.h"
#include "app_config.h"

typedef enum
{
	PROTOCOL_EVENT_NONE=0,
	PROTOCOL_EVENT_HELLO_ACK,
	PROTOCOL_EVENT_PING,
	PROTOCOL_EVENT_PONG,
	PROTOCOL_EVENT_ACK,
	PROTOCOL_EVENT_AI_STAGE,
	PROTOCOL_EVENT_AI_RESULT,
	PROTOCOL_EVENT_ERROR,
	PROTOCOL_EVENT_BAD_FRAME
} ProtocolEventType;

typedef struct
{
	ProtocolEventType type;
	u16 request_id;
	char status[16];
	char issue[24];
	char watering[24];
	char advice[24];
	char error[24];
} ProtocolEvent;

void Protocol_Init(void);
void Protocol_InputBytes(const u8 *data,u16 length);
u8 Protocol_GetEvent(ProtocolEvent *event);
u16 Protocol_GetDroppedEventCount(void);
u16 Protocol_BuildAiRequest(u8 *destination,u16 size,u16 request_id,
	                         u8 temperature,u8 humidity,u8 light,
	                         const char *light_level);

#endif
