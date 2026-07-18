#ifndef _UI_SERVICE_H
#define _UI_SERVICE_H

#include "app_state.h"

void UiService_Init(void);
void UiService_Render(const AppState *state);
void UiService_Invalidate(void);
u16 UiService_AiDialogMaxScroll(void);

#endif
