#ifndef _GREENMIND_FONT_SERVICE_H
#define _GREENMIND_FONT_SERVICE_H

#include "system.h"

#define FONT_SERVICE_CELL_SIZE       12
#define FONT_SERVICE_GLYPH_BYTES     18
#define FONT_SERVICE_GB2312_SLOTS    8178
#define FONT_SERVICE_UNICODE_ENTRIES 7445

u8 FontService_IsReady(void);
u16 FontService_MeasureUtf8(const u8 *text,u8 scale);
u16 FontService_DrawUtf8(u16 x,u16 y,const u8 *text,u8 scale,
                         u16 color,u16 background);
u8 FontService_DrawUtf8Wrapped(u16 x,u16 y,u16 width,u8 max_lines,
	                           u8 line_height,const u8 *text,
	                           u16 color,u16 background);
u16 FontService_CountUtf8WrappedLines(u16 width,const u8 *text);
u8 FontService_DrawUtf8WrappedFromLine(u16 x,u16 y,u16 width,
	                                   u16 first_line,u8 max_lines,
	                                   u8 line_height,const u8 *text,
	                                   u16 color,u16 background);

#endif
