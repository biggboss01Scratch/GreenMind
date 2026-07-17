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
	PROTOCOL_EVENT_PLANT_LIST_BEGIN,
	PROTOCOL_EVENT_PLANT_ITEM,
	PROTOCOL_EVENT_PLANT_LIST_END,
	PROTOCOL_EVENT_PLANT_DETAIL,
	PROTOCOL_EVENT_PLANT_SELECTED,
	PROTOCOL_EVENT_PLANT_ERROR,
	PROTOCOL_EVENT_ASSET_BEGIN,
	PROTOCOL_EVENT_ASSET_CHUNK,
	PROTOCOL_EVENT_ASSET_END,
	PROTOCOL_EVENT_ASSET_ERROR,
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
	u8 item_index;
	u8 item_count;
	char species_id[16];
	char display_name[16];
	char source_type[8];
	u8 temp_min;
	u8 temp_max;
	u8 humidity_min;
	u8 humidity_max;
	u8 light_min;
	u8 light_max;
	u16 transfer_id;
	u16 sequence;
	u16 chunk_count;
	u16 byte_size;
	u32 crc32;
	u8 asset_width;
	u8 asset_height;
	char asset_state[12];
	char asset_hex[81];
} ProtocolEvent;

void Protocol_Init(void);
void Protocol_InputBytes(const u8 *data,u16 length);
u8 Protocol_GetEvent(ProtocolEvent *event);
u16 Protocol_GetDroppedEventCount(void);
u16 Protocol_BuildAiRequest(u8 *destination,u16 size,u16 request_id,
	                         u8 temperature,u8 humidity,u8 light,
	                         const char *light_level);
u16 Protocol_BuildPlantAiRequest(u8 *destination,u16 size,u16 request_id,
	                              const char *device_id,const char *species_id,
	                              u8 temperature,u8 humidity,u8 light,
	                              const char *light_level);
u16 Protocol_BuildPlantListRequest(u8 *destination,u16 size,
	                                const char *device_id);
u16 Protocol_BuildPlantDetailRequest(u8 *destination,u16 size,
	                                  const char *species_id);
u16 Protocol_BuildPlantSelect(u8 *destination,u16 size,
	                           const char *device_id,const char *species_id);
u16 Protocol_BuildAssetRequest(u8 *destination,u16 size,u16 transfer_id,
	                            const char *species_id,const char *state);

#endif
