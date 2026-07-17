#include "ui_service.h"
#include "app_config.h"
#include "asset_service.h"
#include "plant_assets_builtin.h"
#include "tftlcd.h"
#include <stdio.h>
#include <string.h>

#define UI_BG          0xEFBD
#define UI_HEADER      0x2B49
#define UI_CARD        0xFFFF
#define UI_TEXT        0x21A5
#define UI_MUTED       0x7BEF
#define UI_OK          0x3D8E
#define UI_WARN        0xFDC0
#define UI_ERROR       0xF800
#define UI_DISABLED    0xC618
#define UI_SHADOW      0xD71B
#define UI_HERO        0xE7BD
#define UI_GREEN       0x45C7
#define UI_GREEN_DARK  0x1B84
#define UI_GREEN_LIGHT 0x9EEB
#define UI_POT         0xD34A
#define UI_POT_DARK    0x9226
#define UI_PIXEL_DARK  0x1082

#define UI_NAV_Y1      432
#define UI_NAV_Y2      479

#define UI_PLANT_GOOD       0
#define UI_PLANT_ATTENTION  1
#define UI_PLANT_ERROR      2
#define UI_PLANT_DANGER     3
#define UI_PLANT_THINKING   4

static AppState g_last_state;
static u8 g_render_cache_valid=0;

static void UiService_Text(u16 x,u16 y,u8 size,const char *text,u16 color,u16 background)
{
	FRONT_COLOR=color;
	BACK_COLOR=background;
	LCD_ShowString(x,y,tftlcd_data.width,tftlcd_data.height,size,(u8 *)text);
}

static void UiService_TextCentered(u16 x1,u16 x2,u16 y,u8 size,
	                               const char *text,u16 color,u16 background)
{
	u16 text_width=(u16)(strlen(text)*(size/2));
	u16 x=x1;
	if(text_width<(u16)(x2-x1+1))x=(u16)(x1+(x2-x1+1-text_width)/2);
	UiService_Text(x,y,size,text,color,background);
}

static void UiService_ClearRect(u16 x1,u16 y1,u16 x2,u16 y2,u16 color)
{
	LCD_Fill(x1,y1,x2,y2,color);
}

static void UiService_FillRoundRect(u16 x1,u16 y1,u16 x2,u16 y2,
	                                u8 radius,u16 color)
{
	u16 width=(u16)(x2-x1+1);
	u16 height=(u16)(y2-y1+1);
	u16 i;
	u16 dx;
	u16 dy;
	u32 distance;
	u32 limit;

	if(radius==0)
	{
		LCD_Fill(x1,y1,x2,y2,color);
		return;
	}
	if((u16)(radius*2)>width)radius=(u8)(width/2);
	if((u16)(radius*2)>height)radius=(u8)(height/2);

	LCD_Fill((u16)(x1+radius),y1,(u16)(x2-radius),y2,color);
	LCD_Fill(x1,(u16)(y1+radius),x2,(u16)(y2-radius),color);
	limit=(u32)radius*radius;

	for(i=0;i<radius;i++)
	{
		dy=(u16)(radius-i);
		dx=radius;
		distance=(u32)dx*dx+(u32)dy*dy;
		while(dx>0 && distance>limit)
		{
			dx--;
			distance=(u32)dx*dx+(u32)dy*dy;
		}
		LCD_Fill((u16)(x1+radius-dx),(u16)(y1+i),
		         (u16)(x2-radius+dx),(u16)(y1+i),color);
		LCD_Fill((u16)(x1+radius-dx),(u16)(y2-i),
		         (u16)(x2-radius+dx),(u16)(y2-i),color);
	}
}

static void UiService_Card(u16 x1,u16 y1,u16 x2,u16 y2)
{
	UiService_FillRoundRect((u16)(x1+3),(u16)(y1+3),
	                        (u16)(x2+3),(u16)(y2+3),12,UI_SHADOW);
	UiService_FillRoundRect(x1,y1,x2,y2,12,UI_CARD);
}

static void UiService_Button(u16 x1,u16 y1,u16 x2,u16 y2,
	                         const char *label,u16 color,u8 enabled)
{
	u16 background=enabled?color:UI_DISABLED;
	UiService_FillRoundRect((u16)(x1+2),(u16)(y1+3),
	                        (u16)(x2+2),(u16)(y2+3),12,UI_SHADOW);
	UiService_FillRoundRect(x1,y1,x2,y2,12,background);
	UiService_TextCentered(x1,x2,(u16)(y1+18),16,label,
	                       enabled?WHITE:UI_MUTED,background);
}

static void UiService_Pill(u16 x1,u16 y1,u16 x2,u16 y2,
	                       const char *label,u16 color,u16 text_color)
{
	UiService_FillRoundRect(x1,y1,x2,y2,(u8)((y2-y1+1)/2),color);
	UiService_TextCentered(x1,x2,(u16)(y1+5),12,label,text_color,color);
}

static void UiService_DrawNavTab(u8 index,const char *label,AppPage active)
{
	u16 x1=(u16)(index*80+5);
	u16 x2=(u16)((index+1)*80-5);
	u16 background=((u8)active==index)?UI_HEADER:UI_CARD;
	u16 color=((u8)active==index)?WHITE:UI_MUTED;
	if((u8)active==index)
		UiService_FillRoundRect(x1,438,x2,474,10,background);
	UiService_TextCentered(x1,x2,449,12,label,color,background);
}

static void UiService_DrawNavigation(AppPage active)
{
	UiService_FillRoundRect(9,436,313,479,14,UI_SHADOW);
	UiService_FillRoundRect(6,433,310,477,14,UI_CARD);
	UiService_DrawNavTab(0,"HOME",active);
	UiService_DrawNavTab(1,"DETAIL",active);
	UiService_DrawNavTab(2,"AI",active);
	UiService_DrawNavTab(3,"SYSTEM",active);
}

