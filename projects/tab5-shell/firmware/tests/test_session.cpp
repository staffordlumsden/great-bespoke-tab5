#include "session.h"
#include "download_policy.h"
#include "touch_keyboard.h"
#include <cassert>
#include <iostream>
using namespace tab5_shell;
int main() {
    assert(https_url_allowed("https://example.invalid/path?q=value"));
    for (auto url:{"http://example.invalid", "https://user:pass@example.invalid", "https://example.invalid:80/", "https://example.invalid/#fragment", "https://example.invalid/\n"}) assert(!https_url_allowed(url));
    TouchKeyboard keyboard;
    assert(keyboard.tap(150,500).empty()); keyboard.visible=true;
    assert(keyboard.tap(140,480)=="w");
    keyboard.tap(190,650); assert(keyboard.control);
    assert(keyboard.tap(260,590)=="\x03" && !keyboard.control);
    keyboard.tap(10,650); assert(keyboard.shift);
    assert(keyboard.tap(140,480)=="W" && !keyboard.shift);
    assert(keyboard.tap(1279,683)=="\r");
    assert(keyboard.tap(-1,480).empty() && keyboard.tap(1280,480).empty());
    SshRequest r; std::string e;
    assert(parse_ssh({"-p","2222","alice@Example.invalid"},r,e));
    assert(r.host=="example.invalid" && r.user=="alice" && r.port==2222);
    for (const auto& command : {"ssh", "ssh alice", "ssh alice@host:22", "ssh -p 0 a@host", "ssh -p 65536 a@host", "ssh -p 22x a@host", "ssh -o StrictHostKeyChecking=no a@host", "ssh -i /private/../x a@host", "ssh --profile x a@host", "ssh -p 22 -p 23 a@host"}) {
        auto p=parse_command(command); std::vector<std::string> args(p.arguments.begin()+1,p.arguments.end());
        assert(!parse_ssh(args,r,e));
    }
    const auto profiles="home alice@one.invalid -p 2222\nwork bob@two.invalid -i /private/work-key\n";
    assert(select_profile(profiles,"work",r,e) && r.user=="bob" && r.key=="/private/work-key");
    assert(select_profile(profiles,"home",r,e) && r.port==2222);
    assert(!select_profile(profiles,"missing",r,e));
    assert(!select_profile("x a@h\nx b@h\n","x",r,e));
    assert(parse_ssh({"a@host"},r,e));
    std::array<std::uint8_t,32> fp{};
    std::string pin="host 22 "+std::string(64,'0')+"\n";
    assert(known_host_matches(pin,r,fp));
    assert(!known_host_matches("",r,fp));
    fp[0]=1; assert(!known_host_matches(pin,r,fp)); fp[0]=0;
    assert(!known_host_matches(pin+pin,r,fp)); r.port=23; assert(!known_host_matches(pin,r,fp));
    SessionFlow flow;
    assert(flow.state()==SessionState::local);
    assert(flow.begin() && !flow.begin()); flow.finish_connect(false);
    assert(flow.state()==SessionState::local);
    assert(flow.begin()); flow.finish_connect(true);
    assert(flow.state()==SessionState::remote);
    flow.stop(); assert(!flow.begin()); flow.transport_idle();
    assert(flow.state()==SessionState::local && flow.begin());
    std::cout << "PASS SSH parser/profiles/strict pins/session transitions ASan+UBSan\n";
}
