#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <cstdlib>

#ifndef TAB5_LOCK_PIN
#if __has_include("tab5_lock_pin_private.h")
#include "tab5_lock_pin_private.h"
#endif
#endif

#ifndef TAB5_LOCK_PIN
#define TAB5_LOCK_PIN "0000"
#endif

namespace tab5::matrix {
// Port of CYD Matrix Rain's independent column heads, speeds and fading tails.
constexpr int cell_width = 12, cell_height = 16, max_columns = 107, max_rows = 80;
struct Column { int head = 0, speed = 1, length = 12; char glyphs[max_rows]{}; };
struct Rain {
    std::array<Column, max_columns> columns{};
    uint32_t rng = 1;
    uint32_t random() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
    char glyph() {
        constexpr char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!@#$%&*+=<>?/|[]{}";
        return chars[random() % (sizeof(chars) - 1)];
    }
    void seed(Column &c, int rows, bool initial) {
        c.length = 8 + random() % 16;
        c.speed = 1 + random() % 3;
        c.head = initial ? int(random() % (rows + c.length)) - c.length : -1;
        for (auto &ch : c.glyphs) ch = glyph();
    }
    void reset(int rows, uint32_t seed_value) {
        rng = seed_value ? seed_value : 1;
        for (auto &c : columns) seed(c, rows, true);
    }
    void tick(int cols, int rows) {
        for (int i = 0; i < cols; ++i) {
            auto &c = columns[i];
            c.head += c.speed;
            if (c.head - c.length >= rows) seed(c, rows, false);
            if (c.head >= 0 && c.head < rows) c.glyphs[c.head] = glyph();
        }
    }
    static int intensity(const Column &c, int row) {
        int d = c.head - row;
        return d < 0 || d >= c.length ? 0 : d == 0 ? 3 : d < c.length / 3 ? 2 : 1;
    }
};

struct Box {
    int x, y, w, h;
    bool contains(int px,int py) const { return px>=x && px<x+w && py>=y && py<y+h; }
};
struct Keypad {
    int left, top;
    Keypad(int width,int height): left((width-600)/2),top((height-640)/2) {}
    Box key(int index) const { return {left+(index%3)*204,top+140+(index/3)*100,192,88}; }
    Box cancel() const { return {left,top+550,600,70}; }
    int hit(int x,int y) const {
        for(int i=0;i<12;++i) if(key(i).contains(x,y)) return i;
        return cancel().contains(x,y) ? 12 : -1;
    }
};
enum class State { rain, keypad, unlocked };
struct Lock {
    State state=State::rain;
    char digits[5]{};
    int count=0;
    uint64_t wrong_until=0, first_tap=0, press_at=0;
    bool pending_tap=false, touching=false, armed=false, moved=false;
    int start_x=0,start_y=0,first_x=0,first_y=0;
    unsigned releases=0;
    void clear() { std::memset(digits,0,sizeof(digits)); count=0; }
    void tap(int x,int y,uint64_t now,int width,int height) {
        if(state==State::rain) {
            if(pending_tap && now-first_tap<=450 &&
               std::abs(x-first_x)<=80 && std::abs(y-first_y)<=80) {
                state=State::keypad; clear(); pending_tap=false;
            } else { pending_tap=true; first_tap=now; first_x=x; first_y=y; }
            return;
        }
        if(state!=State::keypad || now<wrong_until) return;
        int key=Keypad(width,height).hit(x,y);
        if(key==12) { clear(); wrong_until=0; state=State::rain; pending_tap=false; }
        else if(key==9) { if(count) digits[--count]=0; }
        else if(key==11 && count==4) {
            if(std::strcmp(digits, TAB5_LOCK_PIN)==0) { clear(); state=State::unlocked; }
            else { clear(); wrong_until=now+900; }
        } else if(key>=0 && key<=10 && key!=9 && count<4) {
            digits[count++]=key==10?'0':char('1'+key); digits[count]=0;
        }
    }
    // Consume the opening touch, debounce release, reject drags/long holds.
    bool poll(bool pressed,int x,int y,uint64_t now,int width,int height) {
        if(!armed) { releases=pressed?0:releases+1; if(releases>=3) {armed=true;releases=0;} return false; }
        if(pressed) {
            if(!touching) {touching=true;start_x=x;start_y=y;press_at=now;moved=false;}
            if(std::abs(x-start_x)>40 || std::abs(y-start_y)>40) moved=true;
            releases=0;
        } else if(touching && ++releases>=2) {
            touching=false;releases=0;
            if(!moved && now-press_at<=600) {tap(start_x,start_y,now,width,height); return true;}
            pending_tap=false;
        }
        return false;
    }
};
} // namespace tab5::matrix