static void UiService_Base(const char *title,AppPage page)
{
	LCD_Clear(UI_BG);
	UiService_FillRoundRect(11,9,314,57,14,UI_SHADOW);
	UiService_FillRoundRect(8,6,311,54,14,UI_HEADER);
	UiService_TextCentered(0,319,14,24,title,WHITE,UI_HEADER);
	UiService_Card(10,64,309,422);
	UiService_DrawNavigation(page);
}

static u8 UiService_AiActive(const AppState *state)
{
	return (state->ai_state==APP_AI_SENDING ||
	        state->ai_state==APP_AI_PREPARING ||
	        state->ai_state==APP_AI_THINKING ||
	        state->ai_state==APP_AI_VALIDATING)?1:0;
}

static u16 UiService_AiColor(const AppState *state)
{
	if(state->ai_state==APP_AI_DONE)return UI_OK;
	if(state->ai_state==APP_AI_ERROR || state->ai_state==APP_AI_TIMEOUT)
		return UI_ERROR;
	if(state->ai_state!=APP_AI_IDLE)return UI_WARN;
	return UI_HEADER;
}

static u8 UiService_PlantMode(const AppState *state)
{
	if(UiService_AiActive(state))return UI_PLANT_THINKING;
	if(!state->dht_valid || !state->light_valid)return UI_PLANT_ERROR;
	if(state->ai_state==APP_AI_DONE)
	{
		if(strcmp(state->ai_status,"DANGER")==0)return UI_PLANT_DANGER;
		if(strcmp(state->ai_status,"WARN")==0)return UI_PLANT_ATTENTION;
		if(strcmp(state->ai_status,"NORMAL")==0)return UI_PLANT_GOOD;
	}
	if(state->temperature>=30 || state->humidity<35 || state->humidity>80 ||
	   state->light>APP_LIGHT_NORMAL_MAX)
		return UI_PLANT_ATTENTION;
	return UI_PLANT_GOOD;
}

static const char *UiService_EnvironmentText(const AppState *state)
{
	switch(UiService_PlantMode(state))
	{
		case UI_PLANT_THINKING:return "AI IS THINKING...";
		case UI_PLANT_ERROR:return "CHECK THE SENSORS";
		case UI_PLANT_DANGER:return "PLANT STATUS: DANGER";
		case UI_PLANT_ATTENTION:return "PLANT NEEDS ATTENTION";
		default:return "ENVIRONMENT GOOD";
	}
}

static void UiService_Pixel(u16 x,u16 y,u16 width,u16 height,u16 color)
{
	LCD_Fill(x,y,(u16)(x+width-1),(u16)(y+height-1),color);
}

static void UiService_DrawFallbackPlant(const AppState *state)
{
	u8 mode=UiService_PlantMode(state);
	u16 leaf=UI_GREEN;
	u16 leaf_light=UI_GREEN_LIGHT;
	u16 stem=UI_GREEN_DARK;
	u16 pot=UI_POT;

	if(mode==UI_PLANT_ATTENTION)
	{
		leaf=0x8DA4;
		leaf_light=UI_WARN;
		stem=0x6B63;
	}
	else if(mode==UI_PLANT_DANGER)
	{
		leaf=0x8B26;
		leaf_light=0xC4CB;
		stem=0x59E4;
		pot=0xA514;
	}
	else if(mode==UI_PLANT_ERROR)
	{
		leaf=UI_DISABLED;
		leaf_light=UI_MUTED;
		stem=UI_MUTED;
		pot=UI_DISABLED;
	}

	/* Pixel leaves and stem. */
	UiService_Pixel(151,74,18,18,leaf_light);
	UiService_Pixel(143,82,34,18,leaf);
	UiService_Pixel(119,94,26,18,leaf);
	UiService_Pixel(111,102,26,20,leaf_light);
	UiService_Pixel(129,106,26,18,leaf);
	UiService_Pixel(175,94,26,18,leaf);
	UiService_Pixel(183,102,26,20,leaf_light);
	UiService_Pixel(165,106,26,18,leaf);
	UiService_Pixel(155,114,10,30,stem);

	/* Pixel pot with a face. */
	UiService_Pixel(122,132,76,14,UI_POT_DARK);
	UiService_Pixel(130,146,60,54,pot);
	UiService_Pixel(138,200,44,8,UI_POT_DARK);
	UiService_Pixel(136,152,8,40,0xE3CC);

	if(mode==UI_PLANT_ERROR)
	{
		UiService_Pixel(143,160,5,5,UI_PIXEL_DARK);
		UiService_Pixel(151,168,5,5,UI_PIXEL_DARK);
		UiService_Pixel(151,160,5,5,UI_PIXEL_DARK);
		UiService_Pixel(143,168,5,5,UI_PIXEL_DARK);
		UiService_Pixel(165,160,5,5,UI_PIXEL_DARK);
		UiService_Pixel(173,168,5,5,UI_PIXEL_DARK);
		UiService_Pixel(173,160,5,5,UI_PIXEL_DARK);
		UiService_Pixel(165,168,5,5,UI_PIXEL_DARK);
		UiService_Pixel(151,184,18,5,UI_PIXEL_DARK);
	}
	else
	{
		UiService_Pixel(145,162,7,9,UI_PIXEL_DARK);
		UiService_Pixel(168,162,7,9,UI_PIXEL_DARK);
		if(mode==UI_PLANT_GOOD)
		{
			UiService_Pixel(151,181,5,5,UI_PIXEL_DARK);
			UiService_Pixel(156,186,12,5,UI_PIXEL_DARK);
			UiService_Pixel(168,181,5,5,UI_PIXEL_DARK);
		}
		else if(mode==UI_PLANT_DANGER)
		{
			UiService_Pixel(151,186,5,5,UI_PIXEL_DARK);
			UiService_Pixel(156,181,12,5,UI_PIXEL_DARK);
			UiService_Pixel(168,186,5,5,UI_PIXEL_DARK);
			UiService_Pixel(179,172,5,9,UI_ERROR);
		}
		else
		{
			UiService_Pixel(153,183,14,5,UI_PIXEL_DARK);
		}
	}

	if(mode==UI_PLANT_THINKING)
	{
		UiService_Pixel(202,88,6,6,UI_WARN);
		UiService_Pixel(212,80,6,6,UI_WARN);
		UiService_Pixel(222,74,6,6,UI_WARN);
	}
}

