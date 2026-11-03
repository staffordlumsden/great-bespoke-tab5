#pragma once
#include "tab5_radio.h"

#ifdef __cplusplus
extern "C" {
#endif
esp_err_t tab5_bluetooth_init(void);
void tab5_bluetooth_get_status(tab5_radio_status_t *out);
#ifdef __cplusplus
}
#endif
