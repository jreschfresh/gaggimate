import sys,re
from PIL import Image
src,repo=sys.argv[1],sys.argv[2]
E=repo+"/src/display/ui/default/eez/"
t=Image.open(src).convert("RGB");W0,H0=t.size;c=0.10
im=t.crop((int(W0*c),int(H0*c),int(W0*(1-c)),int(H0*(1-c)))).resize((480,480),Image.LANCZOS);px=im.load()
im.save(sys.argv[3])
def blk(sw):
    rows=[]
    for y in range(480):
        r=[]
        for x in range(480):
            R,G,B=px[x,y];v=((R&0xF8)<<8)|((G&0xFC)<<3)|((B&0xF8)>>3);lo,hi=v&255,v>>8
            r.append("0x%02x,0x%02x,0xff,"%((hi,lo) if sw else (lo,hi)))
        rows.append("    "+"".join(r))
    return "\n".join(rows)
head=open(E+"images/ui_image_logo.c").read().split("#ifndef LV_ATTRIBUTE_IMG_IMG_LOGO")[0]
out=head+"""#ifndef LV_ATTRIBUTE_IMG_IMG_STANDBY
#define LV_ATTRIBUTE_IMG_IMG_STANDBY
#endif

const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_IMG_IMG_STANDBY uint8_t img_standby_map[] = {
#if LV_COLOR_DEPTH == 16 && LV_COLOR_16_SWAP == 0
"""+blk(False)+"\n#endif\n#if LV_COLOR_DEPTH == 16 && LV_COLOR_16_SWAP != 0\n"+blk(True)+"""
#endif
};

const lv_img_dsc_t img_standby = {
    .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,
    .header.always_zero = 0,
    .header.reserved = 0,
    .header.w = 480,
    .header.h = 480,
    .data_size = 230400 * LV_IMG_PX_SIZE_ALPHA_BYTE,
    .data = img_standby_map,
};
"""
open(E+"images/ui_image_standby.c","w").write(out)
h=open(E+"images.h").read()
n=int(re.search(r"images\[(\d+)\]",h).group(1))
h=h.replace("\n#ifndef EXT_IMG_DESC_T","extern const lv_img_dsc_t img_standby;\n\n#ifndef EXT_IMG_DESC_T",1).replace("images[%d]"%n,"images[%d]"%(n+1))
open(E+"images.h","w").write(h)
cc=open(E+"images.c").read().replace("images[%d]"%n,"images[%d]"%(n+1))
i=cc.rindex("};");cc=cc[:i]+'    {"standby", &img_standby},\n'+cc[i:]
open(E+"images.c","w").write(cc)
s=open(E+"screens.c").read()
a=s.index("void create_screen_standby_screen()");b=s.index("void delete_screen_standby_screen()")
f=s[a:b]
old=re.search(r"lv_img_set_src\(obj, &img_logo\);\n\s*lv_img_set_zoom\(obj, \d+\);",f).group(0)
f=f.replace(old,"lv_img_set_src(obj, &img_standby);\n            lv_img_set_zoom(obj, 256);")
i=f.index("&img_standby");j=f.index("lv_obj_set_style_img_recolor_opa(obj, 255",i)
f=f[:j]+"lv_obj_set_style_img_recolor_opa(obj, 0"+f[j+len("lv_obj_set_style_img_recolor_opa(obj, 255"):]
k=f.count("lv_obj_set_style_border_width(obj, 10,")
f=f.replace("lv_obj_set_style_border_width(obj, 10,","lv_obj_set_style_border_width(obj, 0,")
open(E+"screens.c","w").write(s[:a]+f+s[b:])
print("images",n,"->",n+1,"| icon borders zeroed:",k)
