#include "ollama_analytics.h"
#include "ui.h"
#include "cJSON.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
namespace {
std::string read(const std::string &path) {
    std::ifstream f(path);assert(f.good());
    return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};
}
template<typename Change>std::string change(const std::string&raw,Change edit) {
    auto *j=cJSON_Parse(raw.c_str());assert(j);edit(j);
    char *out=cJSON_PrintUnformatted(j);assert(out);std::string result(out);
    cJSON_free(out);cJSON_Delete(j);return result;
}
cJSON *total(cJSON*j){return cJSON_GetObjectItemCaseSensitive(j,"totals");}
cJSON *groups(cJSON*j){return cJSON_GetObjectItemCaseSensitive(total(j),"groups");}
struct Canvas {
    std::vector<unsigned char> pixels=std::vector<unsigned char>(1280*720*3);
    unsigned calls=0;
};
void rect(void *context,int x,int y,int w,int h,uint32_t rgb) {
    auto &c=*static_cast<Canvas*>(context);++c.calls;
    assert(x>=0&&y>=0&&w>0&&h>0&&x+w<=1280&&y+h<=720);
    for(int row=y;row<y+h;++row)for(int col=x;col<x+w;++col) {
        auto *p=&c.pixels[(row*1280+col)*3];p[0]=rgb>>16;p[1]=rgb>>8;p[2]=rgb;
    }
}
void ppm(const std::string &path,const Canvas&c) {
    std::ofstream f(path,std::ios::binary);f<<"P6\n1280 720\n255\n";
    f.write(reinterpret_cast<const char*>(c.pixels.data()),c.pixels.size());assert(f.good());
}
}
int main(int argc,char**argv) {
    if(argc==4&&!std::strcmp(argv[1],"--live")) {
        const auto balance=read(argv[2]),models=read(argv[3]);
        oa::Models parsed;ou::Balance plan;ou::Error error;
        if(!ou::parse_balance(balance.data(),balance.size(),plan,error)||
           !oa::parse_models(models.data(),models.size(),parsed))return 1;
        std::puts("PASS live direct Ollama payloads accepted by firmware parsers; account values withheld");
        return 0;
    }
    assert(argc==3);
    const std::string raw=read(std::string(argv[1])+"/models.json");
    const std::string balance=read(std::string(argv[1])+"/balance.json");
    oa::Models models;
    assert(oa::parse_models(raw.data(),raw.size(),models));
    assert(models.count==5&&models.requests==1000&&models.other_requests==80&&models.global_top_five);
    assert(!std::strcmp(models.top[0].name,"fixture-model-a")&&models.top[0].requests==300);
    assert(models.top[4].requests==80);
    assert(oa::models_fresh(models,models.until+180));
    assert(!oa::models_fresh(models,models.until+181));
    assert(!oa::models_fresh(models,models.until-121));
    const auto truncated=change(raw,[](cJSON*j){
        cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(total(j),"request_count"),1170);
        auto *r=cJSON_GetObjectItemCaseSensitive(total(j),"remainder");
        cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(r,"request_count"),200);
    });
    oa::Models restricted;
    assert(oa::parse_models(truncated.data(),truncated.size(),restricted)&&!restricted.global_top_five);
    std::vector<std::string> bad;
    bad.push_back(change(raw,[](cJSON*j){cJSON_DeleteItemFromObject(total(j),"groups");}));
    bad.push_back(change(raw,[](cJSON*j){cJSON_AddStringToObject(j,"scope","self");}));
    bad.push_back(change(raw,[](cJSON*j){cJSON_AddItemToArray(groups(j),cJSON_Duplicate(groups(j)->child,1));}));
    bad.push_back(change(raw,[](cJSON*j){cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(groups(j)->child,"request_count"),0.5);}));
    bad.push_back(change(raw,[](cJSON*j){cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(groups(j)->child,"request_count"),9007199254740992.0);}));
    bad.push_back(change(raw,[](cJSON*j){cJSON_SetValuestring(cJSON_GetObjectItemCaseSensitive(groups(j)->child,"model"),"line\nbreak");}));
    bad.push_back(raw+"{}");
    bad.push_back(std::string(ou::response_limit+1,' '));
    for(const auto &s:bad) {
        oa::Models previous=models;
        assert(!oa::parse_models(s.data(),s.size(),previous));
        assert(previous.requests==models.requests&&previous.count==models.count);
    }
    oa::Config config;
    const std::string good=R"({"schema":1,"ollama_api_key":"public-test-fixture-key"})";
    assert(oa::parse_config(good.data(),good.size(),config)&&config.ready);
    for(const std::string s:{R"({"schema":1,"schema":1})",R"({"schema":1,"ollama_api_key":"REPLACE_WITH_YOUR_KEY"})",R"({"schema":1,"ollama_api_key":"key\r\nheader-attack"})",R"({"schema":1,"ollama_api_key":"public-test-fixture-key"}[])"}) {
        assert(!oa::parse_config(s.data(),s.size(),config)&&!config.ready&&!config.ollama_key[0]);
    }
    const std::string blank=R"({"schema":1,"ollama_api_key":""})";
    assert(oa::parse_config(blank.data(),blank.size(),config)&&!config.ready);
    ou::PollPolicy policy;
    policy.begin(1000);policy.finish(2000,false,429,120);
    assert(!policy.manual(3000)&&!policy.ready(121999)&&policy.ready(122000));
    policy.finish(122000,false,401);assert(policy.blocked);
    oa::View view;view.models=models;view.has_models=true;view.configured=true;
    ou::Error error;
    assert(ou::parse_balance(balance.data(),balance.size(),view.balance,error));view.has_balance=true;
    view.now=models.until;
    for(bool dark:{true,false}) {
        view.dark=dark;Canvas canvas;
        oa::draw(view,{&canvas,rect},oa::full);assert(canvas.calls>1000);
        ppm(std::string(argv[2])+(dark?"/dark.ppm":"/light.ppm"),canvas);
        const auto previous=canvas.pixels;
        view.wifi=true;view.clock_ready=true;
        oa::draw(view,{&canvas,rect},oa::footer);
        for(unsigned i=0;i<1280*646*3;++i)assert(canvas.pixels[i]==previous[i]);
    }
    assert(oa::hit(1100,50)==oa::Action::console);
    assert(oa::hit(900,50)==oa::Action::theme);
    assert(oa::hit(100,20)==oa::Action::none);
    assert(oa::hit(600,20)==oa::Action::none);
    assert(oa::hit(1100,680)==oa::Action::refresh);
    assert(oa::hit(-1,50)==oa::Action::none&&oa::hit(1280,50)==oa::Action::none);
    std::puts("PASS request-ranked top five, hidden-group proof, stale boundaries, strict config, cooldown and native layout/dirty footer");
}