static const char *UiService_DesiredAssetState(const AppState *state)
{
	u8 mode=UiService_PlantMode(state);
	if(mode==UI_PLANT_DANGER)return "DANGER";
	if(mode==UI_PLANT_ATTENTION || mode==UI_PLANT_THINKING)return "ATTENTION";
	return "NORMAL";
}

static const u16 *UiService_BuiltInPothos(const AppState *state)
{
	const char *asset_state=UiService_DesiredAssetState(state);
	if(strcmp(asset_state,"DANGER")==0)return g_pothos_danger;
	if(strcmp(asset_state,"ATTENTION")==0)return g_pothos_attention;
	return g_pothos_normal;
}

static void UiService_DrawAssetSprite(const u16 *pixels)
{
	u16 row=0;
	u16 repeated_rows;
	u16 column;
	u16 run;
	u16 color;
	const u16 *next_row;

	while(row<PLANT_ASSET_HEIGHT)
	{
		repeated_rows=1;
		while((u16)(row+repeated_rows)<PLANT_ASSET_HEIGHT)
		{
			next_row=&pixels[(u16)(row+repeated_rows)*PLANT_ASSET_WIDTH];
			if(memcmp(&pixels[row*PLANT_ASSET_WIDTH],next_row,
			          PLANT_ASSET_WIDTH*sizeof(u16))!=0)break;
			repeated_rows++;
		}
		column=0;
		while(column<PLANT_ASSET_WIDTH)
		{
			color=pixels[row*PLANT_ASSET_WIDTH+column];
			run=1;
			while((u16)(column+run)<PLANT_ASSET_WIDTH &&
			      pixels[row*PLANT_ASSET_WIDTH+column+run]==color)
				run++;
			LCD_Fill((u16)(88+column*3),(u16)(70+row*3),
			         (u16)(88+(column+run)*3-1),
			         (u16)(70+(row+repeated_rows)*3-1),color);
			column=(u16)(column+run);
		}
		row=(u16)(row+repeated_rows);
	}
}

static u8 UiService_DrawCurrentPlantAsset(const AppState *state)
{
	const char *desired=UiService_DesiredAssetState(state);
	if(strcmp(state->species_id,"pothos")==0)
	{
		UiService_DrawAssetSprite(UiService_BuiltInPothos(state));
		return 1;
	}
	if(AssetService_HasImage(state->species_id,desired))
	{
		UiService_DrawAssetSprite(AssetService_GetPixels());
		return 1;
	}
	return 0;
}

static void UiService_DrawHomeWifi(const AppState *state)
{
	UiService_ClearRect(244,14,309,38,UI_HEADER);
	UiService_Text(244,18,16,"WiFi",WHITE,UI_HEADER);
	UiService_Pixel(292,22,10,10,state->wifi_connected?UI_OK:UI_ERROR);
}

static void UiService_DrawHomePlant(const AppState *state)
{
	u8 mode=UiService_PlantMode(state);
	u16 color=mode==UI_PLANT_GOOD?UI_OK:
	          ((mode==UI_PLANT_ERROR || mode==UI_PLANT_DANGER)?
	           UI_ERROR:UI_WARN);
	u16 text_color=(mode==UI_PLANT_ATTENTION || mode==UI_PLANT_THINKING)?
	               UI_TEXT:WHITE;

	UiService_FillRoundRect(89,71,237,219,18,UI_SHADOW);
	UiService_FillRoundRect(86,68,234,216,18,UI_HERO);
	if(!UiService_DrawCurrentPlantAsset(state))
		UiService_DrawFallbackPlant(state);
	UiService_Pill(94,72,140,94,state->plant_name,UI_CARD,UI_GREEN_DARK);
	if(strcmp(state->species_id,"pothos")!=0 &&
	   strcmp(state->asset_status,"LOADING")==0)
		UiService_Pill(160,72,228,94,"LOADING",UI_WARN,UI_TEXT);
	else if(strcmp(state->species_id,"pothos")!=0 &&
	        strcmp(state->asset_status,"ERROR")==0)
		UiService_Pill(160,72,228,94,"IMG ERROR",UI_ERROR,WHITE);
	UiService_ClearRect(60,218,260,243,UI_CARD);
	UiService_FillRoundRect(60,218,260,242,10,color);
	UiService_TextCentered(60,260,222,16,UiService_EnvironmentText(state),
	                       text_color,color);
}

