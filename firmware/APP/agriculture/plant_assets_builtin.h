#ifndef _PLANT_ASSETS_BUILTIN_H
#define _PLANT_ASSETS_BUILTIN_H

#include "system.h"

#define PLANT_ASSET_WIDTH       48
#define PLANT_ASSET_HEIGHT      48
#define PLANT_ASSET_PIXEL_COUNT (PLANT_ASSET_WIDTH*PLANT_ASSET_HEIGHT)
#define PLANT_ASSET_BYTE_SIZE   (PLANT_ASSET_PIXEL_COUNT*2)

extern const u16 g_pothos_normal[PLANT_ASSET_PIXEL_COUNT];
extern const u16 g_pothos_attention[PLANT_ASSET_PIXEL_COUNT];
extern const u16 g_pothos_danger[PLANT_ASSET_PIXEL_COUNT];

#endif
