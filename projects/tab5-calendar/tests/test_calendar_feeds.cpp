#include "../src/calendar_feeds.h"
#include <cassert>
#include <cstring>
using namespace calendar;
int main(){
 const char text[]="Work|https://calendar.google.com/calendar/ical/example/public/basic.ics\r\nHome|https://calendar.google.com/calendar/ical/example2/public/basic.ics\n";
 FeedList f{};assert(parse_feeds(text,sizeof(text)-1,f)&&f.count==2&&!std::strcmp(f.feeds[0].name,"Work"));
 const char bad[]="Fake|https://calendar.google.com.evil.test/calendar/ical/example.ics\n";assert(!parse_feeds(bad,sizeof(bad)-1,f)&&f.count==0);
 const char other[]="Link|http://calendar.google.com/calendar/ical/example.ics\n";assert(!parse_feeds(other,sizeof(other)-1,f));
 const char outlook[]="Work|https://outlook.office365.com/owa/calendar/example/calendar.ics\n";assert(parse_feeds(outlook,sizeof(outlook)-1,f)&&f.count==1);
 const char spoof[]="Work|https://outlook.office365.com.evil/owa/calendar/example.ics\n";assert(!parse_feeds(spoof,sizeof(spoof)-1,f));
}
