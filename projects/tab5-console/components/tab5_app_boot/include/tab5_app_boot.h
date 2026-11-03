/* Shared lifecycle for resident Console and applications loaded from SD. */
#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "esp_partition.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TAB5_CONSOLE_ADDRESS 0x10000
#define TAB5_CONSOLE_SIZE 0x400000
#define TAB5_OTA_DATA_ADDRESS 0x410000
#define TAB5_APP_SLOT_SIZE 0x500000

/* Checks the full layout, physical flash size and resident Console identity.
 * No writes. A legacy factory-only device fails this check. */
esp_err_t tab5_app_boot_check_layout(void);
bool tab5_app_boot_console_available(void);

/* Console only: select Console as the recovery boot target and choose the
 * alternate app slot. Clears only otadata, leaving NVS and app images intact. */
esp_err_t tab5_app_boot_prepare_launch(const esp_partition_t **slot);
/* Persist slot rotation and select a verified candidate for its first boot. */
esp_err_t tab5_app_boot_select(const esp_partition_t *slot);

/* App: after successful hardware checks AND first-frame presentation, confirm
 * a pending OTA boot, then select Console for the next reset/power cycle.
 * Returns an error without confirming when hardware_ok is false. */
esp_err_t tab5_app_boot_ready(bool hardware_ok);
/* App: select verified Console and restart. Does not erase NVS or app slots. */
esp_err_t tab5_app_boot_return_to_console(void);

#ifdef __cplusplus
}
#endif
