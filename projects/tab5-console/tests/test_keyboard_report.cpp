#include "keyboard_report.h"
#include <cassert>
#include <cstdio>
#include <cstring>
int main() {
    const uint8_t descriptor[]={0x05,1,0x09,6,0xa1,1,0x85,1,0x05,7,
        0x19,0xe0,0x29,0xe7,0x15,0,0x25,1,0x75,1,0x95,8,0x81,2,
        0x95,1,0x75,8,0x81,1,0x95,5,0x75,1,0x05,8,0x19,1,0x29,5,0x91,2,
        0x95,1,0x75,3,0x91,1,0x95,6,0x75,8,0x15,0,0x25,0x65,
        0x05,7,0x19,0,0x29,0x65,0x81,0,0xc0};
    assert(cm::canonical_keyboard_report(descriptor,sizeof(descriptor),1));
    assert(!cm::canonical_keyboard_report(descriptor,sizeof(descriptor),2));
    for(size_t i=0;i<sizeof(descriptor);++i)
        assert(!cm::canonical_keyboard_report(descriptor,i,1));
    uint8_t changed[sizeof(descriptor)];std::memcpy(changed,descriptor,sizeof(changed));
    changed[13]=0xe6;assert(!cm::canonical_keyboard_report(changed,sizeof(changed),1));
    std::memcpy(changed,descriptor,sizeof(changed));changed[48]=5;
    assert(!cm::canonical_keyboard_report(changed,sizeof(changed),1));
    std::memcpy(changed,descriptor,sizeof(changed));changed[sizeof(changed)-2]=2;
    assert(!cm::canonical_keyboard_report(changed,sizeof(changed),1));
    assert(!cm::canonical_keyboard_report(nullptr,10,1));
    std::puts("keyboard descriptor validation PASS");
}
