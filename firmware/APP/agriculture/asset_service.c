#include "asset_service.h"
#include "app_config.h"
#include <string.h>

static AssetTransferState g_state=ASSET_TRANSFER_IDLE;
static u16 g_transfer_id=0;
static char g_species_id[16];
static char g_asset_state[ASSET_SERVICE_STATE_TEXT];
static char g_error[ASSET_SERVICE_ERROR_TEXT];
static u16 g_expected_bytes=0;
static u16 g_expected_chunks=0;
static u16 g_next_sequence=0;
static u16 g_received_bytes=0;
static u16 g_pixel_index=0;
static u32 g_expected_crc=0;
static u32 g_running_crc=0xFFFFFFFFUL;
static u16 g_timeout_ticks=0;
static u8 g_high_byte=0;
static u8 g_have_high_byte=0;
static u16 g_pixels[PLANT_ASSET_PIXEL_COUNT];

static void AssetService_Copy(char *destination,u16 size,const char *source)
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

static void AssetService_SetError(const char *error)
{
	g_state=ASSET_TRANSFER_ERROR;
	g_timeout_ticks=0;
	AssetService_Copy(g_error,sizeof(g_error),error);
}

static s8 AssetService_HexNibble(char value)
{
	if(value>='0' && value<='9')return (s8)(value-'0');
	if(value>='A' && value<='F')return (s8)(value-'A'+10);
	if(value>='a' && value<='f')return (s8)(value-'a'+10);
	return -1;
}

static void AssetService_CrcByte(u8 value)
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

void AssetService_Init(void)
{
	g_state=ASSET_TRANSFER_IDLE;
	g_transfer_id=0;
	g_species_id[0]='\0';
	g_asset_state[0]='\0';
	AssetService_Copy(g_error,sizeof(g_error),"NONE");
	g_timeout_ticks=0;
}

void AssetService_StartRequest(u16 transfer_id,const char *species_id,
	                            const char *state)
{
	g_state=ASSET_TRANSFER_REQUESTING;
	g_transfer_id=transfer_id;
	AssetService_Copy(g_species_id,sizeof(g_species_id),species_id);
	AssetService_Copy(g_asset_state,sizeof(g_asset_state),state);
	AssetService_Copy(g_error,sizeof(g_error),"NONE");
	g_expected_bytes=0;
	g_expected_chunks=0;
	g_next_sequence=0;
	g_received_bytes=0;
	g_pixel_index=0;
	g_expected_crc=0;
	g_running_crc=0xFFFFFFFFUL;
	g_have_high_byte=0;
	g_timeout_ticks=APP_ASSET_TIMEOUT_TICKS;
}

u8 AssetService_Begin(u16 transfer_id,const char *species_id,
	                  const char *state,u8 width,u8 height,u16 byte_size,
	                  u16 chunk_count,u32 crc32)
{
	if(g_state!=ASSET_TRANSFER_REQUESTING || transfer_id!=g_transfer_id ||
	   strcmp(species_id,g_species_id)!=0 ||
	   strcmp(state,g_asset_state)!=0)
	{
		AssetService_SetError("ASSET ID MISMATCH");
		return 0;
	}
	if(width!=PLANT_ASSET_WIDTH || height!=PLANT_ASSET_HEIGHT ||
	   byte_size!=PLANT_ASSET_BYTE_SIZE || chunk_count==0)
	{
		AssetService_SetError("ASSET SIZE ERROR");
		return 0;
	}
	g_state=ASSET_TRANSFER_RECEIVING;
	g_expected_bytes=byte_size;
	g_expected_chunks=chunk_count;
	g_expected_crc=crc32;
	g_next_sequence=0;
	g_received_bytes=0;
	g_pixel_index=0;
	g_running_crc=0xFFFFFFFFUL;
	g_have_high_byte=0;
	g_timeout_ticks=APP_ASSET_TIMEOUT_TICKS;
	return 1;
}

