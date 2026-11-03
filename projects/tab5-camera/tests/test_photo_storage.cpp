#include "photo_storage.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

int main() {
    char root[] = "/tmp/tab5-photo-test-XXXXXX";
    assert(mkdtemp(root));
    constexpr uint8_t jpeg[] = {0xff, 0xd8, 1, 2, 3, 0xff, 0xd9};
    PhotoDate date{true, 2026, 11, 3, 14, 2, 5};
    char path[128], full[300];
    assert(photo_save_jpeg(root, jpeg, sizeof(jpeg), date, path, sizeof(path)) == PhotoSaveResult::ok);
    assert(std::strcmp(path, "/DCIM/100TAB5/IMG_20261103_140205_0001.jpg") == 0);
    std::snprintf(full, sizeof(full), "%s%s", root, path);
    FILE *f = std::fopen(full, "rb");
    uint8_t actual[sizeof(jpeg)]{};
    assert(f && std::fread(actual, 1, sizeof(actual), f) == sizeof(actual));
    assert(std::memcmp(actual, jpeg, sizeof(jpeg)) == 0);
    assert(std::fclose(f) == 0);
    assert(photo_save_jpeg(root, jpeg, sizeof(jpeg), date, path, sizeof(path)) == PhotoSaveResult::ok);
    assert(std::strstr(path, "_0002.jpg"));
    char partial[300];
    std::snprintf(partial, sizeof(partial), "%s/DCIM/100TAB5/.IMG_20261103_140205_0003.jpg.part", root);
    int fd = open(partial, O_CREAT | O_EXCL | O_WRONLY, 0600);
    assert(fd >= 0 && write(fd, "X", 1) == 1 && close(fd) == 0);
    assert(photo_save_jpeg(root, jpeg, sizeof(jpeg), date, path, sizeof(path)) == PhotoSaveResult::ok);
    assert(std::strstr(path, "_0004.jpg"));
    struct stat st{};
    assert(stat(partial, &st) == 0 && st.st_size == 1);
    uint8_t invalid[] = {0xff, 0xd8, 1, 2, 3};
    assert(photo_save_jpeg(root, invalid, sizeof(invalid), date, path, sizeof(path)) == PhotoSaveResult::invalid_jpeg);
    date.valid = false;
    assert(photo_save_jpeg(root, jpeg, sizeof(jpeg), date, path, sizeof(path)) == PhotoSaveResult::ok);
    assert(std::strcmp(path, "/DCIM/100TAB5/IMG_UNDATED_0001.jpg") == 0);
    char tiny[4];
    assert(photo_save_jpeg(root, jpeg, sizeof(jpeg), date, tiny, sizeof(tiny)) != PhotoSaveResult::ok);
    char command[340];
    std::snprintf(command, sizeof(command), "rm -rf %s", root);
    assert(std::system(command) == 0);
    std::puts("photo storage: collision, interrupted temp, exact readback, invalid JPEG and missing date PASS");
}
