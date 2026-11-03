#include "ui.h"
#include "pixel_fonts.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace oa {
namespace {
struct Theme { uint32_t bg, surface, edge, text, muted; };
constexpr Theme dark{0x101215,0x1d2228,0x353d46,0xf7f8fa,0x9ba6b2};
constexpr Theme light{0xeef1f4,0xffffff,0xc8cfd7,0x15181c,0x59636f};
constexpr uint32_t blue=0x3b82f6, green=0x22c55e, amber=0xf59e0b;
using cyd_fonts::Font;
void box(Painter p,int x,int y,int w,int h,uint32_t colour) {
    const int right=std::min(1280,x+w),bottom=std::min(720,y+h);
    x=std::max(0,x);y=std::max(0,y);
    if(right>x&&bottom>y)p.rect(p.context,x,y,right-x,bottom-y,colour);
}
void panel(Painter p,int x,int y,int w,int h,Theme t) {
    box(p,x,y,w,h,t.edge);box(p,x+2,y+2,w-4,h-4,t.surface);
}
void text(Painter p,const char *s,int x,int y,const Font &font,uint32_t colour,int scale=2) {
    const int baseline=y+font.ascent*scale;
    for(;*s;++s) {
        unsigned c=static_cast<unsigned char>(*s);if(c<font.first||c>font.last)c='?';
        const auto &g=font.glyphs[c-font.first];
        for(unsigned gy=0;gy<g.height;++gy)for(unsigned gx=0;gx<g.width;++gx) {
            const unsigned bit=gy*g.width+gx;
            if(font.bitmap[g.offset+bit/8]&(0x80>>(bit%8)))
                box(p,x+(g.x+int(gx))*scale,baseline+(g.y+int(gy))*scale,scale,scale,colour);
        }
        x+=(g.advance+1)*scale;
    }
}
void small(Painter p,const char*s,int x,int y,uint32_t colour) {
    text(p,s,x,y,cyd_fonts::pixelfont_7x5_thin,colour);
}
void meter(Painter p,int x,int y,int width,double used,Theme t) {
    box(p,x,y,width,12,t.edge);
    box(p,x,y,int(width*std::clamp(used,0.0,100.0)/100.0),12,blue);
}

void window(Painter p,const char*label,const Window&w,int64_t now,int x,int y,int width,Theme t) {
    char value[64];
    if(w.known)std::snprintf(value,sizeof(value),"%s  %.0f%% USED / %.0f%% LEFT",label,w.used,100-w.used);
    else std::snprintf(value,sizeof(value),"%s  --",label);
    small(p,value,x,y,w.known?t.text:t.muted);
    if(w.known)meter(p,x,y+29,width,w.used,t);
    else box(p,x,y+29,width,12,t.edge);
    if(w.known)ou::countdown(w.resets_at,now,value,sizeof(value));
    else std::snprintf(value,sizeof(value),"Quota unavailable");
    small(p,value,x,y+52,t.muted);
}
Quota ollama_quota(const View &v) {
    Quota q;
    if(v.has_balance&&v.balance.kind==ou::BalanceKind::legacy)
        q.seven_day={true,v.balance.weekly_used,v.balance.weekly_reset};
    // Legacy session duration is absent from the API: never label it 5H.
    return q;
}
void quota_panel(const View&v,Painter p,Theme t,const Quota&q) {
    panel(p,792,150,464,250,t);
    text(p,v.balance_stale?"Quota - STALE":"Quota",812,165,cyd_fonts::pixelfont_13x7_semibold,t.text,2);
    window(p,"5H",q.five_hour,v.now,812,211,424,t);
    window(p,"7D",q.seven_day,v.now,812,301,424,t);
}
void draw_ollama(const View&v,Painter p,Theme t) {
    panel(p,24,150,744,478,t);
    text(p,"Ollama",44,165,cyd_fonts::pixelfont_13x7_semibold,t.text,3);
    char label[96];
    if(v.has_models)std::snprintf(label,sizeof(label),"24H - %llu REQUESTS%s",
        static_cast<unsigned long long>(v.models.requests),v.models_stale?" - STALE":"");
    else std::snprintf(label,sizeof(label),"24H - WAITING FOR MODEL DATA");
    small(p,label,44,219,t.muted);
    box(p,44,253,700,24,t.edge);
    if(v.has_models&&v.models.requests) {
        uint64_t cumulative=0;int previous=0;
        for(unsigned i=0;i<v.models.count;++i) {
            cumulative+=v.models.top[i].requests;
            const int end=int(700.0*double(cumulative)/double(v.models.requests));
            box(p,44+previous,253,end-previous,24,model_colours[i]);previous=end;
        }
    }
    for(unsigned i=0;i<top_models;++i) {
        const int y=302+int(i)*40;
        box(p,44,y,14,14,i<v.models.count&&v.has_models?model_colours[i]:t.edge);
        if(v.has_models&&i<v.models.count) {
            char name[37];std::snprintf(name,sizeof(name),"%.35s",v.models.top[i].name);
            if(std::strlen(v.models.top[i].name)>35)std::memcpy(name+32,"...",4);
            small(p,name,72,y,t.text);
            ou::count(v.models.top[i].requests,label,sizeof(label));small(p,label,635,y,t.text);
        } else small(p,"--",72,y,t.muted);
    }
    if(v.has_models)std::snprintf(label,sizeof(label),"OTHER: %llu",static_cast<unsigned long long>(v.models.other_requests));
    else std::snprintf(label,sizeof(label),"CONNECT TO SEE YOUR TOP FIVE MODELS");
    small(p,label,44,520,t.muted);
    if(v.has_models&&!v.models.global_top_five) {
        small(p,"TOP 5 OF RETURNED MODELS",44,556,amber);
        small(p,"Provider has hidden other model groups",44,585,t.muted);
    } else small(p,"BAR = SHARE OF REQUESTS, NOT QUOTA",44,585,t.muted);
    panel(p,792,418,464,210,t);
    text(p,"Plan balance",812,434,cyd_fonts::pixelfont_11x5_thin,t.text,2);
    if(v.has_balance) {
        if(v.balance.kind==ou::BalanceKind::legacy) {
            std::snprintf(label,sizeof(label),"SESSION %.0f%% USED%s",v.balance.session_used,v.balance_stale?" *":"");
            small(p,label,812,481,t.text);meter(p,812,514,424,v.balance.session_used,t);
            small(p,"Session duration not reported",812,549,t.muted);
        } else {
            std::snprintf(label,sizeof(label),"INCLUDED $%.2f%s",v.balance.remaining,v.balance_stale?" - STALE":"");
            small(p,label,812,481,t.text);
            if(v.balance.percent_known)meter(p,812,514,424,v.balance.used_percent,t);
            std::snprintf(label,sizeof(label),"PURCHASED $%.2f",v.balance.purchased);small(p,label,812,549,t.text);
        }
    } else small(p,"NOT CONNECTED",812,489,t.muted);
    small(p,"Provider-reported plan limits",812,598,t.muted);
    quota_panel(v,p,t,ollama_quota(v));
}
void draw_footer(const View&v,Painter p,Theme t) {
    box(p,0,646,1280,74,t.bg);
    const char *network=!v.wifi?"WI-FI: OFFLINE":!v.clock_ready?"WI-FI: CHECKING TIME":"WI-FI: ONLINE";
    small(p,network,24,658,v.wifi?green:amber);small(p,v.status,24,690,t.muted);
    panel(p,1010,658,246,48,t);
    const bool fetching=v.busy;
    text(p,fetching?"Updating...":"Refresh",1036,671,cyd_fonts::pixelfont_11x5_thin,
         fetching?t.muted:blue);
}
} // namespace
void draw(const View&v,Painter p,unsigned regions) {
    const Theme t=v.dark?dark:light;
    if(regions&full) {
        box(p,0,0,1280,720,t.bg);
        text(p,"TAB5 Ollama Analytics",24,24,cyd_fonts::pixelfont_13x7_semibold,t.text,3);
        small(p,"v0.1.1 - OLLAMA ONLY",27,84,t.muted);
        panel(p,842,24,190,62,t);text(p,v.dark?"Light":"Dark",875,45,cyd_fonts::pixelfont_11x5_thin,blue);
        panel(p,1048,24,208,62,t);text(p,"Console",1075,45,cyd_fonts::pixelfont_11x5_thin,blue);
    }
    if(regions&(full|ollama))draw_ollama(v,p,t);
    else if(regions&quota)quota_panel(v,p,t,ollama_quota(v));
    if(regions&(full|footer))draw_footer(v,p,t);
}
Action hit(int x,int y) {
    if(x<0||y<0||x>=1280||y>=720)return Action::none;
    if(y>=24&&y<86&&x>=842&&x<1032)return Action::theme;
    if(y>=24&&y<86&&x>=1048&&x<1256)return Action::console;
    if(y>=658&&y<706&&x>=1010&&x<1256)return Action::refresh;
    return Action::none;
}
} // namespace oa
