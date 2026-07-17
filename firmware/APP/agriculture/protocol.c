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
	event->item_index=0;
	event->item_count=0;
	event->species_id[0]='\0';
	event->display_name[0]='\0';
	event->source_type[0]='\0';
	event->temp_min=0;
	event->temp_max=0;
	event->humidity_min=0;
	event->humidity_max=0;
	event->light_min=0;
	event->light_max=0;
	event->transfer_id=0;
	event->sequence=0;
	event->chunk_count=0;
	event->byte_size=0;
	event->crc32=0;
	event->asset_width=0;
	event->asset_height=0;
	event->asset_state[0]='\0';
	event->asset_hex[0]='\0';
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

static u8 Protocol_ParseU8(const char *text,u8 *value)
{
	u16 parsed;
	if(!Protocol_ParseU16(text,&parsed) || parsed>255)return 0;
	*value=(u8)parsed;
	return 1;
}

static u8 Protocol_ParseHexU32(const char *text,u32 *value)
{
	u8 i=0;
	u8 digit;
	u32 result=0;
	if(text[0]=='\0')return 0;
	while(text[i]!='\0')
	{
		if(i>=8)return 0;
		if(text[i]>='0' && text[i]<='9')digit=(u8)(text[i]-'0');
		else if(text[i]>='A' && text[i]<='F')
			digit=(u8)(text[i]-'A'+10);
		else if(text[i]>='a' && text[i]<='f')
			digit=(u8)(text[i]-'a'+10);
		else return 0;
		result=(result<<4)|digit;
		i++;
	}
	if(i!=8)return 0;
	*value=result;
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
	static ProtocolEvent event;
	Protocol_ClearEvent(&event);
	event.type=PROTOCOL_EVENT_BAD_FRAME;
	Protocol_Copy(event.error,sizeof(event.error),error);
	Protocol_QueueEvent(&event);
}

