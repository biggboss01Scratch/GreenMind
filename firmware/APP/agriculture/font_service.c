#include "font_service.h"
#include "tftlcd.h"

#define FONT_SERVICE_FONT_BYTES \
	(FONT_SERVICE_GB2312_SLOTS*FONT_SERVICE_GLYPH_BYTES)
#define FONT_SERVICE_MAP_BYTES \
	(FONT_SERVICE_UNICODE_ENTRIES*4)

extern const u8 g_greenmind_gb2312_font[];
extern const u8 g_greenmind_gb2312_font_end[];
extern const u8 g_greenmind_unicode_gb2312_map[];
extern const u8 g_greenmind_unicode_gb2312_map_end[];

static u16 FontService_ReadU16Le(const u8 *data)
{
	return (u16)((u16)data[0]|((u16)data[1]<<8));
}

static u16 FontService_DecodeUtf8(const u8 **cursor)
{
	const u8 *text=*cursor;
	u8 first=text[0];
	u8 second;
	u8 third;
	u16 codepoint;

	if(first<0x80)
	{
		*cursor=text+1;
		return first;
	}

	second=text[1];
	if(second==0)
	{
		*cursor=text+1;
		return '?';
	}
	if((first&0xE0)==0xC0 && (second&0xC0)==0x80)
	{
		codepoint=(u16)(((u16)(first&0x1F)<<6)|(second&0x3F));
		*cursor=text+2;
		if(codepoint>=0x80)return codepoint;
		return '?';
	}

	if(text[2]==0)
	{
		*cursor=text+1;
		return '?';
	}
	third=text[2];
	if((first&0xF0)==0xE0 &&
	   (second&0xC0)==0x80 && (third&0xC0)==0x80)
	{
		codepoint=(u16)(((u16)(first&0x0F)<<12)|
		                ((u16)(second&0x3F)<<6)|(third&0x3F));
		*cursor=text+3;
		if(codepoint>=0x800)return codepoint;
		return '?';
	}

	*cursor=text+1;
	return '?';
}

static u16 FontService_FindGb2312(u16 unicode)
{
	s16 low=0;
	s16 high=(s16)(FONT_SERVICE_UNICODE_ENTRIES-1);
	s16 middle;
	u16 current;
	const u8 *entry;

	while(low<=high)
	{
		middle=(s16)(low+(high-low)/2);
		entry=&g_greenmind_unicode_gb2312_map[(u16)middle*4];
		current=FontService_ReadU16Le(entry);
		if(current==unicode)return FontService_ReadU16Le(entry+2);
		if(current<unicode)low=(s16)(middle+1);
		else high=(s16)(middle-1);
	}
	return 0;
}

static u8 FontService_GlyphBit(const u8 *glyph,u8 row,u8 column)
{
	u16 bit=(u16)row*FONT_SERVICE_CELL_SIZE+column;
	return (glyph[bit>>3]&(u8)(0x80>>(bit&7)))?1:0;
}

static void FontService_DrawMissing(u16 x,u16 y,u8 scale,
	                                 u16 color,u16 background)
{
	u16 cell=(u16)(FONT_SERVICE_CELL_SIZE*scale);
	FRONT_COLOR=color;
	BACK_COLOR=background;
	LCD_Fill(x,y,(u16)(x+cell-1),(u16)(y+cell-1),background);
	LCD_ShowChar((u16)(x+3*scale),y,'?',(u8)(12*scale),0);
}

static void FontService_DrawGb2312Glyph(u16 x,u16 y,u16 gb2312,u8 scale,
	                                    u16 color,u16 background)
{
	u8 lead=(u8)(gb2312>>8);
	u8 trail=(u8)gb2312;
	u16 slot;
	const u8 *glyph;
	u8 row;
	u8 column;
	u8 run_start;
	u16 cell=(u16)(FONT_SERVICE_CELL_SIZE*scale);

	if(lead<0xA1 || lead>0xF7 || trail<0xA1 || trail>0xFE)
	{
		FontService_DrawMissing(x,y,scale,color,background);
		return;
	}

	slot=(u16)((u16)(lead-0xA1)*94+(trail-0xA1));
	glyph=&g_greenmind_gb2312_font[(u32)slot*FONT_SERVICE_GLYPH_BYTES];
	LCD_Fill(x,y,(u16)(x+cell-1),(u16)(y+cell-1),background);

	for(row=0;row<FONT_SERVICE_CELL_SIZE;row++)
	{
		column=0;
		while(column<FONT_SERVICE_CELL_SIZE)
		{
			while(column<FONT_SERVICE_CELL_SIZE &&
			      !FontService_GlyphBit(glyph,row,column))column++;
			if(column>=FONT_SERVICE_CELL_SIZE)break;
			run_start=column;
			while(column<FONT_SERVICE_CELL_SIZE &&
			      FontService_GlyphBit(glyph,row,column))column++;
			LCD_Fill((u16)(x+run_start*scale),(u16)(y+row*scale),
			         (u16)(x+column*scale-1),
			         (u16)(y+(row+1)*scale-1),color);
		}
	}
}

