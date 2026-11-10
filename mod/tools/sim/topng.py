import sys,glob,os
import numpy as np
from PIL import Image
for f in glob.glob(sys.argv[1]+'/*.rgb565'):
    a=np.fromfile(f,dtype='<u2').reshape(480,480)
    r=((a>>11)&31); g=((a>>5)&63); b=a&31
    rgb=np.dstack(((r<<3)|(r>>2),(g<<2)|(g>>4),(b<<3)|(b>>2))).astype(np.uint8)
    im=Image.fromarray(rgb)
    if len(sys.argv)>2:  # round display mask
        m=Image.new('L',(480*4,480*4),0)
        from PIL import ImageDraw
        ImageDraw.Draw(m).ellipse([0,0,480*4-1,480*4-1],fill=255); m=m.resize((480,480),Image.LANCZOS)
        im=Image.composite(im,Image.new('RGB',(480,480),(20,20,20)),m)
    im.save(f[:-7]+'.png')
