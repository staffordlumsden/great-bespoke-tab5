/* TAB5 Console SD-app installer: validate and install OTA candidates from SD. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TAB5_INSTALLER_PATH_MAX 256
#define TAB5_INSTALLER_PROJECT_NAME_MAX 32
#define TAB5_INSTALLER_VERSION_MAX 24
#define TAB5_INSTALLER_SHA256_HEX_LEN 65
#define TAB5_INSTALLER_MAX_PACKAGES 32

typedef enum {
    TAB5_INSTALLER_IDLE = 0,
    TAB5_INSTALLER_VALIDATING,
    TAB5_INSTALLER_WRITING,
    TAB5_INSTALLER_VERIFYING,
    TAB5_INSTALLER_SELECTING,
    TAB5_INSTALLER_DONE,
    TAB5_INSTALLER_FAILED,
} tab5_installer_state_t;

typedef struct {
    char project[TAB5_INSTALLER_PROJECT_NAME_MAX];
    char version[TAB5_INSTALLER_VERSION_MAX];
    char json_path[TAB5_INSTALLER_PATH_MAX];
    char image_path[TAB5_INSTALLER_PATH_MAX];
    char license_path[TAB5_INSTALLER_PATH_MAX];
    uint32_t size_bytes;
    char sha256[TAB5_INSTALLER_SHA256_HEX_LEN];
    bool valid;
} tab5_installer_package_t;

typedef struct {
    tab5_installer_package_t packages[TAB5_INSTALLER_MAX_PACKAGES];
    size_t count;
} tab5_installer_catalog_t;

typedef struct {
    tab5_installer_state_t state;
    uint8_t progress;   /* 0-100, meaningful during write/verify */
    char message[80];
} tab5_installer_status_t;

typedef void (*tab5_installer_progress_cb_t)(const tab5_installer_status_t *, void *);
/* Register before starting the worker. Callback must only post a UI event. */
void tab5_installer_set_progress_callback(tab5_installer_progress_cb_t callback, void *user);

/* Scan /sdcard/firmware/ for .json package manifests and populate a catalog.
 * Only parses manifests; it does not read image data or touch flash. */
esp_err_t tab5_installer_scan(tab5_installer_catalog_t *catalog);

/* Validate the package image against Tab5 gates. Sets package->valid and fills
 * package->size_bytes and package->sha256 on success. Does not write flash.
 * The combined installer invokes this automatically; no manual UI step. */
esp_err_t tab5_installer_validate(tab5_installer_package_t *package);

/* Install the package into the inactive OTA slot, verify the readback, and set
 * the bootloader to boot it once. The package need not be prevalidated.
 * Preserves factory and NVS. Returns the index
 * of the slot actually written (0 or 1) in *slot, or -1 if unavailable. */
esp_err_t tab5_installer_install(const tab5_installer_package_t *package,
                                 tab5_installer_status_t *status, int *slot);

/* App only: after first-frame health checks, confirm pending OTA and select
 * Console for the next reset. Console cannot confirm the app it installed. */
esp_err_t tab5_installer_confirm_running(void);

/* Return to the factory Console partition without erasing NVS. */
esp_err_t tab5_installer_return_to_factory(void);

/* Get a short human-readable status snapshot. */
void tab5_installer_get_status(tab5_installer_status_t *status);

#ifdef __cplusplus
}
#endif
