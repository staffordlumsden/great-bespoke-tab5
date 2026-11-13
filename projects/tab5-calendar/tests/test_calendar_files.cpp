#include "../src/calendar_files.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>
#include <sys/stat.h>

int main() {
    char dir[] = "/tmp/tab5-calendar-test-XXXXXX";
    assert(mkdtemp(dir));
    auto touch = [&](const char *name) {
        std::string path = std::string(dir) + "/" + name;
        FILE *f = std::fopen(path.c_str(), "wb"); assert(f); std::fclose(f);
    };
    touch("z.ICS"); touch("a.ics"); touch(".hidden.ics"); touch("notes.txt");
    std::string folder = std::string(dir) + "/folder.ics";
    assert(mkdir(folder.c_str(), 0700) == 0);
    std::string link = std::string(dir) + "/link.ics";
    assert(symlink("a.ics", link.c_str()) == 0);
    calendar::FileList list{};
    assert(calendar::scan_files(dir, list) == calendar::ScanResult::ok);
    assert(list.count == 2 && !std::strcmp(list.names[0], "a.ics") &&
           !std::strcmp(list.names[1], "z.ICS"));
    for (int i = 0; i < 15; ++i) {
        char name[20]; std::snprintf(name, sizeof(name), "extra%02d.ics", i); touch(name);
    }
    assert(calendar::scan_files(dir, list) == calendar::ScanResult::too_many);
    assert(list.count == 0);
    for (int i = 0; i < 15; ++i) {
        char name[20]; std::snprintf(name, sizeof(name), "extra%02d.ics", i);
        assert(unlink((std::string(dir) + "/" + name).c_str()) == 0);
    }
    for (const char *name : {"z.ICS", "a.ics", ".hidden.ics", "notes.txt", "link.ics"})
        assert(unlink((std::string(dir) + "/" + name).c_str()) == 0);
    assert(rmdir(folder.c_str()) == 0 && rmdir(dir) == 0);
    assert(calendar::scan_files(dir, list) == calendar::ScanResult::unavailable);
}
