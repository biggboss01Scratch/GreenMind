#include "ai_text_service.h"
#include "app_config.h"
#include <string.h>

#define AI_TEXT_ERROR_SIZE 24
#define AI_TEXT_CHUNK_BYTES 32
#define AI_TEXT_CHUNK_HEX_MAX (AI_TEXT_CHUNK_BYTES*2)
#define AI_TEXT_CHUNK_COUNT_MAX \
	((APP_AI_DIALOG_MAX_BYTES+AI_TEXT_CHUNK_BYTES-1)/AI_TEXT_CHUNK_BYTES)

static AiTextTransferState g_state=AI_TEXT_IDLE;
static u16 g_request_id=0;
static u16 g_expected_bytes=0;
static u16 g_expected_chunks=0;
static u16 g_next_sequence=0;
static u16 g_received_bytes=0;
static u32 g_expected_crc=0;
static u32 g_running_crc=0xFFFFFFFFUL;
static u16 g_timeout_ticks=0;
static char g_error[AI_TEXT_ERROR_SIZE];
static u8 g_text[APP_AI_DIALOG_MAX_BYTES+1];

static void AiTextService_Copy(char *destination,u16 size,const char *source)
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

static void AiTextService_SetError(const char *error)
{
	g_state=AI_TEXT_ERROR;
	g_timeout_ticks=0;
	g_text[0]='\0';
	g_received_bytes=0;
	AiTextService_Copy(g_error,sizeof(g_error),error);
}

static s8 AiTextService_HexNibble(char value)
{
	if(value>='0' && value<='9')return (s8)(value-'0');
	if(value>='A' && value<='F')return (s8)(value-'A'+10);
	if(value>='a' && value<='f')return (s8)(value-'a'+10);
	return -1;
}

static void AiTextService_CrcByte(u8 value)
{
	u8 bit;
	g_running_crc^=value;
	for(bit=0;bit<8;bit++)
	{
		if(g_running_crc&1UL)
			g_running_crc=(g_running_crc>>1)^0xEDB88320UL;
		else g_running_crc>>=1;
	}
}

static u8 AiTextService_ValidUtf8(const u8 *text,u16 length)
{
	u16 i=0;
	u8 first;
	while(i<length)
	{
		first=text[i];
		if(first==0)return 0;
		if(first<0x80)
		{
			if(first<0x20 && first!='\t')return 0;
			i++;
		}
		else if((first&0xE0)==0xC0)
		{
			if(i+1>=length || (text[i+1]&0xC0)!=0x80 ||
			   first<0xC2)return 0;
			i=(u16)(i+2);
		}
		else if((first&0xF0)==0xE0)
		{
			if(i+2>=length || (text[i+1]&0xC0)!=0x80 ||
			   (text[i+2]&0xC0)!=0x80)return 0;
			if(first==0xE0 && text[i+1]<0xA0)return 0;
			if(first==0xED && text[i+1]>=0xA0)return 0;
			i=(u16)(i+3);
		}
		else return 0;
	}
	return 1;
}

void AiTextService_Init(void)
{
	g_state=AI_TEXT_IDLE;
	g_request_id=0;
	g_expected_bytes=0;
	g_expected_chunks=0;
	g_next_sequence=0;
	g_received_bytes=0;
	g_expected_crc=0;
	g_running_crc=0xFFFFFFFFUL;
	g_timeout_ticks=0;
	g_text[0]='\0';
	AiTextService_Copy(g_error,sizeof(g_error),"NONE");
}

void AiTextService_StartRequest(u16 request_id)
{
	g_state=AI_TEXT_REQUESTING;
	g_request_id=request_id;
	g_expected_bytes=0;
	g_expected_chunks=0;
	g_next_sequence=0;
	g_received_bytes=0;
	g_expected_crc=0;
	g_running_crc=0xFFFFFFFFUL;
	g_timeout_ticks=0;
	g_text[0]='\0';
	AiTextService_Copy(g_error,sizeof(g_error),"NONE");
}