static void UiService_DrawHomeValues(const AppState *state)
{
	char line[48];
	UiService_ClearRect(24,246,296,286,UI_CARD);
	if(state->dht_valid)
		sprintf(line,"TEMP %u C        HUMIDITY %u %%",
		        (unsigned int)state->temperature,
		        (unsigned int)state->humidity);
	else sprintf(line,"TEMP --          HUMIDITY --");
	UiService_TextCentered(24,296,248,16,line,
	                       state->dht_valid?UI_TEXT:UI_ERROR,UI_CARD);

	if(state->light_valid)
		sprintf(line,"LIGHT %u %%       AI %s",
		        (unsigned int)state->light,
		        state->gateway_ready?"READY":"OFFLINE");
	else sprintf(line,"LIGHT --          AI %s",
	             state->gateway_ready?"READY":"OFFLINE");
	UiService_TextCentered(24,296,272,16,line,
	                       state->light_valid?UI_TEXT:UI_ERROR,UI_CARD);
}

static void UiService_DrawHomeAdvice(const AppState *state)
{
	const char *advice=state->ai_advice;
	char title[48];
	UiService_ClearRect(24,294,296,350,UI_CARD);
	sprintf(title,"MAIN ADVICE - %s",state->plant_name);
	UiService_Text(28,296,12,title,UI_MUTED,UI_CARD);
	if(strcmp(advice,"-")==0)
		advice="KEEP CURRENT SETUP";
	UiService_Text(28,321,16,advice,UI_TEXT,UI_CARD);
}

static void UiService_DrawHomeButtons(const AppState *state)
{
	UiService_Button(18,362,151,416,"DETAIL",UI_GREEN_DARK,1);
	UiService_Button(169,362,302,416,
	                 UiService_AiActive(state)?"WORKING":"ASK AI",
	                 UI_HEADER,(u8)!UiService_AiActive(state));
}

static void UiService_RenderHomeFull(const AppState *state)
{
	LCD_Clear(UI_BG);
	UiService_FillRoundRect(11,9,314,57,14,UI_SHADOW);
	UiService_FillRoundRect(8,6,311,54,14,UI_HEADER);
	UiService_Text(14,14,24,"GreenMind",WHITE,UI_HEADER);
	UiService_Card(10,64,309,422);
	UiService_DrawNavigation(APP_PAGE_HOME);
	UiService_DrawHomeWifi(state);
	UiService_DrawHomePlant(state);
	UiService_DrawHomeValues(state);
	UiService_Card(20,292,300,350);
	UiService_DrawHomeAdvice(state);
	UiService_DrawHomeButtons(state);
}

static void UiService_RenderHomeChanged(const AppState *state)
{
	if(state->wifi_connected!=g_last_state.wifi_connected)
		UiService_DrawHomeWifi(state);
	if(state->dht_valid!=g_last_state.dht_valid ||
	   state->light_valid!=g_last_state.light_valid ||
	   state->temperature!=g_last_state.temperature ||
	   state->humidity!=g_last_state.humidity ||
	   state->light!=g_last_state.light ||
	   strcmp(state->ai_status,g_last_state.ai_status)!=0 ||
	   strcmp(state->plant_name,g_last_state.plant_name)!=0 ||
	   state->asset_revision!=g_last_state.asset_revision ||
	   state->ai_state!=g_last_state.ai_state)
		UiService_DrawHomePlant(state);
	if(state->dht_valid!=g_last_state.dht_valid ||
	   state->light_valid!=g_last_state.light_valid ||
	   state->temperature!=g_last_state.temperature ||
	   state->humidity!=g_last_state.humidity ||
	   state->light!=g_last_state.light ||
	   state->gateway_ready!=g_last_state.gateway_ready)
		UiService_DrawHomeValues(state);
	if(strcmp(state->ai_advice,g_last_state.ai_advice)!=0 ||
	   strcmp(state->plant_name,g_last_state.plant_name)!=0)
		UiService_DrawHomeAdvice(state);
	if(state->ai_state!=g_last_state.ai_state)
		UiService_DrawHomeButtons(state);
}

static u16 UiService_TemperatureColor(const AppState *state)
{
	if(!state->dht_valid)return UI_ERROR;
	if(state->temperature<18 || state->temperature>=30)return UI_WARN;
	return UI_OK;
}

static u16 UiService_HumidityColor(const AppState *state)
{
	if(!state->dht_valid)return UI_ERROR;
	if(state->humidity<35 || state->humidity>80)return UI_WARN;
	return UI_OK;
}

static u16 UiService_LightColor(const AppState *state)
{
	if(!state->light_valid)return UI_ERROR;
	if(state->light>APP_LIGHT_NORMAL_MAX)return UI_WARN;
	return UI_OK;
}

static void UiService_ProgressBar(u16 y,u8 percent,u16 color)
{
	u16 width;
	if(percent>100)percent=100;
	UiService_FillRoundRect(24,y,296,(u16)(y+18),9,UI_DISABLED);
	width=(u16)(((u32)percent*273)/100);
	if(width>0)
		UiService_FillRoundRect(24,y,(u16)(23+width),(u16)(y+18),9,color);
}

static void UiService_DrawDetailTemperature(const AppState *state)
{
	char line[32];
	u8 percent=0;
	UiService_ClearRect(24,96,296,164,UI_CARD);
	if(state->dht_valid)
	{
		sprintf(line,"TEMPERATURE             %u C",
		        (unsigned int)state->temperature);
		percent=(state->temperature>=50)?100:(u8)(state->temperature*2);
	}
	else sprintf(line,"TEMPERATURE             --");
	UiService_Text(24,96,16,line,state->dht_valid?UI_TEXT:UI_ERROR,UI_CARD);
	UiService_ProgressBar(122,percent,UiService_TemperatureColor(state));
	UiService_Text(24,148,12,
	               !state->dht_valid?"SENSOR ERROR":
	               (state->temperature<18?"LOW":
	                (state->temperature>=30?"HIGH":"NORMAL RANGE")),
	               UiService_TemperatureColor(state),UI_CARD);
}

