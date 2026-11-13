#include "calendar_events.h"
#include "calendar_files.h"
#include "calendar_feeds.h"
#include "calendar_fetch.h"
#include "calendar_layout.h"
#include "calendar_view.h"
#include "fonts/pixel_fonts.h"
#include "input_tab5_kbd.hpp"
#include "tab5_app_boot.h"
#include "tab5_hal.h"
#include "tab5_radio.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

namespace {
using namespace calendar;
constexpr const char *root="/sdcard/files/Calendar";
constexpr uint32_t bg=0x080b10,panel=0x111823,raised=0x1a2431,border=0x334155,white=0xe8edf2,muted=0x93a4b7;
FileList files{}; CalendarSetting settings[max_files]{}; std::vector<Event> events;
char calendar_status[max_files][32]{};
FeedList feeds{}; size_t first_feed=0; bool feed_busy=false, radio_started=false, sntp_started=false;
int64_t next_fetch=0; QueueHandle_t feed_queue=nullptr;unsigned local_warnings=0;bool agenda_truncated=false,feed_config_invalid=false;
Navigation nav{{2026,1,1},View::month,0,0};
int week_scroll=0,week_scroll_max=0;
QueueHandle_t queue=nullptr; unsigned warnings=0; char status[100]=""; bool dirty=true,render_ok=true,clock_ok=false;
struct Row { Date date; size_t event; };
struct FetchResult { std::vector<Event> items; bool ok[max_feeds]{}; FetchOutcome outcome[max_feeds]{}; unsigned rejected[max_feeds]{}; unsigned warnings=0,failed=0; };
std::vector<Row> agenda;
const char *months[]={"January","February","March","April","May","June","July","August","September","October","November","December"};
const char *days[]={"MON","TUE","WED","THU","FRI","SAT","SUN"};
void rect(int x,int y,int w,int h,uint32_t c){if(w>0&&h>0&&tab5_display_rect(x,y,w,h,c)!=ESP_OK)render_ok=false;}
void text(const char *s,int x,int y,uint32_t c,int scale=2,int limit=1200) {
    const auto &font=cyd_fonts::pixelfont_7x5_thin; int cur=x;
    for(const unsigned char *p=reinterpret_cast<const unsigned char*>(s);*p;++p){
        unsigned ch=*p;if(ch<font.first||ch>font.last)ch='?';auto &g=font.glyphs[ch-font.first];
        if(cur+(g.advance+1)*scale>x+limit)break;
        for(unsigned gy=0;gy<g.height;++gy)for(unsigned gx=0;gx<g.width;++gx){unsigned bit=gy*g.width+gx;if(font.bitmap[g.offset+bit/8]&(0x80>>(bit%8))) rect(cur+(g.x+int(gx))*scale,y+(font.ascent+g.y+int(gy))*scale,scale,scale,c);}
        cur+=(g.advance+1)*scale;
    }
}
void frame(int x,int y,int w,int h,uint32_t c){rect(x,y,w,1,c);rect(x,y+h-1,w,1,c);rect(x,y,1,h,c);rect(x+w-1,y,1,h,c);}
void button(int n,const char *label,uint32_t c){int x=layout::button_start_x+n*layout::button_stride;rect(x,layout::button_y,layout::button_width,layout::button_height,raised);frame(x,layout::button_y,layout::button_width,layout::button_height,c);text(label,x+12,layout::button_y+25,c,2,150);}
uint32_t hashname(const char *s){uint32_t h=2166136261u;while(*s){h^=static_cast<unsigned char>(*s++);h*=16777619u;}return h;}
void prefkey(size_t i,char *key,size_t n){std::snprintf(key,n,"c%08lx",static_cast<unsigned long>(hashname(files.names[i])));}
void load_settings(){nvs_handle_t h;if(nvs_open("cal_ui",NVS_READONLY,&h)!=ESP_OK)return;for(size_t i=0;i<files.count;++i){char key[16];prefkey(i,key,sizeof(key));uint8_t v;if(nvs_get_u8(h,key,&v)==ESP_OK && (v&7)<5){settings[i].colour=static_cast<Colour>(v&7);settings[i].visible=(v&8)!=0;}}nvs_close(h);}
void save_setting(size_t i){nvs_handle_t h;if(i>=files.count||nvs_open("cal_ui",NVS_READWRITE,&h)!=ESP_OK)return;char key[16];prefkey(i,key,sizeof(key));nvs_set_u8(h,key,static_cast<uint8_t>(settings[i].colour)|(settings[i].visible?8:0));nvs_commit(h);nvs_close(h);}
FetchOutcome fetch_attempt(const char *url,std::string &body,void *){
    esp_http_client_config_t cfg{};cfg.url=url;cfg.crt_bundle_attach=esp_crt_bundle_attach;cfg.disable_auto_redirect=true;cfg.timeout_ms=20000;cfg.buffer_size=2048;
    esp_http_client_handle_t client=esp_http_client_init(&cfg);if(!client)return {};FetchOutcome result{};
    if(esp_http_client_open(client,0)==ESP_OK){int64_t length=esp_http_client_fetch_headers(client);result.http_status=esp_http_client_get_status_code(client);
        if(length>=0){if(result.http_status!=200)result.error=FetchError::http;else if(length>4*1024*1024)result.error=FetchError::oversized;else{
            result.error=FetchError::incomplete;char chunk[2048];int64_t deadline=esp_timer_get_time()+45000000;
            while(esp_timer_get_time()<deadline){int n=esp_http_client_read(client,chunk,sizeof(chunk));if(n<0)break;if(n>0){if(body.size()+size_t(n)>4*1024*1024){result.error=FetchError::oversized;break;}body.append(chunk,size_t(n));}if(esp_http_client_is_complete_data_received(client)){result.error=FetchError::none;break;}if(!n)break;vTaskDelay(1);}
        }}
    }esp_http_client_close(client);esp_http_client_cleanup(client);return result;
}
void fetch_task(void *){
    auto *result=new FetchResult{};
    for(size_t i=0;i<feeds.count;++i){std::string body;result->outcome[i]=fetch_with_retry(feeds.feeds[i].url,body,fetch_attempt,nullptr);if(result->outcome[i].error!=FetchError::none){++result->failed;continue;}auto parsed=parse_ics(body.data(),body.size(),first_feed+i);if(parsed.fatal){result->outcome[i].error=FetchError::invalid_calendar;++result->failed;continue;}result->ok[i]=true;result->rejected[i]=parsed.warnings;result->warnings+=parsed.warnings;result->items.insert(result->items.end(),parsed.events.begin(),parsed.events.end());}
    xQueueSend(feed_queue,&result,portMAX_DELAY);vTaskDelete(nullptr);
}
void load_files(){
    events.clear();warnings=0;feed_config_invalid=false;feeds.count=0;auto res=scan_files(root,files);
    if(res!=ScanResult::ok){std::snprintf(status,sizeof(status),"Put .ics files in /files/Calendar on SD");return;}
    first_feed=files.count;
    char feedpath[180];std::snprintf(feedpath,sizeof(feedpath),"%s/feeds.txt",root);FILE *feedfile=std::fopen(feedpath,"rb");
    if(feedfile){static char raw[8192];size_t n=std::fread(raw,1,sizeof(raw),feedfile);bool too_big=n==sizeof(raw)&&std::fgetc(feedfile)!=EOF;bool io=std::ferror(feedfile);std::fclose(feedfile);bool valid_feeds=!too_big&&!io&&parse_feeds(raw,n,feeds)&&files.count+feeds.count<=max_files;std::memset(raw,0,sizeof(raw));if(!valid_feeds){feeds.count=0;feed_config_invalid=true;++warnings;}else{for(size_t i=0;i<feeds.count;++i)std::snprintf(files.names[files.count++],max_filename,"%s",feeds.feeds[i].name);}}
    for(size_t i=0;i<files.count;++i){settings[i]={true,static_cast<Colour>(i%5)};std::snprintf(calendar_status[i],sizeof(calendar_status[i]),"%s",i<first_feed?"LOCAL FILE":"WAITING TO LOAD");}
    load_settings();
    for(size_t i=0;i<first_feed;++i){
        char path[300];std::snprintf(path,sizeof(path),"%s/%s",root,files.names[i]);FILE *f=std::fopen(path,"rb");if(!f){++warnings;std::snprintf(calendar_status[i],sizeof(calendar_status[i]),"READ ERROR");continue;}
        if(std::fseek(f,0,SEEK_END)!=0){std::fclose(f);++warnings;std::snprintf(calendar_status[i],sizeof(calendar_status[i]),"READ ERROR");continue;}long n=std::ftell(f);if(n<0||n>4*1024*1024||std::fseek(f,0,SEEK_SET)!=0){std::fclose(f);++warnings;std::snprintf(calendar_status[i],sizeof(calendar_status[i]),"TOO LARGE OR READ ERROR");continue;}
        std::string data(static_cast<size_t>(n),'\0');size_t read=std::fread(data.data(),1,data.size(),f);bool failed=std::ferror(f);std::fclose(f);if(failed||read!=data.size()){++warnings;std::snprintf(calendar_status[i],sizeof(calendar_status[i]),"READ ERROR");continue;}
        auto parsed=parse_ics(data.data(),data.size(),i);warnings+=parsed.warnings+(parsed.fatal?1:0);if(parsed.fatal)std::snprintf(calendar_status[i],sizeof(calendar_status[i]),"INVALID ICS");else if(parsed.warnings)std::snprintf(calendar_status[i],sizeof(calendar_status[i]),"%u UNSUPPORTED",parsed.warnings);
        if(events.size()+parsed.events.size()>8192){++warnings;std::snprintf(calendar_status[i],sizeof(calendar_status[i]),"EVENT LIMIT");continue;}
        events.insert(events.end(),parsed.events.begin(),parsed.events.end());
    }
    std::stable_sort(events.begin(),events.end(),[](const Event &a,const Event &b){if(a.all_day!=b.all_day)return a.all_day;return a.minute<b.minute;});
    local_warnings=warnings;
    if(feed_config_invalid)std::snprintf(status,sizeof(status),"Invalid feeds.txt - check names and URLs");
    else if(warnings)std::snprintf(status,sizeof(status),"WARNING: %u unsupported events or files; calendar may be incomplete",warnings);
    else if(feeds.count)std::snprintf(status,sizeof(status),"%u calendars | waiting for online feeds",unsigned(files.count));
    else std::snprintf(status,sizeof(status),"%u calendars loaded | /files/Calendar",unsigned(files.count));
}
bool visible(const Event &e){return e.calendar<files.count&&settings[e.calendar].visible;}
void build_agenda(){agenda.clear();agenda_truncated=false;Date d=nav.date;for(int day=0;day<366;++day){for(size_t i=0;i<events.size();++i)if(visible(events[i])&&occurs(events[i],d)){if(agenda.size()==3000){agenda_truncated=true;break;}agenda.push_back({d,i});}if(agenda_truncated)break;Date next=add_days(d,1);if(compare(next,d)==0)break;d=next;}
    std::sort(agenda.begin(),agenda.end(),[](const Row&a,const Row&b){int c=compare(a.date,b.date);if(c)return c<0;const Event&x=events[a.event];const Event&y=events[b.event];if(x.all_day!=y.all_day)return x.all_day;int xm=starts_on(x,a.date)?occurrence_minute(x,a.date):0;int ym=starts_on(y,b.date)?occurrence_minute(y,b.date):0;return xm<ym;});
    if(nav.agenda_offset>=agenda.size())nav.agenda_offset=agenda.empty()?0:agenda.size()-1;
}
void draw_event(const Event &e,Date d,int x,int y,int width,int scale=2){char label[170];if(!starts_on(e,d))std::snprintf(label,sizeof(label),"CONT %s",e.title.c_str());else if(e.all_day)std::snprintf(label,sizeof(label),"%s",e.title.c_str());else{int minute=occurrence_minute(e,d);std::snprintf(label,sizeof(label),"%02d:%02d %s",minute/60,minute%60,e.title.c_str());}rect(x,y+2,4,scale*7-1,colours[static_cast<int>(settings[e.calendar].colour)]);text(label,x+8,y,colours[static_cast<int>(settings[e.calendar].colour)],scale,width-8);}
void draw_month(){
    Date first=month_grid_start(nav.date);int cellw=180,cellh=85;for(int c=0;c<7;++c)text(days[c],13+c*cellw,75,muted);
    for(int row=0;row<6;++row)for(int col=0;col<7;++col){Date d=add_days(first,row*7+col);int x=8+col*cellw,y=100+row*cellh;rect(x,y,cellw-3,cellh-3,panel);frame(x,y,cellw-3,cellh-3,compare(d,nav.date)==0?0x56a8ff:border);char date[8];std::snprintf(date,sizeof(date),"%d",d.day);text(date,x+6,y+5,d.month==nav.date.month?white:muted);int count=0;for(const auto&e:events)if(visible(e)&&occurs(e,d)){if(count<2)draw_event(e,d,x+5,y+24+count*19,cellw-14);++count;}if(count>2){char more[20];std::snprintf(more,sizeof(more),"+%d more",count-2);text(more,x+6,y+64,muted,1);}}
}
void draw_week(){Date start=week_start(nav.date);week_scroll_max=0;for(int c=0;c<7;++c){Date d=add_days(start,c);int x=8+c*180;rect(x,80,177,530,panel);frame(x,80,177,530,compare(d,nav.date)==0?0x56a8ff:border);char label[32];std::snprintf(label,sizeof(label),"%s %d/%d",days[c],d.day,d.month);text(label,x+6,90,white);int count=0;for(const auto&e:events)if(visible(e)&&occurs(e,d)){if(count>=week_scroll&&count<week_scroll+23)draw_event(e,d,x+5,120+(count-week_scroll)*20,166);++count;}week_scroll_max=std::max(week_scroll_max,std::max(0,count-23));if(count>week_scroll+23)text("MORE BELOW",x+6,590,0xf5b84b,2);}}
void draw_agenda(){build_agenda();if(agenda.empty()){text("No events in the next year",20,110,muted,layout::large_text_scale);return;}for(size_t i=nav.agenda_offset;i<agenda.size()&&i<nav.agenda_offset+13;++i){int y=85+int(i-nav.agenda_offset)*40;const Row&r=agenda[i];const Event&e=events[r.event];char date[40];std::snprintf(date,sizeof(date),"%s %02d %.3s",days[weekday(r.date)],r.date.day,months[r.date.month-1]);text(date,18,y,white,layout::large_text_scale,232);draw_event(e,r.date,260,y,950,layout::large_text_scale);rect(10,y+30,1250,1,border);}text(agenda_truncated?"NEXT YEAR - LIST LIMITED TO 3000":"NEXT YEAR FROM SELECTED DATE",690,604,muted,layout::large_text_scale,570); }
void draw_calendars(){for(size_t i=0;i<files.count && i<16;++i){int col=i/8,row=i%8,x=15+int(col)*620,y=90+int(row)*61;rect(x,y,600,52,raised);frame(x,y,600,52,i==nav.calendar_selection?0x56a8ff:border);uint32_t c=colours[static_cast<int>(settings[i].colour)];rect(x+8,y+10,20,30,c);text(settings[i].visible?"ON":"OFF",x+40,y+10,settings[i].visible?c:muted,layout::large_text_scale);text(files.names[i],x+105,y+4,white,layout::large_text_scale,320);text(calendar_status[i],x+105,y+28,muted,layout::large_text_scale,320);text("COLOUR >",x+440,y+14,c,layout::large_text_scale,150);}}
void draw(){static int last_view=-1;rect(0,0,1280,50,panel);text("TAB5 CALENDAR",15,16,white,2);char heading[100];std::snprintf(heading,sizeof(heading),"%s %d  |  %02d/%02d/%04d",months[nav.date.month-1],nav.date.year,nav.date.day,nav.date.month,nav.date.year);text(heading,350,16,0x56a8ff,2);if(!clock_ok)text("CLOCK NOT SET",1000,17,0xf5b84b,2);
    if(tab5_display_present_region(0,0,1280,50)!=ESP_OK)render_ok=false;
    rect(0,50,1280,layout::toolbar_y-50,bg);
    if(nav.view==View::month)draw_month();else if(nav.view==View::week)draw_week();else if(nav.view==View::agenda)draw_agenda();else draw_calendars();
    text(status,12,628,warnings?0xf5b84b:muted,2,1250);
    if(tab5_display_present_region(0,50,1280,layout::toolbar_y-50)!=ESP_OK)render_ok=false;
    if(last_view!=static_cast<int>(nav.view)){
        rect(0,layout::toolbar_y,1280,720-layout::toolbar_y,bg);button(0,"MONTH",nav.view==View::month?0x56a8ff:muted);button(1,"WEEK",nav.view==View::week?0x55d187:muted);button(2,"AGENDA",nav.view==View::agenda?0xf5b84b:muted);button(3,"CALENDARS",nav.view==View::calendars?0xa855f7:muted);button(4,"PREV",white);button(5,"NEXT",white);button(6,"CONSOLE",0xf5b84b);
        if(tab5_display_present_region(0,layout::toolbar_y,1280,720-layout::toolbar_y)!=ESP_OK)render_ok=false;
        last_view=static_cast<int>(nav.view);
    }
    dirty=false;
}
void change_view(View v){nav.view=v;nav.agenda_offset=0;dirty=true;}
void start_radio(){if(!feeds.count||radio_started)return;if(tab5_radio_power_set(true)==ESP_OK){vTaskDelay(pdMS_TO_TICKS(250));if(tab5_radio_init()==ESP_OK&&tab5_radio_set_wifi_enabled(true)==ESP_OK){radio_started=true;return;}}std::snprintf(status,sizeof(status),"Wi-Fi unavailable: use Console to set up network");dirty=true;}
void set_feed_status(size_t i,const FetchOutcome &outcome,unsigned rejected,bool applied){
    char *label=calendar_status[first_feed+i];size_t cap=sizeof(calendar_status[0]);
    if(outcome.error==FetchError::none){if(!applied)std::snprintf(label,cap,"EVENT LIMIT");else if(rejected)std::snprintf(label,cap,"%u UNSUPPORTED",rejected);else std::snprintf(label,cap,"UP TO DATE");}
    else if(outcome.error==FetchError::http)std::snprintf(label,cap,"HTTP %d",outcome.http_status);
    else if(outcome.error==FetchError::oversized)std::snprintf(label,cap,"FEED TOO LARGE");
    else if(outcome.error==FetchError::invalid_calendar)std::snprintf(label,cap,"INVALID ICS");
    else if(outcome.error==FetchError::incomplete)std::snprintf(label,cap,"INCOMPLETE");
    else std::snprintf(label,cap,"NETWORK/TLS ERROR");
}
void action(int a){if(a<4)change_view(static_cast<View>(a));else if(a==4||a==5){int n=a==4?-1:1;if(nav.view==View::month)nav.date=add_months(nav.date,n);else if(nav.view==View::week){nav.date=add_days(nav.date,n*7);week_scroll=0;}else if(nav.view==View::agenda)move(nav,n<0?Direction::left:Direction::right,agenda.size(),files.count);else if(files.count){cycle_colour(settings[nav.calendar_selection],n);save_setting(nav.calendar_selection);}dirty=true;}else if(a==6){tab5_sd_unmount();tab5_app_boot_return_to_console();}}
void key(uint8_t c){static int esc=0;if(esc==1){esc=c=='['?2:0;return;}if(esc==2){esc=0;Direction d;if(c=='A')d=Direction::up;else if(c=='B')d=Direction::down;else if(c=='C')d=Direction::right;else if(c=='D')d=Direction::left;else return;if(nav.view==View::calendars&&(d==Direction::left||d==Direction::right)&&files.count){cycle_colour(settings[nav.calendar_selection],d==Direction::left?-1:1);save_setting(nav.calendar_selection);}else if(nav.view==View::week&&(d==Direction::up||d==Direction::down)){week_scroll=std::clamp(week_scroll+(d==Direction::up?-1:1),0,week_scroll_max);}else {move(nav,d,agenda.size(),files.count);if(nav.view==View::week)week_scroll=0;}dirty=true;return;}if(c==27){esc=1;return;}if(c=='1')action(0);else if(c=='2')action(1);else if(c=='3')action(2);else if(c=='4')action(3);else if((c==' '||c=='\r')&&nav.view==View::calendars&&files.count){toggle(settings[nav.calendar_selection]);save_setting(nav.calendar_selection);dirty=true;}else if((c=='r'||c=='R')&&!feed_busy){load_files();start_radio();next_fetch=0;dirty=true;}}
void input(const uint8_t *data,size_t n){if(queue)for(size_t i=0;i<n;++i)xQueueSend(queue,&data[i],0);}
}
extern "C" void app_main(){
    esp_log_level_set("HTTP_CLIENT",ESP_LOG_NONE);esp_log_level_set("esp-tls",ESP_LOG_NONE);
    setenv("TZ","AEST-10AEDT,M10.1.0,M4.1.0/3",1);tzset();
    bool nvs_ok=nvs_flash_init()==ESP_OK;auto board=tab5_board_init();tab5_display_set_rotation(TAB5_ROTATION_0);
    tab5_clock_time now{};if(tab5_rtc_read(&now)&&valid({now.year,now.month,now.day})&&!tab5_rtc_was_initialized_from_build()){nav.date={now.year,now.month,now.day};clock_ok=true;}
    queue=xQueueCreate(128,sizeof(uint8_t));feed_queue=xQueueCreate(1,sizeof(FetchResult*));if(queue)tab5::start_tab5_kbd_input(input);
    if(board.sd_ready)load_files();else std::snprintf(status,sizeof(status),"SD card unavailable");
    if(board.display_ready)draw();
    bool healthy=nvs_ok&&board.display_ready&&board.touch_ready&&board.sd_ready&&queue&&feed_queue&&render_ok&&tab5_app_boot_console_available();
    bool confirmed=healthy&&tab5_app_boot_ready(true)==ESP_OK;int64_t deadline=esp_timer_get_time()+15000000;
    if(confirmed)start_radio();
    bool touch=false,wifi_notified=false;unsigned released=0;int x=0,y=0;int64_t network_deadline=esp_timer_get_time()+30000000;
    while(true){if(!confirmed&&esp_timer_get_time()>deadline)tab5_app_boot_return_to_console();
        if(feeds.count&&radio_started){tab5_radio_status_t radio{};tab5_radio_get_status(&radio);if(radio.wifi_has_ip&&!sntp_started){esp_sntp_config_t cfg=ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");sntp_started=esp_netif_sntp_init(&cfg)==ESP_OK;}
            if(!radio.wifi_has_ip&&!wifi_notified&&esp_timer_get_time()>network_deadline){std::snprintf(status,sizeof(status),"Wi-Fi not connected - configure a network in Console");wifi_notified=true;dirty=true;}
            if(!clock_ok&&std::time(nullptr)>=1704067200){time_t t=std::time(nullptr);tm local{};if(localtime_r(&t,&local)){Date d{local.tm_year+1900,local.tm_mon+1,local.tm_mday};if(valid(d)){nav.date=d;clock_ok=true;dirty=true;}}}
            if(radio.wifi_has_ip&&std::time(nullptr)>=1704067200&&!feed_busy&&esp_timer_get_time()>=next_fetch){feed_busy=true;if(xTaskCreate(fetch_task,"calendar_fetch",16384,nullptr,3,nullptr)!=pdPASS){feed_busy=false;next_fetch=esp_timer_get_time()+60000000;std::snprintf(status,sizeof(status),"Unable to start calendar refresh");dirty=true;}else{std::snprintf(status,sizeof(status),"Refreshing online calendars...");dirty=true;}}}
        FetchResult *result=nullptr;
        if(feed_queue&&xQueueReceive(feed_queue,&result,0)==pdTRUE){
            size_t retained=0;
            for(const auto&e:events){bool replaced=e.calendar>=first_feed&&e.calendar<first_feed+feeds.count&&result->ok[e.calendar-first_feed];if(!replaced)++retained;}
            bool applied=retained+result->items.size()<=8192;
            if(applied){for(size_t i=0;i<feeds.count;++i)if(result->ok[i])events.erase(std::remove_if(events.begin(),events.end(),[i](const Event&e){return e.calendar==first_feed+i;}),events.end());events.insert(events.end(),result->items.begin(),result->items.end());}
            else ++result->warnings;
            for(size_t i=0;i<feeds.count;++i)set_feed_status(i,result->outcome[i],result->rejected[i],applied);
            warnings=local_warnings+result->warnings+result->failed;
            if(result->failed)std::snprintf(status,sizeof(status),"%u feed errors - see Calendars; %u unsupported items",result->failed,local_warnings+result->warnings);
            else if(warnings)std::snprintf(status,sizeof(status),"%u unsupported items - see Calendars",warnings);
            else std::snprintf(status,sizeof(status),"Calendars updated | refresh every 30 min");
            std::stable_sort(events.begin(),events.end(),[](const Event&a,const Event&b){if(a.all_day!=b.all_day)return a.all_day;return a.minute<b.minute;});
            bool failed=result->failed>0;delete result;feed_busy=false;next_fetch=esp_timer_get_time()+(failed?60000000LL:1800000000LL);dirty=true;
        }
        uint8_t usb[32];size_t n=tab5_keyboard_poll(usb,sizeof(usb));for(size_t i=0;i<n;++i)key(usb[i]);uint8_t ch;while(queue&&xQueueReceive(queue,&ch,0)==pdTRUE)key(ch);
        int tx=0,ty=0;if(tab5_touch_poll(&tx,&ty)){if(!touch){touch=true;x=tx;y=ty;}released=0;}else if(touch&&++released>=2){touch=false;int toolbar=layout::toolbar_action(x,y);if(toolbar>=0)action(toolbar);else if(nav.view==View::calendars&&y>=90&&y<578){size_t i=(x>=635?8:0)+(y-90)/61;if(i<files.count){nav.calendar_selection=i;if((x%620)>440)cycle_colour(settings[i],1);else toggle(settings[i]);save_setting(i);dirty=true;}}else if(nav.view==View::month&&y>=100&&y<610){int col=(x-8)/180,row=(y-100)/85;if(col>=0&&col<7&&row<6){nav.date=add_days(month_grid_start(nav.date),row*7+col);dirty=true;}}}
        if(dirty&&board.display_ready)draw();
        vTaskDelay(pdMS_TO_TICKS(20));}
}
