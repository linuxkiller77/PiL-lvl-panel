#pragma once

#include "lvgl.h"
#include "app/app_state.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_demo_create(lv_display_t *display);
void ui_demo_update_from_state(const app_state_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif
