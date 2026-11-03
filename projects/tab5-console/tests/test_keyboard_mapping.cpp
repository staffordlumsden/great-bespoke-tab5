#include "keyboard_mapping.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
using cm::KeySequence;
void expect(uint8_t usage,uint8_t mods,const std::string& expected,bool caps=false,bool num=true,bool app=false) {
    KeySequence out{};assert(cm::map_key(usage,mods,caps,num,app,out));
    assert(out.size==expected.size() && !std::memcmp(out.bytes,expected.data(),out.size));
}
void collect(void* p,const KeySequence& value){static_cast<std::string*>(p)->append(value.bytes,value.size);}
int main() {
    {cm::KeySequence action{};
     assert(cm::map_key(75,2,false,false,false,action,true) && action.local==cm::LocalKeyAction::scroll_older && action.size==0);
     assert(cm::map_key(78,2,false,false,false,action,true) && action.local==cm::LocalKeyAction::scroll_newer && action.size==0);
     assert(cm::map_key(75,2,false,false,false,action,false) && action.local==cm::LocalKeyAction::none && action.size>0);
     assert(cm::map_key(75,1,false,false,false,action,true) && action.local==cm::LocalKeyAction::none && action.size>0);}

    expect(4,0,"a");expect(4,2,"A");expect(4,0,"A",true);expect(4,2,"a",true);
    expect(30,2,"!");expect(31,2,"@");expect(45,0,"-");expect(45,2,"_");
    expect(47,2,"{");expect(49,0,"\\");expect(52,2,"\"");expect(53,0,"`");expect(56,2,"?");
    expect(6,1,std::string(1,3));expect(7,0x10,std::string(1,4));expect(15,1,std::string(1,12));
    expect(29,1,std::string(1,26));expect(4,3,std::string(1,1));expect(8,1,std::string(1,5));
    expect(44,1,std::string(1,0));expect(31,1,std::string(1,0));expect(47,1,std::string(1,27));
    expect(40,0,"\r");expect(41,0,"\033");expect(42,0,"\177");expect(42,1,"\b");
    expect(43,0,"\t");expect(43,2,"\033[Z");expect(4,4,"\033a");
    expect(82,0,"\033[A");expect(82,0,"\033OA",false,true,true);expect(80,1,"\033[1;5D");
    expect(79,3,"\033[1;6C");expect(74,0,"\033[H");expect(77,4,"\033[1;3F");
    expect(73,0,"\033[2~");expect(76,1,"\033[3;5~");expect(75,0,"\033[5~");expect(78,0,"\033[6~");
    expect(58,0,"\033OP");expect(61,1,"\033[1;5S");expect(62,0,"\033[15~");expect(69,2,"\033[24;2~");
    expect(89,0,"1");expect(89,0,"\033[F",false,false);expect(99,0,"\033[3~",false,false);
    KeySequence out{};assert(!cm::map_key(4,8,false,true,false,out));assert(!cm::map_key(255,0,false,true,false,out));
    cm::BootKeyboard keyboard;std::string typed;uint8_t report[8]={0,0,4,4,5,0,0,0};
    assert(keyboard.report(report,8,0,collect,&typed));assert(typed=="ab");
    assert(keyboard.report(report,8,100,collect,&typed));assert(typed=="ab");
    keyboard.tick(449,collect,&typed);assert(typed=="ab");keyboard.tick(450,collect,&typed);assert(typed=="abb");
    keyboard.tick(100000,collect,&typed);assert(typed=="abbb"); // No accumulated repeat burst.
    report[0]=2;assert(keyboard.report(report,8,100001,collect,&typed));keyboard.tick(100050,collect,&typed);assert(typed=="abbbB");
    report[2]=report[3]=report[4]=0;assert(keyboard.report(report,8,100100,collect,&typed));keyboard.tick(200000,collect,&typed);assert(typed=="abbbB");
    report[0]=1;report[2]=6;assert(keyboard.report(report,8,200001,collect,&typed));assert(typed.back()==3);
    report[2]=1;assert(!keyboard.report(report,8,200002,collect,&typed));const auto before=typed;keyboard.tick(300000,collect,&typed);assert(typed==before);
    keyboard.reset();assert(keyboard.modifiers()==0);keyboard.tick(400000,collect,&typed);assert(typed==before);
    assert(!keyboard.report(report,7,400001,collect,&typed));assert(!keyboard.report(nullptr,8,0,collect,&typed));
    KeySequence zoom{};
    assert(cm::map_key(46,1,false,true,false,zoom,true) && zoom.local==cm::LocalKeyAction::zoom_in && zoom.size==0);
    assert(cm::map_key(46,3,false,true,false,zoom,true) && zoom.local==cm::LocalKeyAction::zoom_in);
    assert(cm::map_key(45,1,false,true,false,zoom,true) && zoom.local==cm::LocalKeyAction::zoom_out);
    assert(!cm::map_key(46,1,false,true,false,zoom));expect(45,1,std::string(1,31));
    cm::BootKeyboard zoom_keyboard;zoom_keyboard.local_zoom(true);zoom_keyboard.reset();unsigned actions=0;
    const auto count_zoom=[](void* context,const KeySequence& key){if(key.local!=cm::LocalKeyAction::none)++*static_cast<unsigned*>(context);};
    uint8_t zoom_report[8]={1,0,46,0,0,0,0,0};assert(zoom_keyboard.report(zoom_report,8,0,count_zoom,&actions));zoom_keyboard.tick(10000,count_zoom,&actions);assert(actions==1);
    zoom_report[2]=0;assert(zoom_keyboard.report(zoom_report,8,10001,count_zoom,&actions));zoom_report[2]=45;assert(zoom_keyboard.report(zoom_report,8,10002,count_zoom,&actions));assert(actions==2);
    std::puts("PASS: US mapping, modifiers, ANSI controls, releases, rollover, repeat bounds and disconnect reset");
}
