#ifndef _APP_CONFIG_H
#define _APP_CONFIG_H

#include "system.h"
#include "app_secrets.h"

#define APP_WIFI_SSID                  "0Range777"
#define APP_WIFI_AP_SSID               "ESP07"
#define APP_WIFI_AP_PASSWORD           "00000000"
#define APP_WIFI_TCP_PORT              8080

#define APP_TASK_PERIOD_MS             10
#define APP_DHT_PERIOD_TICKS           200
#define APP_LIGHT_PERIOD_TICKS         100
#define APP_UI_REFRESH_MIN_TICKS       10
#define APP_AI_TIMEOUT_TICKS           4500

#define APP_LIGHT_DARK_MAX             29
#define APP_LIGHT_NORMAL_MAX           69

#define APP_PROTOCOL_FRAME_MAX         120
#define APP_PROTOCOL_EVENT_QUEUE_SIZE  8
#define APP_TX_QUEUE_SIZE              3

#endif
