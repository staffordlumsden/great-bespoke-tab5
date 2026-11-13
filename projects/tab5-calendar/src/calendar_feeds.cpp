#include "calendar_feeds.h"
#include <cstring>
namespace calendar {
bool parse_feeds(const char *data,size_t len,FeedList &out){
    out.count=0;if(!data)return false;size_t pos=0;
    while(pos<len){size_t end=pos;while(end<len&&data[end]!='\n'&&data[end]!='\r')++end;
        if(end>pos && data[pos]!='#'){
            size_t split=pos;while(split<end&&data[split]!='|')++split;
            size_t name=split-pos,url=split<end?end-split-1:0;
            const char *google="https://calendar.google.com/calendar/ical/";
            const char *outlook="https://outlook.office365.com/owa/calendar/";
            size_t glen=std::strlen(google),olen=std::strlen(outlook);
            bool trusted=(url>glen&&!std::memcmp(data+split+1,google,glen))||(url>olen&&!std::memcmp(data+split+1,outlook,olen));
            if(!name||name>=sizeof(out.feeds[0].name)||url>=sizeof(out.feeds[0].url)||out.count==max_feeds||!trusted){out.count=0;return false;}
            for(size_t i=pos;i<end;++i){unsigned char c=data[i];if(c<32||c>126||(i>split&&(c==' '||c=='#'||c=='\\'))){out.count=0;return false;}}
            Feed &f=out.feeds[out.count++];std::memcpy(f.name,data+pos,name);f.name[name]=0;std::memcpy(f.url,data+split+1,url);f.url[url]=0;
        }
        pos=end;if(pos<len&&data[pos]=='\r')++pos;if(pos<len&&data[pos]=='\n')++pos;
    }
    return true;
}
}