u8 AssetService_AppendChunk(u16 transfer_id,u16 sequence,
	                        const char *hex_payload)
{
	u16 length;
	u16 i;
	s8 high;
	s8 low;
	u8 value;

	if(g_state!=ASSET_TRANSFER_RECEIVING || transfer_id!=g_transfer_id)
	{
		AssetService_SetError("CHUNK ID ERROR");
		return 0;
	}
	if(sequence!=g_next_sequence)
	{
		AssetService_SetError("CHUNK SEQ ERROR");
		return 0;
	}
	length=(u16)strlen(hex_payload);
	if(length==0 || length>80 || (length&1)!=0)
	{
		AssetService_SetError("CHUNK HEX ERROR");
		return 0;
	}
	if((u32)g_received_bytes+(u32)(length/2)>g_expected_bytes)
	{
		AssetService_SetError("CHUNK OVERFLOW");
		return 0;
	}

	for(i=0;i<length;i+=2)
	{
		high=AssetService_HexNibble(hex_payload[i]);
		low=AssetService_HexNibble(hex_payload[i+1]);
		if(high<0 || low<0)
		{
			AssetService_SetError("CHUNK HEX ERROR");
			return 0;
		}
		value=(u8)(((u8)high<<4)|(u8)low);
		AssetService_CrcByte(value);
		if(!g_have_high_byte)
		{
			g_high_byte=value;
			g_have_high_byte=1;
		}
		else
		{
			if(g_pixel_index>=PLANT_ASSET_PIXEL_COUNT)
			{
				AssetService_SetError("PIXEL OVERFLOW");
				return 0;
			}
			g_pixels[g_pixel_index++]=(u16)(((u16)g_high_byte<<8)|value);
			g_have_high_byte=0;
		}
		g_received_bytes++;
	}
	g_next_sequence++;
	g_timeout_ticks=APP_ASSET_TIMEOUT_TICKS;
	return 1;
}

u8 AssetService_End(u16 transfer_id,u16 chunk_count,u32 crc32)
{
	u32 final_crc;
	if(g_state!=ASSET_TRANSFER_RECEIVING || transfer_id!=g_transfer_id)
	{
		AssetService_SetError("END ID ERROR");
		return 0;
	}
	final_crc=g_running_crc^0xFFFFFFFFUL;
	if(chunk_count!=g_expected_chunks || g_next_sequence!=g_expected_chunks ||
	   g_received_bytes!=g_expected_bytes ||
	   g_pixel_index!=PLANT_ASSET_PIXEL_COUNT || g_have_high_byte ||
	   crc32!=g_expected_crc || final_crc!=g_expected_crc)
	{
		AssetService_SetError("ASSET CRC ERROR");
		return 0;
	}
	g_state=ASSET_TRANSFER_READY;
	g_timeout_ticks=0;
	AssetService_Copy(g_error,sizeof(g_error),"NONE");
	return 1;
}

void AssetService_Fail(u16 transfer_id,const char *error)
{
	if(transfer_id!=0 && transfer_id!=g_transfer_id)return;
	AssetService_SetError(error);
}

u8 AssetService_Task(void)
{
	if(g_state!=ASSET_TRANSFER_REQUESTING &&
	   g_state!=ASSET_TRANSFER_RECEIVING)return 0;
	if(g_timeout_ticks>0)g_timeout_ticks--;
	if(g_timeout_ticks==0)
	{
		AssetService_SetError("ASSET TIMEOUT");
		return 1;
	}
	return 0;
}

void AssetService_Cancel(const char *error)
{
	AssetService_SetError(error);
}

AssetTransferState AssetService_GetState(void)
{
	return g_state;
}

u16 AssetService_GetTransferId(void)
{
	return g_transfer_id;
}

const char *AssetService_GetSpeciesId(void)
{
	return g_species_id;
}

const char *AssetService_GetAssetState(void)
{
	return g_asset_state;
}

const char *AssetService_GetError(void)
{
	return g_error;
}

const u16 *AssetService_GetPixels(void)
{
	return g_pixels;
}

u8 AssetService_HasImage(const char *species_id,const char *state)
{
	return (g_state==ASSET_TRANSFER_READY &&
	        strcmp(species_id,g_species_id)==0 &&
	        strcmp(state,g_asset_state)==0)?1:0;
}
