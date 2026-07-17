#ifndef _ASSET_SERVICE_H
#define _ASSET_SERVICE_H

#include "system.h"
#include "plant_assets_builtin.h"

#define ASSET_SERVICE_STATE_TEXT 12
#define ASSET_SERVICE_ERROR_TEXT 24

typedef enum
{
	ASSET_TRANSFER_IDLE=0,
	ASSET_TRANSFER_REQUESTING,
	ASSET_TRANSFER_RECEIVING,
	ASSET_TRANSFER_READY,
	ASSET_TRANSFER_ERROR
} AssetTransferState;

void AssetService_Init(void);
void AssetService_StartRequest(u16 transfer_id,const char *species_id,
	                            const char *state);
u8 AssetService_Begin(u16 transfer_id,const char *species_id,
	                  const char *state,u8 width,u8 height,u16 byte_size,
	                  u16 chunk_count,u32 crc32);
u8 AssetService_AppendChunk(u16 transfer_id,u16 sequence,
	                        const char *hex_payload);
u8 AssetService_End(u16 transfer_id,u16 chunk_count,u32 crc32);
void AssetService_Fail(u16 transfer_id,const char *error);
u8 AssetService_Task(void);
void AssetService_Cancel(const char *error);

AssetTransferState AssetService_GetState(void);
u16 AssetService_GetTransferId(void);
const char *AssetService_GetSpeciesId(void);
const char *AssetService_GetAssetState(void);
const char *AssetService_GetError(void);
const u16 *AssetService_GetPixels(void);
u8 AssetService_HasImage(const char *species_id,const char *state);

#endif
