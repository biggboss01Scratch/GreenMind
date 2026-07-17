#include "touch_service.h"
#include "touch.h"
#include "tftlcd.h"

#define TOUCH_NAV_Y1          432
#define TOUCH_ACTION_Y1       362
#define TOUCH_ACTION_Y2       416

static u8 g_touch_was_pressed=0;
static u8 g_touch_has_point=0;
static u16 g_touch_x=0;
static u16 g_touch_y=0;

static u8 TouchService_InRect(u16 x,u16 y,u16 x1,u16 y1,u16 x2,u16 y2)
{
	return (x>=x1 && x<=x2 && y>=y1 && y<=y2)?1:0;
}

static AppTouchEvent TouchService_HitTest(AppPage page,u16 x,u16 y)
{
	if(y>=TOUCH_NAV_Y1)
	{
		if(x<80)return APP_TOUCH_EVENT_HOME;
		if(x<160)return APP_TOUCH_EVENT_DETAIL;
		if(x<240)return APP_TOUCH_EVENT_AI_PAGE;
		return APP_TOUCH_EVENT_SYSTEM;
	}

	if(page==APP_PAGE_HOME &&
	   TouchService_InRect(x,y,60,68,260,243))
		return APP_TOUCH_EVENT_PLANT_LIBRARY;

	if(page==APP_PAGE_HOME &&
	   TouchService_InRect(x,y,18,TOUCH_ACTION_Y1,151,TOUCH_ACTION_Y2))
		return APP_TOUCH_EVENT_DETAIL;

	if(page==APP_PAGE_HOME &&
	   TouchService_InRect(x,y,169,TOUCH_ACTION_Y1,302,TOUCH_ACTION_Y2))
		return APP_TOUCH_EVENT_AI_REQUEST;

	if(page==APP_PAGE_AI &&
	   TouchService_InRect(x,y,18,TOUCH_ACTION_Y1,151,TOUCH_ACTION_Y2))
		return APP_TOUCH_EVENT_AI_REQUEST;

	if(page==APP_PAGE_AI &&
	   TouchService_InRect(x,y,169,TOUCH_ACTION_Y1,302,TOUCH_ACTION_Y2))
		return APP_TOUCH_EVENT_AI_REQUEST;

	if(page==APP_PAGE_SYSTEM &&
	   TouchService_InRect(x,y,18,TOUCH_ACTION_Y1,151,TOUCH_ACTION_Y2))
		return APP_TOUCH_EVENT_NETWORK_RETRY;

	if(page==APP_PAGE_SYSTEM &&
	   TouchService_InRect(x,y,169,TOUCH_ACTION_Y1,302,TOUCH_ACTION_Y2))
		return APP_TOUCH_EVENT_CALIBRATE;

	if(page==APP_PAGE_PLANT_LIBRARY)
	{
		if(TouchService_InRect(x,y,18,88,151,166))
			return APP_TOUCH_EVENT_PLANT_ITEM_0;
		if(TouchService_InRect(x,y,169,88,302,166))
			return APP_TOUCH_EVENT_PLANT_ITEM_1;
		if(TouchService_InRect(x,y,18,178,151,256))
			return APP_TOUCH_EVENT_PLANT_ITEM_2;
		if(TouchService_InRect(x,y,169,178,302,256))
			return APP_TOUCH_EVENT_PLANT_ITEM_3;
		if(TouchService_InRect(x,y,18,268,151,346))
			return APP_TOUCH_EVENT_PLANT_ITEM_4;
		if(TouchService_InRect(x,y,169,268,302,346))
			return APP_TOUCH_EVENT_PLANT_ITEM_5;
		if(TouchService_InRect(x,y,18,TOUCH_ACTION_Y1,151,TOUCH_ACTION_Y2))
			return APP_TOUCH_EVENT_PLANT_BACK;
		if(TouchService_InRect(x,y,169,TOUCH_ACTION_Y1,302,TOUCH_ACTION_Y2))
			return APP_TOUCH_EVENT_PLANT_REFRESH;
	}

	if(page==APP_PAGE_PLANT_PROFILE)
	{
		if(TouchService_InRect(x,y,18,TOUCH_ACTION_Y1,151,TOUCH_ACTION_Y2))
			return APP_TOUCH_EVENT_PLANT_BACK;
		if(TouchService_InRect(x,y,169,TOUCH_ACTION_Y1,302,TOUCH_ACTION_Y2))
			return APP_TOUCH_EVENT_PLANT_USE;
	}

	return APP_TOUCH_EVENT_NONE;
}

void TouchService_Init(void)
{
	TP_Init();
	g_touch_was_pressed=0;
	g_touch_has_point=0;
}

AppTouchEvent TouchService_Task(AppPage page)
{
	if(TP_Scan(0))
	{
		g_touch_was_pressed=1;
		if(tp_dev.x[0]<tftlcd_data.width && tp_dev.y[0]<tftlcd_data.height)
		{
			g_touch_x=tp_dev.x[0];
			g_touch_y=tp_dev.y[0];
			g_touch_has_point=1;
		}
		return APP_TOUCH_EVENT_NONE;
	}

	if(g_touch_was_pressed)
	{
		g_touch_was_pressed=0;
		if(g_touch_has_point)
		{
			g_touch_has_point=0;
			return TouchService_HitTest(page,g_touch_x,g_touch_y);
		}
	}

	return APP_TOUCH_EVENT_NONE;
}

void TouchService_Adjust(void)
{
	TP_Adjust();
	g_touch_was_pressed=0;
	g_touch_has_point=0;
}
