#include "calendar_events.h"
#include "ical_lines.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

namespace calendar {
namespace {
int serial_day(Date d);
bool dateval(const char *v, Date &d) {
    if (std::strlen(v) < 8) return false;
    for (int i=0;i<8;++i) if (!std::isdigit(static_cast<unsigned char>(v[i]))) return false;
    d={(v[0]-'0')*1000+(v[1]-'0')*100+(v[2]-'0')*10+v[3]-'0', (v[4]-'0')*10+v[5]-'0', (v[6]-'0')*10+v[7]-'0'};
    return valid(d);
}
bool datetime(const std::string &key,const char *v,Date &d,int &minute,bool &all) {
    if (!dateval(v,d)) return false;
    size_t n=std::strlen(v); all = n==8;
    if (all) { minute=0; return true; }
    if ((n!=15 && n!=16) || v[8]!='T' || (n==16 && v[15]!='Z')) return false;
    for(int i=9;i<15;++i) if(!std::isdigit(static_cast<unsigned char>(v[i]))) return false;
    int h=(v[9]-'0')*10+v[10]-'0', m=(v[11]-'0')*10+v[12]-'0', s=(v[13]-'0')*10+v[14]-'0';
    if(h>23||m>59||s>59) return false;
    size_t tz=key.find("TZID=");
    int source_offset=0;bool fixed_zone=false;
    if(tz!=std::string::npos){
        std::string zone=key.substr(tz+5);size_t end=zone.find(';');if(end!=std::string::npos)zone.resize(end);
        if(zone.size()>=2&&zone.front()=='"'&&zone.back()=='"')zone=zone.substr(1,zone.size()-2);
        if(zone=="Asia/Seoul" && d.year>=2000){source_offset=9*60;fixed_zone=true;}
        else if(zone=="America/Bogota" && d.year>=2000){source_offset=-5*60;fixed_zone=true;}
        else if(zone!="Australia/Sydney"&&zone!="Australia/Melbourne"&&zone!="AUS Eastern Standard Time")return false;
    }
    minute=h*60+m;
    if(n==16||fixed_zone) {
        int64_t seconds=int64_t(serial_day(d)-serial_day({1970,1,1}))*86400+h*3600+m*60+s-source_offset*60;
        time_t utc=static_cast<time_t>(seconds); tm loc{}; if(!localtime_r(&utc,&loc)) return false;
        d={loc.tm_year+1900,loc.tm_mon+1,loc.tm_mday};minute=loc.tm_hour*60+loc.tm_min;
    }
    return valid(d);
}
std::string unescape(const char *v) {
    std::string out;
    while (*v && out.size()<120) { char c=*v++; if(c=='\\' && *v) { c=*v++; if(c=='n'||c=='N') c=' '; } if(static_cast<unsigned char>(c)>=32) out+=c; }
    return out.empty()?"(untitled)":out;
}
bool rule(const char *value, Event &e) {
    std::string s(value); size_t p=0; bool freq=false;
    while(p<s.size()) {
        size_t q=s.find(';',p); if(q==std::string::npos) q=s.size();
        auto part=s.substr(p,q-p); auto at=part.find('='); if(at==std::string::npos) return false;
        auto k=part.substr(0,at), v=part.substr(at+1);
        if(k=="FREQ") { freq=true; if(v=="DAILY")e.frequency='D'; else if(v=="WEEKLY")e.frequency='W'; else if(v=="MONTHLY")e.frequency='M'; else if(v=="YEARLY")e.frequency='Y'; else return false; }
        else if(k=="INTERVAL" || k=="COUNT") { char *end=nullptr; long n=std::strtol(v.c_str(),&end,10); if(!end||*end||n<1||n>100000) return false; if(k=="COUNT")e.count=n;else e.interval=n; }
        else if(k=="UNTIL") { bool all=false;if(!datetime("",v.c_str(),e.until,e.until_minute,all))return false;if(all)e.until_minute=1439; }
        else if(k=="BYDAY") { const char *names[]={"MO","TU","WE","TH","FR","SA","SU"}; size_t x=0; while(x<v.size()) { size_t y=v.find(',',x);if(y==std::string::npos)y=v.size(); std::string day=v.substr(x,y-x);if(day.size()<2)return false;bool found=false;for(int i=0;i<7;++i) if(day.substr(day.size()-2)==names[i]){e.weekdays|=1<<i;found=true;}if(!found)return false;if(day.size()>2){std::string prefix=day.substr(0,day.size()-2);char *tail=nullptr;long ordinal=std::strtol(prefix.c_str(),&tail,10);if(!tail||*tail||ordinal==0||ordinal < -5||ordinal>5||e.weekday_position||x!=0||y!=v.size())return false;e.weekday_position=int(ordinal);}x=y+1; } }
        else if(k=="BYSETPOS"||k=="BYMONTH"||k=="BYMONTHDAY"){char *tail=nullptr;long n=std::strtol(v.c_str(),&tail,10);if(!tail||*tail)return false;if(k=="BYSETPOS"){if(n==0||n < -5||n>5||e.weekday_position)return false;e.weekday_position=int(n);}else if(k=="BYMONTH"){if(n<1||n>12)return false;e.month=int(n);}else{if(n<1||n>31)return false;e.month_day=int(n);}}
        else if(k=="WKST") {const char *names[]={"MO","TU","WE","TH","FR","SA","SU"};bool found=false;for(int i=0;i<7;++i)if(v==names[i]){e.week_start_day=i;found=true;}if(!found)return false;}
        else return false;
        p=q+1;
    }
    if(e.weekdays && e.frequency!='W' && !e.weekday_position) return false;
    if(e.weekday_position && (!e.weekdays || (e.frequency!='M'&&e.frequency!='Y'))) return false;
    if(e.month_day && e.weekdays) return false;
    if(e.month && e.frequency!='Y') return false;
    return freq;
}
int serial_day(Date d) {
    int y=d.year-(d.month<=2); int era=(y>=0?y:y-399)/400;
    unsigned yoe=static_cast<unsigned>(y-era*400);
    unsigned mp=static_cast<unsigned>(d.month+(d.month>2?-3:9));
    unsigned doy=(153*mp+2)/5+static_cast<unsigned>(d.day)-1;
    return era*146097+static_cast<int>(yoe*365+yoe/4-yoe/100+doy);
}
int days_between(Date a,Date b) { return serial_day(b)-serial_day(a); }
int occurrence_day(const Event &e,int year,int month){
    if(!e.weekday_position){int day=e.month_day?e.month_day:(e.fixed_source_zone?e.source_start.day:e.start.day);return valid({year,month,day})?day:0;}
    int matches[31];int n=0;for(int day=1;day<=31;++day){Date d{year,month,day};if(valid(d)&&(e.weekdays&(1<<weekday(d))))matches[n++]=day;}
    int i=e.weekday_position>0?e.weekday_position-1:n+e.weekday_position;return i>=0&&i<n?matches[i]:0;
}
}
ParseResult parse_ics(const char *data,size_t length,size_t ci) {
    struct Override { std::string uid; Date original; Event event; bool cancelled; bool valid_event; };
    struct Additional { Date date; int minute; bool all_day; };
    ParseResult out; std::vector<Override> overrides;std::vector<Additional> additional; size_t pos=0; std::vector<char> line_buffer(16384);char *line=line_buffer.data();bool inside=false, nested=false, have_start=false, bad=false, cancelled=false, utc_start=false, is_override=false, override_valid=false, got_calendar=false, got_end=false;Date original{}; Event e;
    while(true) {
        auto result=next_line(data,length,pos,line,line_buffer.size());
        if(result==LineResult::end) break;
        if(result!=LineResult::line){out.fatal=true;break;}
        if(!line[0])continue;
        if(!std::strcmp(line,"BEGIN:VCALENDAR")){got_calendar=true;continue;}
        if(!std::strcmp(line,"END:VCALENDAR")){got_end=true;continue;}
        if(!std::strcmp(line,"BEGIN:VEVENT")){if(inside){out.fatal=true;break;} inside=true;nested=false;have_start=bad=cancelled=utc_start=is_override=override_valid=false;original={};e=Event{};e.calendar=ci;additional.clear();continue;}
        if(!inside)continue;
        if(!std::strcmp(line,"END:VEVENT")) {
            if(nested){out.fatal=true;break;}
            if(is_override){
                if(!override_valid||e.uid.empty()){++out.warnings;}
                else {bool valid_event=!bad&&have_start&&!e.recurring;if(!cancelled&&!valid_event)++out.warnings;if(valid_event){if(!valid(e.end))e.end=e.all_day?add_days(e.start,1):e.start;if(e.title.empty())e.title="(untitled)";}overrides.push_back({e.uid,original,e,cancelled,valid_event});}
            } else if(!cancelled) { if(!have_start||bad||(e.recurring&&valid(e.end)&&days_between(e.start,e.end)>31)){++out.warnings;} else { if(!valid(e.end))e.end=e.all_day?add_days(e.start,1):e.start; if(e.title.empty())e.title="(untitled)"; if(out.events.size()+1+additional.size()>8192){out.fatal=true;break;}out.events.push_back(e);int duration=days_between(e.start,e.end)*1440+e.end_minute-e.minute;for(const auto &a:additional){if(a.all_day!=e.all_day||duration<0||duration>31*1440){++out.warnings;continue;}if(starts_on(e,a.date)&&(e.all_day||occurrence_minute(e,a.date)==a.minute))continue;Event extra=e;extra.recurring=false;extra.excluded.clear();extra.start=a.date;extra.minute=a.minute;int end=a.minute+duration;extra.end=add_days(a.date,end/1440);extra.end_minute=end%1440;out.events.push_back(extra);} } }
            inside=false;continue;
        }
        if(!std::strncmp(line,"BEGIN:",6)){nested=true;continue;}
        if(!std::strncmp(line,"END:",4)){nested=false;continue;}
        if(nested)continue;
        char *colon=std::strchr(line,':');if(!colon){bad=true;continue;} *colon=0; std::string key(line); const char *v=colon+1;
        std::string name=key.substr(0,key.find(';'));
        if(name=="DTSTART"){bool all=false; if(!datetime(key,v,e.start,e.minute,all))bad=true;else{e.all_day=all;have_start=true;size_t n=std::strlen(v);utc_start=n&&v[n-1]=='Z';e.fixed_source_zone=utc_start||key.find("Asia/Seoul")!=std::string::npos||key.find("America/Bogota")!=std::string::npos;if(e.fixed_source_zone){dateval(v,e.source_start);e.source_minute=(v[9]-'0')*600+(v[10]-'0')*60+(v[11]-'0')*10+v[12]-'0';e.source_utc_offset=utc_start?0:key.find("Asia/Seoul")!=std::string::npos?540:-300;}}}
        else if(name=="DTEND"){bool all=false;if(!datetime(key,v,e.end,e.end_minute,all))bad=true;}
        else if(name=="SUMMARY")e.title=unescape(v);
        else if(name=="UID") {if(std::strlen(v)>255)bad=true;else e.uid=v;}
        else if(name=="STATUS" && !std::strcmp(v,"CANCELLED"))cancelled=true;
        else if(name=="RRULE") {e.recurring=true;if(!rule(v,e))bad=true;}
        else if(name=="EXDATE") {std::string values(v);size_t p=0;while(p<values.size()){size_t q=values.find(',',p);if(q==std::string::npos)q=values.size();Date d{};int min;bool all;if(!datetime(key,values.substr(p,q-p).c_str(),d,min,all)){bad=true;break;} e.excluded.push_back(d);p=q+1;}}
        else if(name=="RECURRENCE-ID") {is_override=true;int min=0;bool all=false;override_valid=datetime(key,v,original,min,all);if(key.find("RANGE=")!=std::string::npos)bad=true;}
        else if(name=="RDATE") {std::string values(v);size_t p=0;while(p<values.size()){size_t q=values.find(',',p);if(q==std::string::npos)q=values.size();Additional a{};if(!datetime(key,values.substr(p,q-p).c_str(),a.date,a.minute,a.all_day)||additional.size()>=256){bad=true;break;}additional.push_back(a);p=q+1;}}
        else if(name=="DURATION")bad=true;
    }
    if(inside || !got_calendar || !got_end)out.fatal=true;
    if(!out.fatal){for(const auto &o:overrides){for(auto &base:out.events)if(base.recurring&&base.uid==o.uid)base.excluded.push_back(o.original);if(!o.cancelled&&o.valid_event){if(out.events.size()>=8192){out.fatal=true;break;}out.events.push_back(o.event);}}}
    if(out.fatal)out.events.clear();
    return out;
}
static bool matches_rule(const Event &e, Date d, Date start) {
    if(compare(d,start)<0)return false;
    int delta=days_between(start,d); bool match=false; int ordinal=0;
    if(e.frequency=='D'){match=delta%e.interval==0;ordinal=delta/e.interval+1;}
    else if(e.frequency=='W'){
        int mask=e.weekdays?e.weekdays:(1<<weekday(start));int start_index=(weekday(start)-e.week_start_day+7)%7;int day_index=(weekday(d)-e.week_start_day+7)%7;Date start_week=add_days(start,-start_index);Date date_week=add_days(d,-day_index);int diff=days_between(start_week,date_week);
        match=(diff/7)%e.interval==0 && (mask&(1<<weekday(d)));
        if(e.count && match){
            int per_week=0;for(int i=0;i<7;++i)if(mask&(1<<i))++per_week;
            int active_weeks=(diff/7)/e.interval;
            ordinal=active_weeks*per_week;
            for(int i=0;i<start_index;++i)if(mask&(1<<((i+e.week_start_day)%7)))--ordinal;
            for(int i=0;i<=day_index;++i)if(mask&(1<<((i+e.week_start_day)%7)))++ordinal;
        }
    } else if(e.frequency=='M') {int diff=(d.year-start.year)*12+d.month-start.month;match=diff>=0&&diff%e.interval==0&&d.day==occurrence_day(e,d.year,d.month);if(e.count&&match){Date first{start.year,start.month,1};for(int i=0;i<=diff;i+=e.interval){Date x=add_months(first,i);x.day=occurrence_day(e,x.year,x.month);if(valid(x)&&compare(x,start)>=0&&compare(x,d)<=0)++ordinal;}}}
    else if(e.frequency=='Y'){int diff=d.year-start.year;int month=e.month?e.month:start.month;match=diff>=0&&diff%e.interval==0&&d.month==month&&d.day==occurrence_day(e,d.year,month);if(e.count&&match){for(int i=0;i<=diff;i+=e.interval){Date x{start.year+i,month,occurrence_day(e,start.year+i,month)};if(valid(x)&&compare(x,start)>=0&&compare(x,d)<=0)++ordinal;}}}
    return match && (!e.count || ordinal<=e.count);
}
static int fixed_occurrence(const Event &e,Date d){
    for(int delta=-1;delta<=1;++delta){Date source=add_days(d,delta);if(!matches_rule(e,source,e.source_start))continue;
        int64_t seconds=int64_t(serial_day(source)-serial_day({1970,1,1}))*86400+e.source_minute*60-e.source_utc_offset*60;
        time_t t=static_cast<time_t>(seconds);tm local{};if(!localtime_r(&t,&local))continue;
        Date actual{local.tm_year+1900,local.tm_mon+1,local.tm_mday};if(compare(actual,d)==0)return local.tm_hour*60+local.tm_min;
    }return -1;
}
int occurrence_minute(const Event &e, Date d){
    return e.recurring&&e.fixed_source_zone?fixed_occurrence(e,d):e.minute;
}
bool starts_on(const Event &e, Date d) {
    if(!e.recurring)return compare(d,e.start)==0;
    if(compare(d,e.until)>0)return false;
    int minute=occurrence_minute(e,d);if(minute<0)return false;
    if(compare(d,e.until)==0 && !e.all_day && minute>e.until_minute)return false;
    for(auto x:e.excluded)if(compare(x,d)==0)return false;
    return e.fixed_source_zone||matches_rule(e,d,e.start);
}
bool occurs(const Event &e, Date d) {
    if(!e.recurring)return compare(d,e.start)>=0&&(e.all_day?compare(d,e.end)<0:(compare(d,e.end)<0||(compare(d,e.end)==0&&(e.end_minute>0||compare(e.start,e.end)==0))));
    int span=days_between(e.start,e.end);if(span>0&&(e.all_day||e.end_minute==0))--span;
    if(span<0||span>31)return false;
    for(int i=0;i<=span;++i){Date start=add_days(d,-i);if(starts_on(e,start))return true;}
    return false;
}
}
