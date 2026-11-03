#include "tab5_installer.h"
#include "tab5_app_boot.h"
#include "test_boot_sdk.h"
#include "mbedtls/sha256.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static esp_partition_t target = {0, 16, 0x420000, 0x500000, false};
static unsigned char flash[0x500000], image[24576];
static size_t image_size, written;
static char image_path[256];
static int begin_calls, end_calls, abort_calls, select_calls, write_calls;
static bool bad_layout, fail_write, fail_end, bad_readback, mutate_source;
static unsigned last_progress, event_count;
static tab5_installer_state_t last_state;

static void sha(const unsigned char *data, size_t size, char out[65])
{
    unsigned char hash[32]; mbedtls_sha256(data, size, hash, 0);
    for (unsigned i = 0; i < 32; ++i) sprintf(out + i * 2, "%02x", hash[i]);
}
static void fixture(void)
{
    memset(image, 0, sizeof(image));
    image[0] = 0xe9; image[1] = 1; image[2] = 2; image[3] = 0x40;
    image[12] = 0x12; image[15] = 100; image[23] = 1;
    uint32_t n = 20000, magic = 0xabcd5432;
    memcpy(image + 28, &n, 4); memcpy(image + 32, &magic, 4);
    strcpy((char *)image + 48, "0.1.1"); strcpy((char *)image + 80, "Tab5Weather");
    unsigned char checksum = 0xef;
    for (unsigned i = 32; i < 32 + n; ++i) checksum ^= image[i];
    size_t end = (32 + n) | 15;
    image[end++] = checksum;
    mbedtls_sha256(image, end, image + end, 0); image_size = end + 32;
    FILE *f = fopen(image_path, "wb"); assert(f);
    assert(fwrite(image, 1, image_size, f) == image_size); fclose(f);
}
static tab5_installer_package_t package(void)
{
    tab5_installer_package_t p = {0};
    strcpy(p.project, "Tab5Weather"); strcpy(p.version, "0.1.1");
    strcpy(p.image_path, image_path); p.size_bytes = (uint32_t)image_size;
    sha(image, image_size, p.sha256);
    return p; // No separate manual validation is needed.
}
static void progress(const tab5_installer_status_t *s, void *context)
{
    assert(context == NULL);
    if (!event_count) assert(s->state == TAB5_INSTALLER_VALIDATING && s->progress == 0);
    if (s->state != TAB5_INSTALLER_FAILED) assert(s->progress >= last_progress);
    if (s->state == TAB5_INSTALLER_DONE) assert(s->progress == 100);
    last_progress = s->progress; last_state = s->state; ++event_count;
}
static void reset(void)
{
    begin_calls = end_calls = abort_calls = select_calls = write_calls = 0;
    bad_layout = fail_write = fail_end = bad_readback = mutate_source = false;
    written = event_count = last_progress = 0;
    memset(flash, 0xcc, sizeof(flash)); fixture();
}
void vTaskDelay(unsigned ticks) { assert(ticks == 1); }
esp_err_t tab5_app_boot_prepare_launch(const esp_partition_t **slot)
{ *slot = bad_layout ? NULL : &target; return bad_layout ? ESP_ERR_INVALID_STATE : ESP_OK; }
esp_err_t tab5_app_boot_select(const esp_partition_t *slot)
{ assert(slot == &target && end_calls == 1); ++select_calls; return ESP_OK; }
esp_err_t tab5_app_boot_ready(bool ok) { assert(ok); return ESP_OK; }
esp_err_t tab5_app_boot_return_to_console(void) { return ESP_OK; }
esp_err_t esp_ota_begin(const esp_partition_t *slot, size_t mode, esp_ota_handle_t *handle)
{
    assert(slot == &target && mode == OTA_WITH_SEQUENTIAL_WRITES); ++begin_calls; *handle = 1;
    if (mutate_source) {
        FILE *f = fopen(image_path, "r+b"); assert(f); fseek(f, 1000, SEEK_SET); fputc(7, f); fclose(f);
    }
    return ESP_OK;
}
esp_err_t esp_ota_write(esp_ota_handle_t handle, const void *data, size_t size)
{
    assert(handle == 1 && size <= 4096 && written + size <= sizeof(flash)); ++write_calls;
    if (fail_write) return ESP_FAIL;
    memcpy(flash + written, data, size); written += size; return ESP_OK;
}
esp_err_t esp_ota_end(esp_ota_handle_t handle)
{ assert(handle == 1); ++end_calls; return fail_end ? ESP_FAIL : ESP_OK; }
esp_err_t esp_ota_abort(esp_ota_handle_t handle)
{ assert(handle == 1); ++abort_calls; return ESP_OK; }
esp_err_t esp_partition_read(const esp_partition_t *slot, size_t offset, void *data, size_t size)
{
    assert(slot == &target && offset + size <= written);
    memcpy(data, flash + offset, size);
    if (bad_readback) ((unsigned char *)data)[0] ^= 1;
    return ESP_OK;
}

int main(int argc, char **argv)
{
    assert(argc == 2); snprintf(image_path, sizeof(image_path), "%s/image.bin", argv[1]);
    tab5_installer_set_progress_callback(progress, NULL);
    tab5_installer_status_t status; int slot;
    for (unsigned i = 0; i < 2; ++i) {
        reset(); tab5_installer_package_t p = package();
        assert(tab5_installer_install(&p, &status, &slot) == ESP_OK);
        assert(slot == 0 && select_calls == 1 && begin_calls == 1 && !abort_calls);
        assert(status.state == TAB5_INSTALLER_DONE && status.progress == 100 && event_count > 10);
        assert(written == image_size && !memcmp(flash, image, image_size));
    }
    reset(); tab5_installer_package_t p = package(); p.sha256[0] = p.sha256[0] == 'a' ? 'b' : 'a';
    assert(tab5_installer_install(&p, &status, &slot) != ESP_OK && !begin_calls && !select_calls);
    reset(); p = package(); fail_write = true;
    assert(tab5_installer_install(&p, &status, &slot) != ESP_OK && abort_calls == 1 && !select_calls);
    reset(); p = package(); fail_end = true;
    assert(tab5_installer_install(&p, &status, &slot) != ESP_OK && !select_calls);
    reset(); p = package(); bad_readback = true;
    assert(tab5_installer_install(&p, &status, &slot) != ESP_OK && !select_calls);
    reset(); p = package(); bad_layout = true;
    assert(tab5_installer_install(&p, &status, &slot) != ESP_OK && !begin_calls && !select_calls);
    reset(); p = package(); mutate_source = true;
    assert(tab5_installer_install(&p, &status, &slot) != ESP_OK && abort_calls == 1 && !select_calls);
    assert(last_state == TAB5_INSTALLER_FAILED);
    puts("PASS: production installer under ASan/UBSan; automatic validation, monotonic progress, sequential writes, corruption/failure gates and safe retry");
    return 0;
}