u8 FontService_IsReady(void)
{
	u32 font_size=(u32)g_greenmind_gb2312_font_end-
	              (u32)g_greenmind_gb2312_font;
	u32 map_size=(u32)g_greenmind_unicode_gb2312_map_end-
	             (u32)g_greenmind_unicode_gb2312_map;
	return (font_size==FONT_SERVICE_FONT_BYTES &&
	        map_size==FONT_SERVICE_MAP_BYTES)?1:0;
}

u16 FontService_MeasureUtf8(const u8 *text,u8 scale)
{
	u16 width=0;
	u16 codepoint;
	const u8 *cursor=text;

	if(scale!=1 && scale!=2)scale=1;
	while(*cursor)
	{
		codepoint=FontService_DecodeUtf8(&cursor);
		if(codepoint=='\n')break;
		width=(u16)(width+((codepoint<0x80)?6:12)*scale);
	}
	return width;
}

u16 FontService_DrawUtf8(u16 x,u16 y,const u8 *text,u8 scale,
	                      u16 color,u16 background)
{
	u16 origin=x;
	u16 codepoint;
	u16 gb2312;
	const u8 *cursor=text;

	if(scale!=1 && scale!=2)scale=1;
	FRONT_COLOR=color;
	BACK_COLOR=background;

	while(*cursor)
	{
		codepoint=FontService_DecodeUtf8(&cursor);
		if(codepoint=='\n')break;
		if(codepoint>=0x20 && codepoint<=0x7E)
		{
			if((u16)(x+6*scale)>tftlcd_data.width)break;
			LCD_ShowChar(x,y,(u8)codepoint,(u8)(12*scale),0);
			x=(u16)(x+6*scale);
			continue;
		}

		if((u16)(x+12*scale)>tftlcd_data.width)break;
		gb2312=FontService_FindGb2312(codepoint);
		if(gb2312)
			FontService_DrawGb2312Glyph(x,y,gb2312,scale,color,background);
		else FontService_DrawMissing(x,y,scale,color,background);
		x=(u16)(x+12*scale);
	}
	return (u16)(x-origin);
}

u16 FontService_CountUtf8WrappedLines(u16 width,const u8 *text)
{
	u16 codepoint;
	u16 glyph_width;
	u16 used=0;
	u16 lines=1;
	const u8 *cursor=text;

	if(width<12 || text[0]=='\0')return 0;
	while(*cursor)
	{
		codepoint=FontService_DecodeUtf8(&cursor);
		if(codepoint=='\n')
		{
			lines++;
			used=0;
			continue;
		}
		glyph_width=(codepoint<0x80)?6:12;
		if((u16)(used+glyph_width)>width)
		{
			lines++;
			used=0;
			if(codepoint==' ')continue;
		}
		used=(u16)(used+glyph_width);
	}
	return lines;
}

u8 FontService_DrawUtf8WrappedFromLine(u16 x,u16 y,u16 width,
	                                   u16 first_line,u8 max_lines,
	                                   u8 line_height,const u8 *text,
	                                   u16 color,u16 background)
{
	u16 cursor_x=x;
	u16 cursor_y=y;
	u16 codepoint;
	u16 gb2312;
	u16 glyph_width;
	u16 used=0;
	u16 line=0;
	u8 drawn_lines=0;
	const u8 *cursor=text;

	if(width<12 || max_lines==0 || text[0]=='\0')return 0;
	if(line_height<FONT_SERVICE_CELL_SIZE)line_height=FONT_SERVICE_CELL_SIZE;
	FRONT_COLOR=color;
	BACK_COLOR=background;

	while(*cursor)
	{
		codepoint=FontService_DecodeUtf8(&cursor);
		if(codepoint=='\n')
		{
			line++;
			used=0;
			cursor_x=x;
			if(line>=first_line)
				cursor_y=(u16)(y+(line-first_line)*line_height);
			if(line>=first_line+max_lines)break;
			continue;
		}
		glyph_width=(codepoint<0x80)?6:12;
		if((u16)(used+glyph_width)>width)
		{
			line++;
			used=0;
			cursor_x=x;
			if(line>=first_line)
				cursor_y=(u16)(y+(line-first_line)*line_height);
			if(line>=first_line+max_lines)break;
			if(codepoint==' ')continue;
		}
		if(line<first_line)
		{
			used=(u16)(used+glyph_width);
			continue;
		}
		if((u8)(line-first_line+1)>drawn_lines)
			drawn_lines=(u8)(line-first_line+1);
		if(codepoint>=0x20 && codepoint<=0x7E)
			LCD_ShowChar(cursor_x,cursor_y,(u8)codepoint,12,0);
		else
		{
			gb2312=FontService_FindGb2312(codepoint);
			if(gb2312)
				FontService_DrawGb2312Glyph(cursor_x,cursor_y,gb2312,1,
				                           color,background);
			else FontService_DrawMissing(cursor_x,cursor_y,1,color,background);
		}
		cursor_x=(u16)(cursor_x+glyph_width);
		used=(u16)(used+glyph_width);
	}
	return drawn_lines;
}

u8 FontService_DrawUtf8Wrapped(u16 x,u16 y,u16 width,u8 max_lines,
	                           u8 line_height,const u8 *text,
	                           u16 color,u16 background)
{
	return FontService_DrawUtf8WrappedFromLine(x,y,width,0,max_lines,
	                                          line_height,text,
	                                          color,background);
}
