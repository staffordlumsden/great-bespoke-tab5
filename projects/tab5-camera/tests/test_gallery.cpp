#include "gallery_model.h"
#include "image_effects.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

static void create(const char *root, const char *name) {
    char path[300]; std::snprintf(path, sizeof(path), "%s/DCIM/100TAB5/%s", root, name);
    FILE *f = std::fopen(path,"wb"); assert(f); assert(std::fwrite("TEST",1,4,f)==4); assert(std::fclose(f)==0);
}
int main() {
    char root[] = "/tmp/tab5-gallery-test-XXXXXX"; assert(mkdtemp(root));
    char dcim[300], dir[300];
    std::snprintf(dcim,sizeof(dcim),"%s/DCIM",root); assert(mkdir(dcim,0700)==0);
    std::snprintf(dir,sizeof(dir),"%s/DCIM/100TAB5",root); assert(mkdir(dir,0700)==0);
    const char *a="IMG_20261103_200825_0001.jpg", *b="IMG_20261103_200841_0001.jpg", *c="IMG_20261103_200854_0001.jpg";
    char out[100]{}; unsigned total=0;
    assert(gallery_select(root,"",GalleryDirection::latest,out,sizeof(out),&total)==GalleryResult::empty);
    create(root,b); create(root,c); create(root,a); create(root,"foreign.jpg"); create(root,".IMG_20261103_200854_0001.jpg.part");
    assert(gallery_select(root,"",GalleryDirection::latest,out,sizeof(out),&total)==GalleryResult::ok && !std::strcmp(out,c) && total==3);
    assert(gallery_select(root,c,GalleryDirection::older,out,sizeof(out),&total)==GalleryResult::ok && !std::strcmp(out,b));
    assert(gallery_select(root,b,GalleryDirection::older,out,sizeof(out),&total)==GalleryResult::ok && !std::strcmp(out,a));
    assert(gallery_select(root,a,GalleryDirection::older,out,sizeof(out),&total)==GalleryResult::boundary);
    assert(gallery_select(root,a,GalleryDirection::newer,out,sizeof(out),&total)==GalleryResult::ok && !std::strcmp(out,b));
    assert(gallery_delete(root,"../foreign.jpg")==GalleryResult::invalid);
    assert(gallery_delete(root,b)==GalleryResult::ok);
    assert(gallery_select(root,c,GalleryDirection::older,out,sizeof(out),&total)==GalleryResult::ok && !std::strcmp(out,a) && total==2);
    assert(gallery_valid_name("IMG_UNDATED_0001.jpg"));
    assert(!gallery_valid_name("IMG_20261103_200825_0001.jpg.part"));
    assert(tab5_mono_rgb565(0x0000)==0 && tab5_mono_rgb565(0xffff)==0xffff);
    const uint16_t values[]={0xf800,0x07e0,0x001f,0x7bef,0xffff,0};
    for (uint16_t pixel: values) {
        uint16_t mono=tab5_mono_rgb565(pixel);
        unsigned r=(mono>>11)&31, g=(mono>>5)&63, b=mono&31;
        assert(r==b && (g/2==r || g/2==r+1));
    }
    char command[340]; std::snprintf(command,sizeof(command),"rm -rf %s",root); assert(std::system(command)==0);
    std::puts("gallery ordering, boundaries, deletion safety and monochrome conversion PASS");
}
