#include "tab5_app_boot.h"
#include "test_boot_sdk.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static esp_partition_t parts[] = {
    {1, 2, 0x9000, 0x6000, false}, {1, 1, 0xf000, 0x1000, false},
    {0, 0, 0x10000, 0x400000, false}, {1, 0, 0x410000, 0x2000, false},
    {0, 16, 0x420000, 0x500000, false}, {0, 17, 0x920000, 0x500000, false},
};
static const esp_partition_t *running, *selected;
static int missing, calls, confirmed_at, selected_at, restarts;
static esp_err_t select_error, confirm_error, nvs_error, state_error;
static uint32_t flash_size;
static uint8_t last_slot;
static bool saved;
static const char *factory_name;
static esp_ota_img_states_t image_state;

static void reset(void)
{
    running = &parts[2]; selected = NULL; missing = -1;
    calls = confirmed_at = selected_at = restarts = 0;
    select_error = confirm_error = nvs_error = state_error = ESP_OK;
    flash_size = 0x1000000; last_slot = 1; saved = false;
    factory_name = "tab5_console"; image_state = ESP_OTA_IMG_PENDING_VERIFY;
}
const esp_partition_t *esp_partition_find_first(int type, int subtype, const char *label)
{
    assert(label == NULL);
    for (unsigned i = 0; i < sizeof(parts) / sizeof(parts[0]); ++i)
        if ((int)i != missing && parts[i].type == type && parts[i].subtype == subtype)
            return &parts[i];
    return NULL;
}
const esp_partition_t *esp_ota_get_running_partition(void) { return running; }
esp_err_t esp_flash_get_size(void *chip, uint32_t *size)
{ assert(chip == NULL); *size = flash_size; return ESP_OK; }
esp_err_t esp_ota_get_partition_description(const esp_partition_t *p, esp_app_desc_t *d)
{ assert(p == &parts[2]); snprintf(d->project_name, 32, "%s", factory_name); return ESP_OK; }
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *p)
{ selected_at = ++calls; if (!select_error) selected = p; return select_error; }
esp_err_t esp_ota_get_state_partition(const esp_partition_t *p, esp_ota_img_states_t *s)
{ assert(p == running); *s = image_state; return state_error; }
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void)
{ confirmed_at = ++calls; return confirm_error; }
void esp_restart(void) { ++restarts; }
esp_err_t nvs_open(const char *ns, int mode, nvs_handle_t *h)
{ assert(!strcmp(ns, "tab5_launcher")); *h = 1;
  return nvs_error ? nvs_error : mode == NVS_READONLY && !saved ? ESP_ERR_NVS_NOT_FOUND : ESP_OK; }
esp_err_t nvs_get_u8(nvs_handle_t h, const char *key, uint8_t *value)
{ assert(h == 1 && !strcmp(key, "last_slot")); *value = last_slot; return ESP_OK; }
esp_err_t nvs_set_u8(nvs_handle_t h, const char *key, uint8_t value)
{ assert(h == 1 && !strcmp(key, "last_slot")); last_slot = value; saved = true; return ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t h) { assert(h == 1); return ESP_OK; }
void nvs_close(nvs_handle_t h) { assert(h == 1); }

int main(void)
{
    reset(); assert(tab5_app_boot_check_layout() == ESP_OK);
    for (unsigned i = 0; i < sizeof(parts) / sizeof(parts[0]); ++i) {
        reset(); missing = (int)i;
        assert(tab5_app_boot_check_layout() != ESP_OK && !calls);
        reset(); parts[i].address += 0x1000;
        assert(tab5_app_boot_check_layout() != ESP_OK && !calls);
        parts[i].address -= 0x1000;
    }
    reset(); flash_size /= 2; assert(tab5_app_boot_check_layout() != ESP_OK);
    reset(); factory_name = "another_app"; assert(!tab5_app_boot_console_available());
    reset(); const esp_partition_t *target = NULL;
    assert(tab5_app_boot_prepare_launch(&target) == ESP_OK);
    assert(target == &parts[4] && selected == &parts[2]);
    assert(tab5_app_boot_select(target) == ESP_OK && selected == target);
    assert(tab5_app_boot_prepare_launch(&target) == ESP_OK && target == &parts[5]);
    assert(tab5_app_boot_select(target) == ESP_OK);
    assert(tab5_app_boot_prepare_launch(&target) == ESP_OK && target == &parts[4]);
    assert(tab5_app_boot_select(&parts[2]) != ESP_OK);
    reset(); select_error = ESP_FAIL;
    assert(tab5_app_boot_prepare_launch(&target) == ESP_FAIL && target == NULL);
    reset(); nvs_error = ESP_FAIL;
    assert(tab5_app_boot_prepare_launch(&target) == ESP_FAIL && !calls);
    reset(); running = &parts[4]; assert(tab5_app_boot_console_available());
    assert(tab5_app_boot_prepare_launch(&target) != ESP_OK && !calls);
    assert(tab5_app_boot_select(&parts[5]) != ESP_OK && !calls);
    assert(tab5_app_boot_ready(false) != ESP_OK && !calls);
    assert(tab5_app_boot_ready(true) == ESP_OK);
    assert(confirmed_at > 0 && selected_at > confirmed_at && selected == &parts[2]);
    reset(); running = &parts[4]; confirm_error = ESP_FAIL;
    assert(tab5_app_boot_ready(true) == ESP_FAIL && !selected_at);
    reset(); running = &parts[4]; select_error = ESP_FAIL;
    assert(tab5_app_boot_ready(true) == ESP_FAIL);
    reset(); running = &parts[4]; image_state = ESP_OTA_IMG_NEW;
    assert(tab5_app_boot_ready(true) != ESP_OK && !calls);
    reset(); running = &parts[5]; state_error = ESP_ERR_NOT_FOUND;
    assert(tab5_app_boot_ready(true) == ESP_OK && !confirmed_at);
    assert(tab5_app_boot_return_to_console() == ESP_OK && restarts == 1);
    reset(); running = &parts[4]; select_error = ESP_FAIL;
    assert(tab5_app_boot_return_to_console() == ESP_FAIL && !restarts);
    reset(); assert(tab5_app_boot_ready(true) != ESP_OK && !calls);
    puts("PASS: real boot lifecycle under ASan/UBSan; layout guards, slot rotation, confirmation order and Console return");
    return 0;
}
