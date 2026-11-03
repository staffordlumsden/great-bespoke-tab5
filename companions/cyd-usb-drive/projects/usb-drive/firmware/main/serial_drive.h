#pragma once
#include <cstdint>

namespace usbdrive {
struct Snapshot {
    bool ready = false;
    bool linked = false;
    bool busy = false;
    bool writing = false;
    uint64_t done = 0;
    uint64_t total = 0;
    uint64_t card_total = 0;
    uint64_t card_free = 0;
    char operation[20]{};
    char name[52]{};
    char message[72]{};
};
bool serial_start();
Snapshot snapshot();
void cancel_transfer();
void host_disconnect();
}