static void UiService_DrawDetailHumidity(const AppState *state)
{
	char line[32];
	u8 percent=state->dht_valid?state->humidity:0;
	UiService_ClearRect(24,180,296,248,UI_CARD);
	if(state->dht_valid)
		sprintf(line,"HUMIDITY                %u %%",
		        (unsigned int)state->humidity);
	else sprintf(line,"HUMIDITY                --");
	UiService_Text(24,180,16,line,state->dht_valid?UI_TEXT:UI_ERROR,UI_CARD);
	UiService_ProgressBar(206,percent,UiService_HumidityColor(state));
	UiService_Text(24,232,12,
	               !state->dht_valid?"SENSOR ERROR":
	               (state->humidity<35?"LOW":
	                (state->humidity>80?"HIGH":"NORMAL RANGE")),
	               UiService_HumidityColor(state),UI_CARD);
}

static void UiService_DrawDetailLight(const AppState *state)
{
	char line[32];
	u8 percent=state->light_valid?state->light:0;
	UiService_ClearRect(24,264,296,332,UI_CARD);
	if(state->light_valid)
		sprintf(line,"LIGHT                   %u %%",
		        (unsigned int)state->light);
	else sprintf(line,"LIGHT                   --");
	UiService_Text(24,264,16,line,state->light_valid?UI_TEXT:UI_ERROR,UI_CARD);
	UiService_ProgressBar(290,percent,UiService_LightColor(state));
	UiService_Text(24,316,12,
	               state->light_valid?AppState_LightLevel(state->light):
	               "SENSOR ERROR",UiService_LightColor(state),UI_CARD);
}

static void UiService_DrawDetailOverall(const AppState *state)
{
	const char *text;
	u16 color;
	UiService_ClearRect(24,352,296,400,UI_CARD);
	if(!state->dht_valid || !state->light_valid)
	{
		text="OVERALL: SENSOR ERROR";
		color=UI_ERROR;
	}
	else if(UiService_PlantMode(state)==UI_PLANT_ATTENTION)
	{
		text="OVERALL: NEEDS ATTENTION";
		color=UI_WARN;
	}
	else
	{
		text="OVERALL: GOOD";
		color=UI_OK;
	}
	UiService_Pill(42,360,278,394,text,color,
	               color==UI_WARN?UI_TEXT:WHITE);
}

static void UiService_RenderDetailFull(const AppState *state)
{
	UiService_Base("ENVIRONMENT DETAIL",APP_PAGE_DETAIL);
	UiService_Text(24,76,12,"LIVE SENSOR LEVELS",UI_MUTED,UI_CARD);
	UiService_DrawDetailTemperature(state);
	UiService_DrawDetailHumidity(state);
	UiService_DrawDetailLight(state);
	UiService_DrawDetailOverall(state);
}

static void UiService_RenderDetailChanged(const AppState *state)
{
	if(state->dht_valid!=g_last_state.dht_valid ||
	   state->temperature!=g_last_state.temperature)
		UiService_DrawDetailTemperature(state);
	if(state->dht_valid!=g_last_state.dht_valid ||
	   state->humidity!=g_last_state.humidity)
		UiService_DrawDetailHumidity(state);
	if(state->light_valid!=g_last_state.light_valid ||
	   state->light!=g_last_state.light)
		UiService_DrawDetailLight(state);
	if(state->dht_valid!=g_last_state.dht_valid ||
	   state->light_valid!=g_last_state.light_valid ||
	   state->temperature!=g_last_state.temperature ||
	   state->humidity!=g_last_state.humidity ||
	   state->light!=g_last_state.light)
		UiService_DrawDetailOverall(state);
}

static void UiService_DrawAiProgress(const AppState *state)
{
	u8 i;
	u8 level=0;
	u16 color=UI_WARN;
	u16 x1;

	if(state->ai_state==APP_AI_SENDING || state->ai_state==APP_AI_PREPARING)level=1;
	else if(state->ai_state==APP_AI_THINKING)level=2;
	else if(state->ai_state==APP_AI_VALIDATING)level=3;
	else if(state->ai_state==APP_AI_DONE)
	{
		level=4;
		color=UI_OK;
	}
	else if(state->ai_state==APP_AI_ERROR || state->ai_state==APP_AI_TIMEOUT)
	{
		level=4;
		color=UI_ERROR;
	}

	for(i=0;i<4;i++)
	{
		x1=(u16)(24+i*70);
		UiService_FillRoundRect(x1,142,(u16)(x1+60),158,8,
		                       (i<level)?color:UI_DISABLED);
	}
}

static void UiService_DrawAiState(const AppState *state)
{
	char line[48];
	UiService_ClearRect(24,100,296,130,UI_CARD);
	sprintf(line,"AI: %s",AppState_AiStateText(state->ai_state));
	UiService_TextCentered(24,296,102,24,line,UiService_AiColor(state),UI_CARD);
	UiService_DrawAiProgress(state);
}

static void UiService_DrawAiDetails(const AppState *state)
{
	char line[48];
	UiService_ClearRect(24,170,296,348,UI_CARD);
	sprintf(line,"REQ #%u  %s/%s",(unsigned int)state->ai_request_id,
	        state->device_id,state->plant_name);
	UiService_Text(28,170,16,line,UI_MUTED,UI_CARD);
	sprintf(line,"STATUS: %s",state->ai_status);
	UiService_Text(28,202,16,line,UI_TEXT,UI_CARD);
	sprintf(line,"ISSUE: %s",state->ai_issue);
	UiService_Text(28,234,16,line,UI_TEXT,UI_CARD);
	sprintf(line,"WATER: %s",state->ai_watering);
	UiService_Text(28,266,16,line,UI_TEXT,UI_CARD);
	sprintf(line,"ADVICE: %s",state->ai_advice);
	UiService_Text(28,298,16,line,UI_TEXT,UI_CARD);
	sprintf(line,"ERROR: %s",state->last_error);
	UiService_Text(28,330,12,line,
	               (state->ai_state==APP_AI_ERROR ||
	                state->ai_state==APP_AI_TIMEOUT)?UI_ERROR:UI_MUTED,UI_CARD);
}

