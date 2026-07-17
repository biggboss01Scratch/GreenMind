#include "key_service.h"

#define KEY_DEBOUNCE_TICKS  3

typedef struct
{
	u8 stable;
	u8 counter;
} DebounceKey;

static DebounceKey g_page_key={0,0};
static DebounceKey g_ai_key={0,0};

static u8 KeyService_Update(DebounceKey *key,u8 pressed)
{
	if(pressed==key->stable)
	{
		key->counter=0;
		return 0;
	}
	if(key->counter<KEY_DEBOUNCE_TICKS)key->counter++;
	if(key->counter>=KEY_DEBOUNCE_TICKS)
	{
		key->counter=0;
		key->stable=pressed;
		if(pressed)return 1;
	}
	return 0;
}

void KeyService_Init(void)
{
	GPIO_InitTypeDef gpio;
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA|RCC_APB2Periph_GPIOE,ENABLE);

	gpio.GPIO_Pin=GPIO_Pin_0;
	gpio.GPIO_Mode=GPIO_Mode_IPD;
	gpio.GPIO_Speed=GPIO_Speed_50MHz;
	GPIO_Init(GPIOA,&gpio);

	gpio.GPIO_Pin=GPIO_Pin_2|GPIO_Pin_3|GPIO_Pin_4;
	gpio.GPIO_Mode=GPIO_Mode_IPU;
	GPIO_Init(GPIOE,&gpio);
}

AppKeyEvent KeyService_Task(void)
{
	u8 page_pressed=(GPIO_ReadInputDataBit(GPIOA,GPIO_Pin_0)==Bit_SET)?1:0;
	u8 ai_pressed=(GPIO_ReadInputDataBit(GPIOE,GPIO_Pin_3)==Bit_RESET)?1:0;

	if(KeyService_Update(&g_page_key,page_pressed))return APP_KEY_EVENT_PAGE;
	if(KeyService_Update(&g_ai_key,ai_pressed))return APP_KEY_EVENT_AI_REQUEST;
	return APP_KEY_EVENT_NONE;
}
