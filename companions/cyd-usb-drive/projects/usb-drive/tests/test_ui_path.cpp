#include "drive_path.h"
#include "ui.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>

using namespace usbdrive;

static void path_is(const char *source, const char *expected, bool allow_root=false) {
    char output[300]{};
    assert(virtual_path(source, output, sizeof(output), allow_root));
    assert(std::string(output) == expected);
}

static void raster(View &view, const std::string &name) {
    std::ofstream output(name, std::ios::binary);
    output << "P6\n320 240\n255\n";
    for (int y=0;y<240;++y) for(int x=0;x<320;++x) {
        const auto colour=pixel(x,y,&view);
        const unsigned char rgb[]={static_cast<unsigned char>(((colour>>11)&31)*255/31),
                                   static_cast<unsigned char>(((colour>>5)&63)*255/63),
                                   static_cast<unsigned char>((colour&31)*255/31)};
        output.write(reinterpret_cast<const char *>(rgb),3);
    }
    assert(output.good());
}

int main(int argc,char **argv) {
    assert(argc==2);
    path_is("/Files/Notes/today.md","0:/files/Notes/today.md");
    path_is("/Firmware/App.bin","0:/firmware/App.bin");
    path_is("/Files","0:/files",true);
    path_is("/Firmware","0:/firmware",true);
    char output[300]{};
    for(const char *bad:{"/","/config.json","/Files/../config.json","/Files/.hidden",
                         "/Files/a//b","/Files/a/","/Files/a:b","/Firmware/._junk"})
        assert(!virtual_path(bad,output,sizeof(output)));
    assert(!virtual_path("/Files",output,sizeof(output)));
    assert(canonical_utf8(reinterpret_cast<const unsigned char *>("caf\xC3\xA9.md")));
    const unsigned char invalid[]={0xc0,0xaf,0};
    assert(!canonical_utf8(invalid));
    assert(root_path("/")&&root_path("/Files")&&root_path("/Firmware")&&!root_path("/files"));
    assert(std::string(display_name("/Files/Notes/today.md"))=="today.md");
    assert(hit(20,215)==Action::theme&&hit(120,215)==Action::stop&&hit(250,215)==Action::console);
    assert(hit(7,215)==Action::theme&&hit(313,215)==Action::console&&hit(100,100)==Action::none);
    assert(hit(30,199)==Action::theme&&hit(150,239)==Action::stop&&hit(319,239)==Action::console);
    View view;view.console=true;view.drive.ready=true;view.drive.card_total=32ull<<30;view.drive.card_free=12ull<<30;
    raster(view,std::string(argv[1])+"/dark-waiting.ppm");
    view.light=true;view.drive.linked=true;view.drive.busy=true;view.drive.writing=true;
    view.drive.done=1536;view.drive.total=4096;
    std::snprintf(view.drive.operation,sizeof(view.drive.operation),"Writing");
    std::snprintf(view.drive.name,sizeof(view.drive.name),"today.md");
    raster(view,std::string(argv[1])+"/light-writing.ppm");
    std::puts("PASS private-path isolation, canonical UTF-8, virtual roots, touch targets and both UI themes");
}