static void UiService_DrawAiButtons(const AppState *state)
{
	u8 active=UiService_AiActive(state);
	UiService_Button(18,362,151,416,active?"WORKING":"ANALYZE",
	                 UI_HEADER,(u8)!active);
	UiService_Button(169,362,302,416,"RETRY",UI_WARN,(u8)!active);
}

static void UiService_RenderAiFull(const AppState *state)
{
	UiService_Base("AI PLANT ASSISTANT",APP_PAGE_AI);
	UiService_Pill(24,74,170,96,
	               state->gateway_ready?"GATEWAY READY":"GATEWAY OFFLINE",
	               state->gateway_ready?UI_OK:UI_WARN,
	               state->gateway_ready?WHITE:UI_TEXT);
	UiService_Pill(180,74,296,96,state->plant_name,
	               UI_GREEN_LIGHT,UI_GREEN_DARK);
	UiService_DrawAiState(state);
	UiService_DrawAiDetails(state);
	UiService_DrawAiButtons(state);
}

static void UiService_RenderAiChanged(const AppState *state)
{
	if(state->gateway_ready!=g_last_state.gateway_ready)
	{
		UiService_ClearRect(20,72,176,98,UI_CARD);
		UiService_Pill(24,74,170,96,
		               state->gateway_ready?"GATEWAY READY":"GATEWAY OFFLINE",
		               state->gateway_ready?UI_OK:UI_WARN,
		               state->gateway_ready?WHITE:UI_TEXT);
	}
	if(state->ai_state!=g_last_state.ai_state)UiService_DrawAiState(state);
	if(state->ai_request_id!=g_last_state.ai_request_id ||
	   strcmp(state->ai_status,g_last_state.ai_status)!=0 ||
	   strcmp(state->ai_issue,g_last_state.ai_issue)!=0 ||
	   strcmp(state->ai_watering,g_last_state.ai_watering)!=0 ||
	   strcmp(state->ai_advice,g_last_state.ai_advice)!=0 ||
	   strcmp(state->last_error,g_last_state.last_error)!=0 ||
	   state->ai_state!=g_last_state.ai_state)
		UiService_DrawAiDetails(state);
	if(state->ai_state!=g_last_state.ai_state)UiService_DrawAiButtons(state);
}

static u16 UiService_PlantAccent(const char *species_id)
{
	if(strcmp(species_id,"mint")==0)return 0x4E49;
	if(strcmp(species_id,"cactus")==0)return 0x2CE6;
	if(strcmp(species_id,"orchid")==0)return 0xE35C;
	if(strcmp(species_id,"succulent")==0)return 0x65AD;
	if(strcmp(species_id,"tomato")==0)return 0xF2C3;
	return UI_GREEN;
}

static void UiService_DrawPlantMiniIcon(u16 x,u16 y,const char *species_id)
{
	u16 leaf=UiService_PlantAccent(species_id);
	UiService_Pixel((u16)(x+10),y,8,8,leaf);
	UiService_Pixel((u16)(x+2),(u16)(y+8),9,8,leaf);
	UiService_Pixel((u16)(x+17),(u16)(y+8),9,8,leaf);
	UiService_Pixel((u16)(x+12),(u16)(y+12),4,17,UI_GREEN_DARK);
	UiService_Pixel((u16)(x+6),(u16)(y+27),16,7,UI_POT);
	UiService_Pixel((u16)(x+9),(u16)(y+34),10,5,UI_POT_DARK);
}

static void UiService_DrawPlantLibraryCard(const AppState *state,u8 index)
{
	u16 x1;
	u16 x2;
	u16 y1;
	u16 y2;
	const AppPlantListItem *item;
	u8 current;

	x1=(index%2==0)?18:169;
	x2=(index%2==0)?151:302;
	y1=(u16)(88+(index/2)*90);
	y2=(u16)(y1+78);
	item=&state->plant_items[index];
	if(!item->valid)
	{
		UiService_FillRoundRect(x1,y1,x2,y2,12,UI_DISABLED);
		UiService_TextCentered(x1,x2,(u16)(y1+31),12,"EMPTY",
		                       UI_MUTED,UI_DISABLED);
		return;
	}

	current=(strcmp(item->species_id,state->species_id)==0)?1:0;
	UiService_Card(x1,y1,x2,y2);
	UiService_DrawPlantMiniIcon((u16)(x1+9),(u16)(y1+12),item->species_id);
	UiService_Text((u16)(x1+45),(u16)(y1+12),16,item->display_name,
	               UI_TEXT,UI_CARD);
	UiService_Text((u16)(x1+45),(u16)(y1+35),12,item->source_type,
	               UI_MUTED,UI_CARD);
	if(current)
		UiService_Pill((u16)(x1+43),(u16)(y1+53),(u16)(x2-7),
		               (u16)(y1+72),"CURRENT",UI_OK,WHITE);
	else
		UiService_Text((u16)(x1+45),(u16)(y1+57),12,"VIEW PROFILE",
		               UI_GREEN_DARK,UI_CARD);
}

