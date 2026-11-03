#include "ui.h"
#include "pixel_fonts.h"
#include <algorithm>
#include <cstdio>

namespace usbdrive {
namespace {
constexpr uint16_t rgb(unsigned c) { return ((c >> 19) & 31) << 11 | ((c >> 10) & 63) << 5 | ((c >> 3) & 31); }
struct Palette { uint16_t bg, header, surface, edge, text, muted, blue, green, amber, red; };
Palette palette(bool light) { return light ? Palette{rgb(0xa9dcc2),rgb(0xb9e4da),rgb(0xd6efe7),rgb(0x83b6b6),rgb(0x0c3d48),rgb(0x376e73),rgb(0x147ba4),rgb(0x21795c),rgb(0xc66b16),rgb(0xb93f50)} : Palette{rgb(0x081115),rgb(0x101d24),rgb(0x172b32),rgb(0x29414a),rgb(0xe4eef0),rgb(0x91a9b0),rgb(0x42a5f5),rgb(0x66cf8e),rgb(0xffae42),rgb(0xef6b72)}; }
bool box(int x,int y,int l,int t,int r,int b){return x>=l&&x<r&&y>=t&&y<b;}
bool outline(int x,int y,int l,int t,int r,int b){return box(x,y,l,t,r,b)&&(x==l||x==r-1||y==t||y==b-1);}
bool ink(const char *text,int left,int top,int x,int y,const cyd_fonts::Font &font,int scale=1) { int cursor=left;for(const unsigned char *c=reinterpret_cast<const unsigned char *>(text);*c;++c){unsigned ch=(*c>=font.first&&*c<=font.last)?*c:'?';const auto &g=font.glyphs[ch-font.first];const int gx=(x-cursor)/scale-g.x,gy=(y-top)/scale-font.ascent-g.y;if(x>=cursor&&gx>=0&&gy>=0&&gx<g.width&&gy<g.height){unsigned bit=gy*g.width+gx;if(font.bitmap[g.offset+bit/8]&(0x80>>(bit%8)))return true;}cursor+=(g.advance+1)*scale;if(cursor>x+font.ascent*scale)break;}return false; }
void human(uint64_t bytes,char (&out)[24]) { if(bytes<1024)std::snprintf(out,sizeof(out),"%llu B",(unsigned long long)bytes);else if(bytes<1024*1024)std::snprintf(out,sizeof(out),"%.1f KiB",double(bytes)/1024.0);else if(bytes<1024ull*1024*1024)std::snprintf(out,sizeof(out),"%.1f MiB",double(bytes)/(1024.0*1024));else std::snprintf(out,sizeof(out),"%.1f GiB",double(bytes)/(1024.0*1024*1024)); }
}
uint16_t pixel(int x,int y,void *context) {
    const auto &view=*static_cast<View *>(context);const auto p=palette(view.light);uint16_t colour=p.bg;
    if(y<42)colour=p.header;
    if(box(x,y,8,49,312,128)||box(x,y,8,135,312,196))colour=p.surface;
    if(outline(x,y,8,49,312,128)||outline(x,y,8,135,312,196))colour=p.edge;
    if(box(x,y,8,202,92,233)||box(x,y,98,202,202,233)||box(x,y,208,202,312,233))colour=p.surface;
    if(outline(x,y,8,202,92,233)||outline(x,y,98,202,202,233)||outline(x,y,208,202,312,233))colour=p.edge;
    const uint16_t state_colour=view.drive.linked?p.green:view.drive.ready?p.amber:p.red;
    if(box(x,y,282,13,293,24)){const int dx=x-287,dy=y-18;if(dx*dx+dy*dy<=25)colour=state_colour;}
    if(ink("CYD USB DRIVE",10,11,x,y,cyd_fonts::pixelfont_13x7_semibold))colour=p.text;
    if(ink(view.drive.linked?"CONNECTED":view.drive.ready?"WAITING":"ERROR",231,13,x,y,cyd_fonts::pixelfont_7x5_thin))colour=state_colour;
    const char *headline=view.drive.linked?"CYD SD Card":"Start CYD Drive on this Mac";
    if(ink(headline,20,61,x,y,cyd_fonts::pixelfont_11x5_thin))colour=p.text;
    if(ink("Finder read + write",20,83,x,y,cyd_fonts::pixelfont_7x5_thin)||ink("Shared: Files and Firmware",20,101,x,y,cyd_fonts::pixelfont_7x5_thin))colour=p.muted;
    char capacity[64]{},free[24]{},total[24]{};human(view.drive.card_free,free);human(view.drive.card_total,total);std::snprintf(capacity,sizeof(capacity),"%s free / %s",free,total);
    if(ink(capacity,163,101,x,y,cyd_fonts::pixelfont_7x5_thin))colour=p.muted;
    const char *operation=view.drive.operation[0]?view.drive.operation:"Ready";
    if(ink(operation,20,145,x,y,cyd_fonts::pixelfont_11x5_thin))colour=view.drive.busy?p.amber:p.text;
    if(view.drive.name[0]&&ink(view.drive.name,116,147,x,y,cyd_fonts::pixelfont_7x5_thin))colour=p.muted;
    if(box(x,y,20,170,300,181))colour=p.edge;
    if(view.drive.total&&view.drive.done<=view.drive.total){const int width=int(std::min<uint64_t>(280,view.drive.done*280/view.drive.total));if(box(x,y,20,170,20+width,181))colour=view.drive.writing?p.green:p.blue;}
    char progress[80];if(view.drive.busy)std::snprintf(progress,sizeof(progress),"%llu / %llu bytes",(unsigned long long)view.drive.done,(unsigned long long)view.drive.total);else std::snprintf(progress,sizeof(progress),"%s",view.drive.message[0]?view.drive.message:"USB serial file bridge ready");
    if(ink(progress,20,184,x,y,cyd_fonts::pixelfont_7x5_thin))colour=p.muted;
    if(ink(view.light?"DARK":"LIGHT",23,212,x,y,cyd_fonts::pixelfont_11x5_thin))colour=p.blue;
    if(ink(view.drive.busy?"STOP":"DISCONNECT",108,212,x,y,cyd_fonts::pixelfont_7x5_thin))colour=view.drive.busy?p.red:p.amber;
    if(ink("CONSOLE",224,212,x,y,cyd_fonts::pixelfont_11x5_thin))colour=view.console?p.blue:p.muted;
    return colour;
}
Action hit(int x,int y) { if(y<196||y>=240)return Action::none;if(x>=0&&x<96)return Action::theme;if(x>=96&&x<208)return Action::stop;if(x>=208&&x<320)return Action::console;return Action::none; }
}
