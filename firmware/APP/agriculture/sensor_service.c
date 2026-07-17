#include "sensor_service.h"
#include "app_config.h"
#include "dht11.h"
#include "lsens.h"

static u16 g_dht_timer=0;
static u16 g_light_timer=0;
static u8 g_dht_initialized=0;

void SensorService_Init(AppState *state)
{
	Lsens_Init();
	state->light_valid=1;
	g_light_timer=0;

	if(DHT11_Init()==0)g_dht_initialized=1;
	else
	{
		g_dht_initialized=0;
		state->dht_fail_count=1;
	}
	state->dht_valid=0;
	g_dht_timer=100;
	state->ui_dirty=1;
}

void SensorService_Task(AppState *state)
{
	u8 temperature;
	u8 humidity;
	u8 light;
	u8 previous_valid;

	if(g_light_timer==0)
	{
		light=Lsens_Get_Val();
		if(light!=state->light || !state->light_valid)
		{
			state->light=light;
			state->light_valid=1;
			state->ui_dirty=1;
		}
		g_light_timer=APP_LIGHT_PERIOD_TICKS;
	}
	else g_light_timer--;

	if(g_dht_timer==0)
	{
		previous_valid=state->dht_valid;
		if(!g_dht_initialized)
		{
			if(DHT11_Init()==0)g_dht_initialized=1;
		}

		if(g_dht_initialized && DHT11_Read_Data(&temperature,&humidity)==0)
		{
			if(temperature!=state->temperature || humidity!=state->humidity ||
			   !state->dht_valid)state->ui_dirty=1;
			state->temperature=temperature;
			state->humidity=humidity;
			state->dht_valid=1;
			state->dht_fail_count=0;
		}
		else
		{
			if(state->dht_fail_count<255)state->dht_fail_count++;
			if(state->dht_fail_count>=3)state->dht_valid=0;
			if(previous_valid!=state->dht_valid)state->ui_dirty=1;
			g_dht_initialized=0;
		}
		g_dht_timer=APP_DHT_PERIOD_TICKS;
	}
	else g_dht_timer--;
}