static void UiService_DrawPlantLibraryContent(const AppState *state)
{
	u8 i;
	if(state->plant_list_state==APP_PLANT_DATA_LOADING)
	{
		UiService_TextCentered(20,300,180,24,"LOADING PLANTS...",
		                       UI_WARN,UI_CARD);
		UiService_TextCentered(20,300,216,12,"WAITING FOR GATEWAY",
		                       UI_MUTED,UI_CARD);
		return;
	}
	if(state->plant_list_state==APP_PLANT_DATA_ERROR)
	{
		UiService_TextCentered(20,300,170,24,"PLANT LIST ERROR",
		                       UI_ERROR,UI_CARD);
		UiService_TextCentered(20,300,210,16,state->plant_error,
		                       UI_ERROR,UI_CARD);
		UiService_TextCentered(20,300,244,12,"CHECK GATEWAY AND RETRY",
		                       UI_MUTED,UI_CARD);
		return;
	}
	if(state->plant_list_state!=APP_PLANT_DATA_READY)
	{
		UiService_TextCentered(20,300,190,24,"NO PLANT DATA",
		                       UI_MUTED,UI_CARD);
		return;
	}

	for(i=0;i<APP_PLANT_LIST_MAX;i++)
		UiService_DrawPlantLibraryCard(state,i);
}

static void UiService_RenderPlantLibraryFull(const AppState *state)
{
	char line[40];
	UiService_Base("PLANT LIBRARY",APP_PAGE_PLANT_LIBRARY);
	if(state->plant_list_state==APP_PLANT_DATA_READY)
	{
		sprintf(line,"%u PLANTS FROM SQLITE",
		        (unsigned int)state->plant_list_count);
		UiService_TextCentered(20,300,70,12,line,UI_MUTED,UI_CARD);
	}
	else
		UiService_TextCentered(20,300,70,12,"DATABASE-DRIVEN PROFILES",
		                       UI_MUTED,UI_CARD);
	UiService_DrawPlantLibraryContent(state);
	UiService_Button(18,362,151,416,"BACK",UI_GREEN_DARK,1);
	UiService_Button(169,362,302,416,"REFRESH",UI_HEADER,
	                 state->gateway_ready);
}

static void UiService_RenderPlantLibraryChanged(const AppState *state)
{
	if(state->plant_ui_revision!=g_last_state.plant_ui_revision ||
	   state->gateway_ready!=g_last_state.gateway_ready)
		UiService_RenderPlantLibraryFull(state);
}

static void UiService_DrawPlantProfileContent(const AppState *state)
{
	char line[48];
	u8 is_current;

	if(state->plant_detail_state==APP_PLANT_DATA_LOADING)
	{
		UiService_TextCentered(20,300,180,24,"LOADING PROFILE...",
		                       UI_WARN,UI_CARD);
		UiService_TextCentered(20,300,220,16,state->plant_detail_name,
		                       UI_TEXT,UI_CARD);
		return;
	}
	if(state->plant_detail_state==APP_PLANT_DATA_ERROR)
	{
		UiService_TextCentered(20,300,170,24,"PROFILE ERROR",
		                       UI_ERROR,UI_CARD);
		UiService_TextCentered(20,300,210,16,state->plant_error,
		                       UI_ERROR,UI_CARD);
		return;
	}
	if(state->plant_detail_state!=APP_PLANT_DATA_READY)return;

	is_current=(strcmp(state->species_id,state->plant_detail_species_id)==0)?1:0;
	UiService_TextCentered(20,300,78,24,state->plant_detail_name,
	                       UI_GREEN_DARK,UI_CARD);
	UiService_Pill(112,108,208,132,state->plant_detail_source,
	               UI_GREEN_LIGHT,UI_GREEN_DARK);
	sprintf(line,"SPECIES ID: %s",state->plant_detail_species_id);
	UiService_TextCentered(20,300,142,12,line,UI_MUTED,UI_CARD);

	UiService_FillRoundRect(28,172,292,214,10,UI_HERO);
	sprintf(line,"TEMPERATURE       %u - %u C",
	        (unsigned int)state->plant_temp_min,
	        (unsigned int)state->plant_temp_max);
	UiService_Text(40,185,16,line,UI_TEXT,UI_HERO);

	UiService_FillRoundRect(28,224,292,266,10,UI_HERO);
	sprintf(line,"HUMIDITY          %u - %u %%",
	        (unsigned int)state->plant_humidity_min,
	        (unsigned int)state->plant_humidity_max);
	UiService_Text(40,237,16,line,UI_TEXT,UI_HERO);

	UiService_FillRoundRect(28,276,292,318,10,UI_HERO);
	sprintf(line,"LIGHT             %u - %u %%",
	        (unsigned int)state->plant_light_min,
	        (unsigned int)state->plant_light_max);
	UiService_Text(40,289,16,line,UI_TEXT,UI_HERO);

	UiService_Pill(76,330,244,352,
	               state->plant_selection_pending?"SAVING TO DEVICE":
	               (is_current?"CURRENT PLANT":"READY TO USE"),
	               state->plant_selection_pending?UI_WARN:
	               (is_current?UI_OK:UI_HEADER),
	               state->plant_selection_pending?UI_TEXT:WHITE);
}

static void UiService_RenderPlantProfileFull(const AppState *state)
{
	u8 is_current;
	u8 use_enabled;
	const char *use_label;

	UiService_Base("PLANT PROFILE",APP_PAGE_PLANT_PROFILE);
	UiService_DrawPlantProfileContent(state);
	UiService_Button(18,362,151,416,"BACK",UI_GREEN_DARK,1);

	is_current=(strcmp(state->species_id,state->plant_detail_species_id)==0)?1:0;
	use_enabled=(state->plant_detail_state==APP_PLANT_DATA_READY &&
	             !state->plant_selection_pending && state->gateway_ready &&
	             !is_current)?1:0;
	if(state->plant_selection_pending)use_label="SAVING";
	else if(is_current)use_label="CURRENT";
	else use_label="USE PLANT";
	UiService_Button(169,362,302,416,use_label,UI_HEADER,use_enabled);
}

