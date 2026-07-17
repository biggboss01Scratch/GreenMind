#include "protocol.h"
#include <stdio.h>
#include <string.h>

static char g_line[APP_PROTOCOL_FRAME_MAX+1];
static u16 g_line_length=0;
static u8 g_discarding=0;
static ProtocolEvent g_events[APP_PROTOCOL_EVENT_QUEUE_SIZE];
static u8 g_event_head=0;
static u8 g_event_tail=0;
static u16 g_event_dropped=0;

static void Protocol_Copy(char *destination,u16 size,const char *source)
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

static void Protocol_ClearEvent(ProtocolEvent *event)
{
	event->type=PROTOCOL_EVENT_NONE;
	event->request_id=0;
	event->status[0]='\0';
	event->issue[0]='\0';
	event->watering[0]='\0';
	event->advice[0]='\0';
	event->error[0]='\0';
}

static void Protocol_QueueEvent(const ProtocolEvent *event)
{
	u8 next=(u8)((g_event_head+1)%APP_PROTOCOL_EVENT_QUEUE_SIZE);
	if(next==g_event_tail)
	{
		if(g_event_dropped<65535)g_event_dropped++;
		return;
	}
	g_events[g_event_head]=*event;
	g_event_head=next;
}

static u8 Protocol_ParseU16(const char *text,u16 *value)
{
	u32 result=0;
	u16 i=0;
	if(text[0]=='\0')return 0;
	while(text[i]!='\0')
	{
		if(text[i]<'0' || text[i]>'9')return 0;
		result=result*10+(u32)(text[i]-'0');
		if(result>65535)return 0;
		i++;
	}
	*value=(u16)result;
	return 1;
}

static u8 Protocol_Split(char *frame,char **fields,u8 max_fields)
{
	u8 count=0;
	char *cursor=frame;
	if(max_fields==0)return 0;
	fields[count++]=cursor;
	while(*cursor!='\0')
	{
		if(*cursor=='|')
		{
			*cursor='\0';
			if(count>=max_fields)return count;
			fields[count++]=cursor+1;
		}
		cursor++;
	}
	return count;
}

static void Protocol_BadFrame(const char *error)
{
	ProtocolEvent event;
	Protocol_ClearEvent(&event);
	event.type=PROTOCOL_EVENT_BAD_FRAME;
	Protocol_Copy(event.error,sizeof(event.error),error);
	Protocol_QueueEvent(&event);
}

static void Protocol_ParseFrame(char *frame)
{
	char *fields[8];
	u8 count;
	ProtocolEvent event;
	u16 request_id=0;

	count=Protocol_Split(frame,fields,8);
	if(count<2 || strcmp(fields[0],"V1")!=0)
	{
		Protocol_BadFrame("BAD_VERSION");
		return;
	}

	Protocol_ClearEvent(&event);
	if(strcmp(fields[1],"HELLO_ACK")==0 && count==4 &&
	   strcmp(fields[2],"GATEWAY")==0 && strcmp(fields[3],"READY")==0)
		event.type=PROTOCOL_EVENT_HELLO_ACK;
	else if(strcmp(fields[1],"PING")==0 && count==2)
		event.type=PROTOCOL_EVENT_PING;
	else if(strcmp(fields[1],"PONG")==0 && count==2)
		event.type=PROTOCOL_EVENT_PONG;
	else if(strcmp(fields[1],"ACK")==0 && count==4 &&
	        Protocol_ParseU16(fields[2],&request_id))
	{
		event.type=PROTOCOL_EVENT_ACK;
		event.request_id=request_id;
		Protocol_Copy(event.status,sizeof(event.status),fields[3]);
	}
	else if(strcmp(fields[1],"AI_STAGE")==0 && count==4 &&
	        Protocol_ParseU16(fields[2],&request_id))
	{
		event.type=PROTOCOL_EVENT_AI_STAGE;
		event.request_id=request_id;
		Protocol_Copy(event.status,sizeof(event.status),fields[3]);
	}
	else if(strcmp(fields[1],"AI_RESULT")==0 && count==7 &&
	        Protocol_ParseU16(fields[2],&request_id))
	{
		event.type=PROTOCOL_EVENT_AI_RESULT;
		event.request_id=request_id;
		Protocol_Copy(event.status,sizeof(event.status),fields[3]);
		Protocol_Copy(event.issue,sizeof(event.issue),fields[4]);
		Protocol_Copy(event.watering,sizeof(event.watering),fields[5]);
		Protocol_Copy(event.advice,sizeof(event.advice),fields[6]);
	}
	else if(strcmp(fields[1],"ERROR")==0 && count==4 &&
	        Protocol_ParseU16(fields[2],&request_id))
	{
		event.type=PROTOCOL_EVENT_ERROR;
		event.request_id=request_id;
		Protocol_Copy(event.error,sizeof(event.error),fields[3]);
	}
	else
	{
		Protocol_BadFrame("BAD_FRAME");
		return;
	}
	Protocol_QueueEvent(&event);
}

void Protocol_Init(void)
{
	g_line_length=0;
	g_discarding=0;
	g_event_head=0;
	g_event_tail=0;
	g_event_dropped=0;
}

void Protocol_InputBytes(const u8 *data,u16 length)
{
	u16 i;
	for(i=0;i<length;i++)
	{
		if(data[i]=='\n')
		{
			if(g_discarding)
			{
				g_discarding=0;
				g_line_length=0;
				Protocol_BadFrame("FRAME_TOO_LONG");
			}
			else if(g_line_length>0)
			{
				if(g_line[g_line_length-1]=='\r')g_line_length--;
				g_line[g_line_length]='\0';
				if(g_line_length>0)Protocol_ParseFrame(g_line);
				g_line_length=0;
			}
		}
		else if(!g_discarding)
		{
			if(g_line_length<APP_PROTOCOL_FRAME_MAX)
				g_line[g_line_length++]=(char)data[i];
			else
			{
				g_discarding=1;
				g_line_length=0;
			}
		}
	}
}

u8 Protocol_GetEvent(ProtocolEvent *event)
{
	if(g_event_tail==g_event_head)return 0;
	*event=g_events[g_event_tail];
	g_event_tail=(u8)((g_event_tail+1)%APP_PROTOCOL_EVENT_QUEUE_SIZE);
	return 1;
}

u16 Protocol_GetDroppedEventCount(void)
{
	return g_event_dropped;
}

u16 Protocol_BuildAiRequest(u8 *destination,u16 size,u16 request_id,
	                         u8 temperature,u8 humidity,u8 light,
	                         const char *light_level)
{
	int length;
	length=sprintf((char *)destination,"V1|AI_REQ|%u|%u|%u|%u|%s\r\n",
	               (unsigned int)request_id,(unsigned int)temperature,
	               (unsigned int)humidity,(unsigned int)light,light_level);
	if(length<=0 || (u16)length>size)return 0;
	return (u16)length;
}