static void Protocol_ParseFrame(char *frame)
{
	char *fields[12];
	u8 count;
	static ProtocolEvent event;
	u16 request_id=0;

	count=Protocol_Split(frame,fields,12);
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
	else if(strcmp(fields[1],"PLANT_LIST_BEGIN")==0 && count==5 &&
	        Protocol_ParseU8(fields[2],&event.item_count))
	{
		event.type=PROTOCOL_EVENT_PLANT_LIST_BEGIN;
		Protocol_Copy(event.species_id,sizeof(event.species_id),fields[3]);
		Protocol_Copy(event.display_name,sizeof(event.display_name),fields[4]);
	}
	else if(strcmp(fields[1],"PLANT_ITEM")==0 && count==6 &&
	        Protocol_ParseU8(fields[2],&event.item_index))
	{
		event.type=PROTOCOL_EVENT_PLANT_ITEM;
		Protocol_Copy(event.species_id,sizeof(event.species_id),fields[3]);
		Protocol_Copy(event.display_name,sizeof(event.display_name),fields[4]);
		Protocol_Copy(event.source_type,sizeof(event.source_type),fields[5]);
	}
	else if(strcmp(fields[1],"PLANT_LIST_END")==0 && count==3 &&
	        Protocol_ParseU8(fields[2],&event.item_count))
		event.type=PROTOCOL_EVENT_PLANT_LIST_END;
	else if(strcmp(fields[1],"PLANT_DETAIL")==0 && count==11 &&
	        Protocol_ParseU8(fields[4],&event.temp_min) &&
	        Protocol_ParseU8(fields[5],&event.temp_max) &&
	        Protocol_ParseU8(fields[6],&event.humidity_min) &&
	        Protocol_ParseU8(fields[7],&event.humidity_max) &&
	        Protocol_ParseU8(fields[8],&event.light_min) &&
	        Protocol_ParseU8(fields[9],&event.light_max))
	{
		event.type=PROTOCOL_EVENT_PLANT_DETAIL;
		Protocol_Copy(event.species_id,sizeof(event.species_id),fields[2]);
		Protocol_Copy(event.display_name,sizeof(event.display_name),fields[3]);
		Protocol_Copy(event.source_type,sizeof(event.source_type),fields[10]);
	}
	else if(strcmp(fields[1],"PLANT_SELECTED")==0 && count==4)
	{
		event.type=PROTOCOL_EVENT_PLANT_SELECTED;
		Protocol_Copy(event.species_id,sizeof(event.species_id),fields[2]);
		Protocol_Copy(event.display_name,sizeof(event.display_name),fields[3]);
	}
	else if(strcmp(fields[1],"PLANT_ERROR")==0 && count==3)
	{
		event.type=PROTOCOL_EVENT_PLANT_ERROR;
		Protocol_Copy(event.error,sizeof(event.error),fields[2]);
	}
	else if(strcmp(fields[1],"ASSET_BEGIN")==0 && count==10 &&
	        Protocol_ParseU16(fields[2],&event.transfer_id) &&
	        Protocol_ParseU8(fields[5],&event.asset_width) &&
	        Protocol_ParseU8(fields[6],&event.asset_height) &&
	        Protocol_ParseU16(fields[7],&event.byte_size) &&
	        Protocol_ParseU16(fields[8],&event.chunk_count) &&
	        Protocol_ParseHexU32(fields[9],&event.crc32))
	{
		event.type=PROTOCOL_EVENT_ASSET_BEGIN;
		Protocol_Copy(event.species_id,sizeof(event.species_id),fields[3]);
		Protocol_Copy(event.asset_state,sizeof(event.asset_state),fields[4]);
	}
	else if(strcmp(fields[1],"ASSET_CHUNK")==0 && count==5 &&
	        Protocol_ParseU16(fields[2],&event.transfer_id) &&
	        Protocol_ParseU16(fields[3],&event.sequence))
	{
		event.type=PROTOCOL_EVENT_ASSET_CHUNK;
		Protocol_Copy(event.asset_hex,sizeof(event.asset_hex),fields[4]);
	}
	else if(strcmp(fields[1],"ASSET_END")==0 && count==5 &&
	        Protocol_ParseU16(fields[2],&event.transfer_id) &&
	        Protocol_ParseU16(fields[3],&event.chunk_count) &&
	        Protocol_ParseHexU32(fields[4],&event.crc32))
		event.type=PROTOCOL_EVENT_ASSET_END;
	else if(strcmp(fields[1],"ASSET_ERROR")==0 && count==4 &&
	        Protocol_ParseU16(fields[2],&event.transfer_id))
	{
		event.type=PROTOCOL_EVENT_ASSET_ERROR;
		Protocol_Copy(event.error,sizeof(event.error),fields[3]);
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
	if(length<=0 || (u16)length>size ||
	   (u16)length>APP_PROTOCOL_FRAME_MAX)return 0;
	return (u16)length;
}

u16 Protocol_BuildPlantAiRequest(u8 *destination,u16 size,u16 request_id,
	                              const char *device_id,const char *species_id,
	                              u8 temperature,u8 humidity,u8 light,
	                              const char *light_level)
{
	int length;
	length=sprintf((char *)destination,
	               "V1|PLANT_AI_REQ|%u|%s|%s|%u|%u|%u|%s\r\n",
	               (unsigned int)request_id,device_id,species_id,
	               (unsigned int)temperature,(unsigned int)humidity,
	               (unsigned int)light,light_level);
	if(length<=0 || (u16)length>size ||
	   (u16)length>APP_PROTOCOL_FRAME_MAX)return 0;
	return (u16)length;
}

u16 Protocol_BuildPlantListRequest(u8 *destination,u16 size,
	                                const char *device_id)
{
	int length;
	length=sprintf((char *)destination,"V1|PLANT_LIST_REQ|%s\r\n",device_id);
	if(length<=0 || (u16)length>size ||
	   (u16)length>APP_PROTOCOL_FRAME_MAX)return 0;
	return (u16)length;
}

u16 Protocol_BuildPlantDetailRequest(u8 *destination,u16 size,
	                                  const char *species_id)
{
	int length;
	length=sprintf((char *)destination,"V1|PLANT_DETAIL_REQ|%s\r\n",
	               species_id);
	if(length<=0 || (u16)length>size ||
	   (u16)length>APP_PROTOCOL_FRAME_MAX)return 0;
	return (u16)length;
}

u16 Protocol_BuildPlantSelect(u8 *destination,u16 size,
	                           const char *device_id,const char *species_id)
{
	int length;
	length=sprintf((char *)destination,"V1|PLANT_SELECT|%s|%s\r\n",
	               device_id,species_id);
	if(length<=0 || (u16)length>size ||
	   (u16)length>APP_PROTOCOL_FRAME_MAX)return 0;
	return (u16)length;
}

u16 Protocol_BuildAssetRequest(u8 *destination,u16 size,u16 transfer_id,
	                            const char *species_id,const char *state)
{
	int length;
	length=sprintf((char *)destination,"V1|ASSET_REQ|%u|%s|%s\r\n",
	               (unsigned int)transfer_id,species_id,state);
	if(length<=0 || (u16)length>size ||
	   (u16)length>APP_PROTOCOL_FRAME_MAX)return 0;
	return (u16)length;
}
