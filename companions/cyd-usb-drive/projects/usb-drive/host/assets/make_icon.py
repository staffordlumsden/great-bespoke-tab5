#!/usr/bin/env python3
from pathlib import Path
from PIL import Image, ImageDraw
import subprocess

HERE = Path(__file__).resolve().parent
ICONSET = HERE / "USBDrive.iconset"
ICONSET.mkdir(exist_ok=True)

def render(size: int) -> Image.Image:
    im = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    u = size / 64
    def box(x0,y0,x1,y1,fill,outline=None,width=1,r=0):
        d.rounded_rectangle(tuple(int(v*u) for v in (x0,y0,x1,y1)), radius=int(r*u), fill=fill,
                            outline=outline, width=max(1,int(width*u)))
    box(4,4,60,60,"#081115","#42a5f5",2,10)
    box(11,10,53,42,"#172b32","#29414a",1,4)
    # Pixel CYD screen and SD card, kept deliberately blocky at every size.
    box(16,15,43,35,"#101d24","#66cf8e",2,1)
    d.line([(int(21*u),int(28*u)),(int(26*u),int(23*u)),(int(21*u),int(18*u))],fill="#ffae42",width=max(1,int(2*u)))
    d.line([(int(30*u),int(29*u)),(int(38*u),int(29*u))],fill="#e4eef0",width=max(1,int(2*u)))
    box(38,29,53,51,"#ff8a3d",None,1,2)
    d.polygon([(int(47*u),int(29*u)),(int(53*u),int(35*u)),(int(53*u),int(29*u))],fill="#081115")
    for x in (41,44,47): box(x,32,x+1,36,"#081115")
    box(13,46,39,51,"#42a5f5",None,1,2)
    box(18,53,47,56,"#29414a",None,1,1)
    return im

specs = [(16,"icon_16x16.png"),(32,"icon_16x16@2x.png"),(32,"icon_32x32.png"),
         (64,"icon_32x32@2x.png"),(128,"icon_128x128.png"),(256,"icon_128x128@2x.png"),
         (256,"icon_256x256.png"),(512,"icon_256x256@2x.png"),(512,"icon_512x512.png"),
         (1024,"icon_512x512@2x.png")]
for size,name in specs: render(size).save(ICONSET/name)
render(512).save(HERE/"USBDrive.png")
subprocess.run(["iconutil","-c","icns",str(ICONSET),"-o",str(HERE/"USBDrive.icns")],check=True)