static void UiService_RenderPlantProfileChanged(const AppState *state)
{
	if(state->plant_ui_revision!=g_last_state.plant_ui_revision ||
	   state->gateway_ready!=g_last_state.gateway_ready)
		UiService_RenderPlantProfileFull(state);
}

static void UiService_DrawSystemFields(const AppState *state)
{
	char line[48];
	UiService_ClearRect(24,96,296,246,UI_CARD);
	sprintf(line,"WIFI: %s",state->wifi_status);
	UiService_Text(28,98,16,line,state->wifi_connected?UI_OK:UI_WARN,UI_CARD);
	sprintf(line,"SSID: %s",APP_WIFI_SSID);
	UiService_Text(28,124,16,line,UI_TEXT,UI_CARD);
	sprintf(line,"DEVICE: %s / %s",state->device_id,state->plant_name);
	UiService_Text(28,150,16,line,UI_TEXT,UI_CARD);
	sprintf(line,"STA IP: %s",state->sta_ip);
	UiService_Text(28,176,16,line,UI_TEXT,UI_CARD);
	UiService_Text(28,202,16,state->tcp_connected?"TCP: CONNECTED":"TCP: WAITING",
	               state->tcp_connected?UI_OK:UI_WARN,UI_CARD);
	UiService_Text(28,228,16,state->gateway_ready?"GATEWAY: READY":"GATEWAY: OFFLINE",
	               state->gateway_ready?UI_OK:UI_WARN,UI_CARD);
}

static void UiService_DrawSystemSafety(const AppState *state)
{
	char line[48];
	UiService_ClearRect(24,250,296,342,UI_CARD);
	UiService_Card(24,250,296,342);
	UiService_Text(34,262,16,"PUMP: NOT CONNECTED",UI_ERROR,UI_CARD);
	UiService_Text(34,288,12,"OUTPUT COMMANDS DISABLED",UI_WARN,UI_CARD);
	sprintf(line,"LAST ERROR: %s",state->last_error);
	UiService_Text(34,316,12,line,
	               strcmp(state->last_error,"NONE")==0?UI_MUTED:UI_ERROR,UI_CARD);
}

static void UiService_DrawSystemButtons(const AppState *state)
{
	UiService_Button(18,362,151,416,
	                 state->wifi_connected?"CHECK":"RETRY WIFI",
	                 UI_HEADER,1);
	UiService_Button(169,362,302,416,"CALIBRATE",UI_GREEN_DARK,1);
}

static void UiService_RenderSystemFull(const AppState *state)
{
	UiService_Base("SYSTEM & SAFETY",APP_PAGE_SYSTEM);
	UiService_Text(24,76,12,"CONNECTION AND OUTPUT STATUS",UI_MUTED,UI_CARD);
	UiService_DrawSystemFields(state);
	UiService_DrawSystemSafety(state);
	UiService_DrawSystemButtons(state);
}

static void UiService_RenderSystemChanged(const AppState *state)
{
	if(state->wifi_connected!=g_last_state.wifi_connected ||
	   strcmp(state->wifi_status,g_last_state.wifi_status)!=0 ||
	   strcmp(state->sta_ip,g_last_state.sta_ip)!=0 ||
	   state->tcp_connected!=g_last_state.tcp_connected ||
	   state->gateway_ready!=g_last_state.gateway_ready)
		UiService_DrawSystemFields(state);
	if(strcmp(state->last_error,g_last_state.last_error)!=0)
		UiService_DrawSystemSafety(state);
	if(state->wifi_connected!=g_last_state.wifi_connected)
		UiService_DrawSystemButtons(state);
}

void UiService_Init(void)
{
	TFTLCD_Init();
	LCD_LED=1;
	g_render_cache_valid=0;
}

void UiService_Invalidate(void)
{
	g_render_cache_valid=0;
}

void UiService_Render(const AppState *state)
{
	if(!g_render_cache_valid || state->page!=g_last_state.page)
	{
		switch(state->page)
		{
			case APP_PAGE_DETAIL:
				UiService_RenderDetailFull(state);
				break;
			case APP_PAGE_AI:
				UiService_RenderAiFull(state);
				break;
			case APP_PAGE_SYSTEM:
				UiService_RenderSystemFull(state);
				break;
			case APP_PAGE_PLANT_LIBRARY:
				UiService_RenderPlantLibraryFull(state);
				break;
			case APP_PAGE_PLANT_PROFILE:
				UiService_RenderPlantProfileFull(state);
				break;
			default:
				UiService_RenderHomeFull(state);
				break;
		}
	}
	else
	{
		switch(state->page)
		{
			case APP_PAGE_DETAIL:
				UiService_RenderDetailChanged(state);
				break;
			case APP_PAGE_AI:
				UiService_RenderAiChanged(state);
				break;
			case APP_PAGE_SYSTEM:
				UiService_RenderSystemChanged(state);
				break;
			case APP_PAGE_PLANT_LIBRARY:
				UiService_RenderPlantLibraryChanged(state);
				break;
			case APP_PAGE_PLANT_PROFILE:
				UiService_RenderPlantProfileChanged(state);
				break;
			default:
				UiService_RenderHomeChanged(state);
				break;
		}
	}

	g_last_state=*state;
	g_render_cache_valid=1;
}