u8 AiTextService_Begin(u16 request_id,u16 byte_size,
	                   u16 chunk_count,u32 crc32)
{
	if(g_state!=AI_TEXT_REQUESTING || request_id!=g_request_id)
	{
		AiTextService_SetError("TEXT ID ERROR");
		return 0;
	}
	if(byte_size==0 || byte_size>APP_AI_DIALOG_MAX_BYTES ||
	   chunk_count==0 || chunk_count>AI_TEXT_CHUNK_COUNT_MAX)
	{
		AiTextService_SetError("TEXT SIZE ERROR");
		return 0;
	}
	g_state=AI_TEXT_RECEIVING;
	g_expected_bytes=byte_size;
	g_expected_chunks=chunk_count;
	g_next_sequence=0;
	g_received_bytes=0;
	g_expected_crc=crc32;
	g_running_crc=0xFFFFFFFFUL;
	g_timeout_ticks=APP_AI_DIALOG_TIMEOUT_TICKS;
	g_text[0]='\0';
	return 1;
}

u8 AiTextService_AppendChunk(u16 request_id,u16 sequence,
	                         const char *hex_payload)
{
	u16 length;
	u16 i;
	s8 high;
	s8 low;
	u8 value;

	if(g_state!=AI_TEXT_RECEIVING || request_id!=g_request_id)
	{
		AiTextService_SetError("TEXT ID ERROR");
		return 0;
	}
	if(sequence!=g_next_sequence)
	{
		AiTextService_SetError("TEXT SEQ ERROR");
		return 0;
	}
	length=(u16)strlen(hex_payload);
	if(length==0 || length>AI_TEXT_CHUNK_HEX_MAX || (length&1)!=0)
	{
		AiTextService_SetError("TEXT HEX ERROR");
		return 0;
	}
	if((u32)g_received_bytes+(u32)(length/2)>g_expected_bytes)
	{
		AiTextService_SetError("TEXT OVERFLOW");
		return 0;
	}

	for(i=0;i<length;i+=2)
	{
		high=AiTextService_HexNibble(hex_payload[i]);
		low=AiTextService_HexNibble(hex_payload[i+1]);
		if(high<0 || low<0)
		{
			AiTextService_SetError("TEXT HEX ERROR");
			return 0;
		}
		value=(u8)(((u8)high<<4)|(u8)low);
		if(value==0)
		{
			AiTextService_SetError("TEXT NUL ERROR");
			return 0;
		}
		g_text[g_received_bytes++]=value;
		AiTextService_CrcByte(value);
	}
	g_next_sequence++;
	g_timeout_ticks=APP_AI_DIALOG_TIMEOUT_TICKS;
	return 1;
}

u8 AiTextService_End(u16 request_id,u16 chunk_count,u32 crc32)
{
	u32 final_crc;
	if(g_state!=AI_TEXT_RECEIVING || request_id!=g_request_id)
	{
		AiTextService_SetError("TEXT ID ERROR");
		return 0;
	}
	final_crc=g_running_crc^0xFFFFFFFFUL;
	if(chunk_count!=g_expected_chunks || g_next_sequence!=g_expected_chunks ||
	   g_received_bytes!=g_expected_bytes || crc32!=g_expected_crc ||
	   final_crc!=g_expected_crc)
	{
		AiTextService_SetError("TEXT CRC ERROR");
		return 0;
	}
	if(!AiTextService_ValidUtf8(g_text,g_received_bytes))
	{
		AiTextService_SetError("TEXT UTF8 ERROR");
		return 0;
	}
	g_text[g_received_bytes]='\0';
	g_state=AI_TEXT_READY;
	g_timeout_ticks=0;
	AiTextService_Copy(g_error,sizeof(g_error),"NONE");
	return 1;
}

void AiTextService_Fail(u16 request_id,const char *error)
{
	if(request_id!=0 && request_id!=g_request_id)return;
	AiTextService_SetError(error);
}

u8 AiTextService_Task(void)
{
	if(g_state!=AI_TEXT_RECEIVING)return 0;
	if(g_timeout_ticks>0)g_timeout_ticks--;
	if(g_timeout_ticks==0)
	{
		AiTextService_SetError("TEXT TIMEOUT");
		return 1;
	}
	return 0;
}

void AiTextService_Cancel(const char *error)
{
	AiTextService_SetError(error);
}

AiTextTransferState AiTextService_GetState(void)
{
	return g_state;
}

u16 AiTextService_GetRequestId(void)
{
	return g_request_id;
}

const u8 *AiTextService_GetText(void)
{
	return g_text;
}

u16 AiTextService_GetLength(void)
{
	return g_received_bytes;
}

const char *AiTextService_GetError(void)
{
	return g_error;
}
