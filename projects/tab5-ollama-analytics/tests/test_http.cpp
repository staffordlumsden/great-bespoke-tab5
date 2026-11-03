#include "ollama_http.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
struct Response {
    std::string body,type="application/json",retry;
    int status=200;
    int64_t length=-2;
    bool complete=true,read_error=false;
};
struct Client {
    Response response;
    esp_http_client_config_t cfg;
    size_t offset=0;
    bool authorized=false,closed=false;
};
std::vector<Response> pending;
size_t started=0,cleaned=0;
std::string balance,models;
int esp_crt_bundle_attach(void*){return 0;}
int64_t esp_timer_get_time(){static int64_t now=0;return now+=1000;}
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg) {
    assert(started<pending.size()&&cfg->disable_auto_redirect&&cfg->crt_bundle_attach==esp_crt_bundle_attach);
    assert(!cfg->cert_pem&&cfg->timeout_ms==8000);
    assert(!std::strcmp(cfg->url,started==0?"https://ollama.com/api/balance":"https://ollama.com/api/usage?range=24h&scope=self&group_by=model"));
    auto *c=new Client;c->response=pending[started++];c->cfg=*cfg;return c;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t h,const char *name,const char *value) {
    auto *c=static_cast<Client*>(h);
    if(!std::strcmp(name,"Authorization")) {
        assert(!std::strcmp(value,"Bearer public-test-fixture-key"));c->authorized=true;
    } else {assert(!std::strcmp(name,"Accept")&&!std::strcmp(value,"application/json"));}
    return ESP_OK;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t h,int length) {
    assert(!length&&static_cast<Client*>(h)->authorized);return ESP_OK;
}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t h) {
    auto *c=static_cast<Client*>(h);
    for(auto pair:{std::pair<const char*,const std::string&>{"Content-Type",c->response.type},
                   std::pair<const char*,const std::string&>{"Retry-After",c->response.retry}}) {
        if(pair.second.empty())continue;
        esp_http_client_event_t e{HTTP_EVENT_ON_HEADER,c->cfg.user_data,
            const_cast<char*>(pair.first),const_cast<char*>(pair.second.c_str())};
        assert(c->cfg.event_handler(&e)==ESP_OK);
    }
    return c->response.length==-2?static_cast<int64_t>(c->response.body.size()):c->response.length;
}
int esp_http_client_get_status_code(esp_http_client_handle_t h){return static_cast<Client*>(h)->response.status;}
int esp_http_client_read(esp_http_client_handle_t h,char*out,int capacity) {
    auto *c=static_cast<Client*>(h);if(c->response.read_error)return -1;
    size_t n=std::min({c->response.body.size()-c->offset,size_t(capacity),size_t(137)});
    std::memcpy(out,c->response.body.data()+c->offset,n);c->offset+=n;return int(n);
}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t h) {
    auto*c=static_cast<Client*>(h);return c->response.complete&&c->offset==c->response.body.size();
}
void esp_http_client_close(esp_http_client_handle_t h){static_cast<Client*>(h)->closed=true;}
void esp_http_client_cleanup(esp_http_client_handle_t h) {
    auto*c=static_cast<Client*>(h);assert(c->closed);++cleaned;delete c;
}
oa::FetchResult run(std::vector<Response> responses,bool configured=true) {
    pending=std::move(responses);started=cleaned=0;
    oa::Config config;config.ready=configured;std::strcpy(config.ollama_key,"public-test-fixture-key");
    oa::FetchResult result;oa::fetch_ollama(config,result);assert(cleaned==started);return result;
}
std::string read(const std::string &path){std::ifstream f(path);assert(f.good());return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};}
int main(int argc,char**argv) {
    assert(argc==2);balance=read(std::string(argv[1])+"/balance.json");models=read(std::string(argv[1])+"/models.json");
    Response b,m;b.body=balance;m.body=models;
    auto result=run({b,m});assert(result.balance_meta.ok&&result.models_meta.ok&&started==2);
    result=run({},false);assert(started==0&&result.models_meta.error==oa::HttpError::config);
    Response rejected=b;rejected.status=401;result=run({rejected});assert(started==1&&!result.models_meta.ok&&result.models_meta.status==401);
    rejected=b;rejected.status=302;result=run({rejected,m});assert(!result.balance_meta.ok&&result.models_meta.ok&&started==2);
    rejected=m;rejected.type="text/html";result=run({b,rejected});assert(result.models_meta.error==oa::HttpError::content_type);
    rejected=m;rejected.length=ou::response_limit+1;result=run({b,rejected});assert(result.models_meta.error==oa::HttpError::size);
    rejected=m;rejected.body=std::string(ou::response_limit+1,' ');rejected.length=0;result=run({b,rejected});assert(result.models_meta.error==oa::HttpError::size);
    rejected=m;rejected.complete=false;result=run({b,rejected});assert(result.models_meta.error==oa::HttpError::incomplete);
    rejected=m;rejected.body="{}";result=run({b,rejected});assert(result.models_meta.error==oa::HttpError::json);
    rejected=m;rejected.status=429;rejected.retry="123";result=run({b,rejected});assert(result.models_meta.retry_after==123&&!result.models_meta.ok);
    rejected=m;rejected.read_error=true;result=run({b,rejected});assert(result.models_meta.error==oa::HttpError::transport);
    rejected=m;rejected.type="APPLICATION/JSON; charset=utf-8";result=run({b,rejected});assert(result.models_meta.ok);
    std::puts("PASS fixed HTTPS origins, CA verification, bearer headers, no redirects, independent partial success, bounded/chunked bodies, auth stop and Retry-After");
}
