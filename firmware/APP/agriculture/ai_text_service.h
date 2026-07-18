#ifndef _AI_TEXT_SERVICE_H
#define _AI_TEXT_SERVICE_H

#include "system.h"

typedef enum
{
	AI_TEXT_IDLE=0,
	AI_TEXT_REQUESTING,
	AI_TEXT_RECEIVING,
	AI_TEXT_READY,
	AI_TEXT_ERROR
} AiTextTransferState;

void AiTextService_Init(void);
void AiTextService_StartRequest(u16 request_id);
u8 AiTextService_Begin(u16 request_id,u16 byte_size,
	                   u16 chunk_count,u32 crc32);
u8 AiTextService_AppendChunk(u16 request_id,u16 sequence,
	                         const char *hex_payload);
u8 AiTextService_End(u16 request_id,u16 chunk_count,u32 crc32);
void AiTextService_Fail(u16 request_id,const char *error);
u8 AiTextService_Task(void);
void AiTextService_Cancel(const char *error);

AiTextTransferState AiTextService_GetState(void);
u16 AiTextService_GetRequestId(void);
const u8 *AiTextService_GetText(void);
u16 AiTextService_GetLength(void);
const char *AiTextService_GetError(void);

#endif
