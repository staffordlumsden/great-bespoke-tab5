#include "../src/calendar_events.h"
#include <cassert>
#include <cstdlib>
#include <ctime>
#include <string>
using namespace calendar;
int main(){
    setenv("TZ","AEST-10AEDT,M10.1.0,M4.1.0/3",1);tzset();
    const char ics[]="BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nDTSTART;VALUE=DATE:20261113\r\nDTEND;VALUE=DATE:20261115\r\nSUMMARY:Holiday\r\nEND:VEVENT\r\nBEGIN:VEVENT\r\nDTSTART;TZID=Australia/Sydney:20261102T090000\r\nRRULE:FREQ=WEEKLY;BYDAY=MO,WE;COUNT=4\r\nEXDATE;TZID=Australia/Sydney:20261104T090000\r\nSUMMARY:Meeting\r\nEND:VEVENT\r\nBEGIN:VEVENT\r\nDTSTART:20261113T000000Z\r\nSUMMARY:UTC event\r\nEND:VEVENT\r\nBEGIN:VEVENT\r\nDTSTART;TZID=Europe/London:20261113T090000\r\nSUMMARY:Unsupported zone\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
    auto p=parse_ics(ics,sizeof(ics)-1,0);assert(!p.fatal&&p.events.size()==3&&p.warnings==1);
    assert(occurs(p.events[0],{2026,11,13})&&occurs(p.events[0],{2026,11,14})&&!occurs(p.events[0],{2026,11,15}));
    assert(occurs(p.events[1],{2026,11,2})&&!occurs(p.events[1],{2026,11,4})&&occurs(p.events[1],{2026,11,11})&&!occurs(p.events[1],{2026,11,16}));
    assert(p.events[2].minute==11*60);
    const char bad[]="BEGIN:VCALENDAR\nBEGIN:VEVENT\n";auto broken=parse_ics(bad,sizeof(bad)-1,0);assert(broken.fatal);
    const char recurring[]=
      "BEGIN:VCALENDAR\n\nBEGIN:VEVENT\nUID:weekly\nDTSTART;TZID=Australia/Sydney:20261101T090000\nDTEND;TZID=Australia/Sydney:20261101T100000\nRRULE:FREQ=WEEKLY;INTERVAL=2;BYDAY=SU,MO;WKST=SU\nSUMMARY:Weekly\nEND:VEVENT\n"
      "BEGIN:VEVENT\nUID:weekly\nRECURRENCE-ID;TZID=Australia/Sydney:20261102T090000\nDTSTART;TZID=Australia/Sydney:20261103T110000\nDTEND;TZID=Australia/Sydney:20261103T120000\nSUMMARY:Moved\nEND:VEVENT\n"
      "BEGIN:VEVENT\nUID:monthly\nDTSTART;TZID=Australia/Sydney:20261117T090000\nRRULE:FREQ=MONTHLY;BYDAY=3TU\nSUMMARY:Monthly\nEND:VEVENT\n"
      "BEGIN:VEVENT\nUID:first-workday\nDTSTART;TZID=Australia/Sydney:20261102T090000\nRRULE:FREQ=MONTHLY;BYDAY=MO,TU,WE,TH,FR;BYSETPOS=1\nSUMMARY:Workday\nEND:VEVENT\n"
      "BEGIN:VEVENT\nUID:extra\nDTSTART;TZID=America/Bogota:20261102T090000\nDTEND;TZID=America/Bogota:20261102T100000\nRDATE;TZID=America/Bogota:20261109T090000\nSUMMARY:Extra\nEND:VEVENT\nEND:VCALENDAR\n";
    auto q=parse_ics(recurring,sizeof(recurring)-1,0);assert(!q.fatal&&q.warnings==0&&q.events.size()==6);
    assert(occurs(q.events[0],{2026,11,1})&&!occurs(q.events[0],{2026,11,2})&&occurs(q.events[0],{2026,11,15}));
    assert(occurs(q.events[1],{2026,12,15})&&!occurs(q.events[1],{2026,12,8}));
    assert(occurs(q.events[2],{2026,12,1})&&!occurs(q.events[2],{2026,12,2}));
    assert(occurs(q.events[4],{2026,11,10})&&occurs(q.events[5],{2026,11,3}));
    std::string longline="BEGIN:VCALENDAR\n\nBEGIN:VEVENT\nDTSTART;VALUE=DATE:20261101\nDESCRIPTION:"+std::string(9000,'a')+"\nEND:VEVENT\nEND:VCALENDAR\n";
    auto l=parse_ics(longline.data(),longline.size(),0);assert(!l.fatal&&l.warnings==0&&l.events.size()==1);
    const char overnight[]="BEGIN:VCALENDAR\nBEGIN:VEVENT\nDTSTART;TZID=Australia/Sydney:20261102T230000\nDTEND;TZID=Australia/Sydney:20261103T010000\nRRULE:FREQ=WEEKLY;BYDAY=MO;WKST=SU\nEND:VEVENT\nEND:VCALENDAR\n";
    auto night=parse_ics(overnight,sizeof(overnight)-1,0);assert(!night.fatal&&night.warnings==0&&night.events.size()==1);
    assert(starts_on(night.events[0],{2026,11,9})&&occurs(night.events[0],{2026,11,10})&&!starts_on(night.events[0],{2026,11,10}));
    const char seoul[]="BEGIN:VCALENDAR\nBEGIN:VEVENT\nDTSTART;TZID=Asia/Seoul:20260706T100000\nDTEND;TZID=Asia/Seoul:20260706T110000\nRRULE:FREQ=WEEKLY;BYDAY=MO;WKST=MO\nEND:VEVENT\nEND:VCALENDAR\n";
    auto fixed=parse_ics(seoul,sizeof(seoul)-1,0);assert(!fixed.fatal&&!fixed.warnings&&fixed.events.size()==1);
    assert(starts_on(fixed.events[0],{2026,7,6})&&occurrence_minute(fixed.events[0],{2026,7,6})==11*60);
    assert(starts_on(fixed.events[0],{2026,11,2})&&occurrence_minute(fixed.events[0],{2026,11,2})==12*60);
}
