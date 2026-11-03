#ifdef TAB5_LOCK_PIN
#undef TAB5_LOCK_PIN
#endif
#define TAB5_LOCK_PIN "1357"
#include "matrix_model.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>
using namespace tab5::matrix;

void arm(Lock &lock,int w,int h) {
    for(int i=0;i<10;++i) lock.poll(true,500,550,i*20,w,h);
    assert(!lock.armed);
    for(int i=0;i<3;++i) lock.poll(false,0,0,200+i*20,w,h);
    assert(lock.armed);
}
void tap(Lock &lock,int x,int y,uint64_t now,int w,int h) {
    assert(!lock.poll(true,x,y,now,w,h));
    assert(!lock.poll(false,0,0,now+20,w,h));
    assert(lock.poll(false,0,0,now+40,w,h));
}
void key(Lock &lock,int index,uint64_t now,int w,int h) {
    const auto b=index==12?Keypad(w,h).cancel():Keypad(w,h).key(index);
    tap(lock,b.x+b.w/2,b.y+b.h/2,now,w,h);
}
int digit_to_key(char d) {
    if(d=='0') return 10;
    return d-'1';          // '1'..'9' -> 0..8
}
void enter_pin(Lock &lock,const char *pin,uint64_t base,int w,int h) {
    for(int i=0;pin[i];++i) key(lock,digit_to_key(pin[i]),base+i*100,w,h);
}
void open(Lock &lock,uint64_t now,int w,int h) {
    tap(lock,100,100,now,w,h); assert(lock.state==State::rain);
    tap(lock,110,110,now+160,w,h); assert(lock.state==State::keypad);
    assert(lock.count==0); // Opening tap cannot also press a keypad key.
}
int main() {
    for(int h : {720,1280}) {
        const int w=h==720?1280:720;
        Lock lock; arm(lock,w,h);
        tap(lock,100,100,400,w,h); assert(lock.state==State::rain);
        tap(lock,100,100,1000,w,h); assert(lock.state==State::rain); // Slow taps.
        tap(lock,500,500,1150,w,h); assert(lock.state==State::rain); // Far taps.
        lock.pending_tap=false;
        open(lock,1400,w,h);
        key(lock,11,1800,w,h); assert(lock.state==State::keypad); // Empty OK.
        enter_pin(lock,"1234",1900,w,h); // Wrong PIN (bounded to four digits).
        assert(lock.count==4);
        key(lock,11,2400,w,h); assert(lock.state==State::keypad && lock.count==0);
        assert(lock.wrong_until>2440);
        key(lock,1,2600,w,h); assert(lock.count==0); // No input during wrong feedback.
        key(lock,1,3500,w,h); assert(lock.count==1);
        key(lock,9,3600,w,h); assert(lock.count==0); // Back.
        key(lock,9,3700,w,h); assert(lock.count==0);
        key(lock,12,3800,w,h); assert(lock.state==State::rain && lock.count==0);
        open(lock,4000,w,h);
        enter_pin(lock,TAB5_LOCK_PIN,4400,w,h);
        assert(lock.state==State::keypad); // Four digits alone do not unlock.
        key(lock,11,4800,w,h); assert(lock.state==State::unlocked && lock.count==0);
        for(int i=0;i<13;++i) {
            auto b=i==12?Keypad(w,h).cancel():Keypad(w,h).key(i);
            assert(b.x>=0 && b.y>=0 && b.x+b.w<=w && b.y+b.h<=h);
        }
        Lock held; arm(held,w,h);
        held.poll(true,100,100,300,w,h);
        held.poll(false,0,0,1200,w,h); held.poll(false,0,0,1220,w,h);
        assert(!held.pending_tap && held.state==State::rain);
        held.poll(true,100,100,1400,w,h); held.poll(true,300,300,1420,w,h);
        held.poll(false,0,0,1440,w,h); held.poll(false,0,0,1460,w,h);
        assert(!held.pending_tap);
    }
    Rain rain; rain.reset(80,42);
    int respawns=0, tails=0;
    for(int frame=0;frame<2000;++frame) {
        auto before=rain.columns;
        rain.tick(max_columns,max_rows);
        for(int c=0;c<max_columns;++c) {
            const auto &col=rain.columns[c];
            assert(col.speed>=1 && col.speed<=3 && col.length>=8 && col.length<=23);
            if(col.head<before[c].head) ++respawns;
            else assert(col.head==before[c].head+before[c].speed);
            if(col.head>=0 && col.head<max_rows) assert(Rain::intensity(col,col.head)==3);
            for(int row=0;row<max_rows;++row) {
                int brightness=Rain::intensity(col,row);
                if(brightness) assert(col.glyphs[row]>=32 && col.glyphs[row]<127);
                if(brightness==1) ++tails;
            }
        }
    }
    assert(respawns>100 && tails>1000);
    std::puts("PASS: column heads/tails/respawns; double tap; PIN, Back, Cancel, wrong/held/drag input; both layouts");
}
