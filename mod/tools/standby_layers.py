# Splits the standby picture into a background plate and two hand sprites (480-pixel display space).
import cv2, numpy as np, sys, os
src=os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','gaggimate_standby.png')
out=sys.argv[1]
im=cv2.imread(src,cv2.IMREAD_UNCHANGED); bgr=np.ascontiguousarray(im[...,:3])
m=cv2.imread(os.path.join(os.path.dirname(os.path.abspath(__file__)),'standby_mask.png'),0)
# whole espresso glass belongs to the right hand
cv2.fillPoly(m,[np.array([(568,541),(672,538),(667,673),(585,673)],np.int32)],255)
m=cv2.morphologyEx(m,cv2.MORPH_CLOSE,np.ones((5,5),np.uint8))

# background plate: hands painted over with the surrounding plaster
hole=cv2.dilate(m,cv2.getStructuringElement(cv2.MORPH_ELLIPSE,(13,13)))
plate=cv2.inpaint(bgr,hole,7,cv2.INPAINT_TELEA)
# a little plaster grain back into the filled area (it would look smeared otherwise)
rng=np.random.default_rng(3)
noise=cv2.GaussianBlur(rng.normal(0,1,bgr.shape[:2]).astype(np.float32),(0,0),1.2)*9
plate=np.clip(plate.astype(np.float32)+(hole[...,None]>0)*noise[...,None],0,255).astype(np.uint8)
# display transform (same as patch.py: crop 10 % on each side, scale to 480)
c0=int(1024*0.10); c1=int(1024*0.90); s=480.0/(c1-c0)
def warp(img,x0,y0,w,h,interp=cv2.INTER_AREA):
    M=np.array([[s,0,-c0*s-x0],[0,s,-c0*s-y0]],np.float32)
    return cv2.warpAffine(img,M,(w,h),flags=interp,borderMode=cv2.BORDER_REPLICATE)
bg=warp(plate,0,0,480,480)
cv2.imwrite(out+'/standby_bg.png',bg)
alpha=cv2.GaussianBlur(m,(0,0),1.3)
n,lab,st,_=cv2.connectedComponentsWithStats(m)
info={}
for i in range(1,n):
    if st[i,4]<3000: continue
    side='left' if st[i,0]<400 else 'right'
    a=np.where(lab==i,alpha,0).astype(np.uint8)
    a=np.maximum(a,cv2.GaussianBlur(((lab==i)*255).astype(np.uint8),(0,0),1.3))
    x,y,w,h=st[i,:4]
    X0=int(np.floor((x-c0)*s))-2; X1=int(np.ceil((x+w-c0)*s))+2
    Y0=int(np.floor((y-c0)*s))-2; Y1=int(np.ceil((y+h-c0)*s))+2
    if side=='left': X0=max(X0,-26)
    else: X1=min(X1,502)
    W,H=X1-X0,Y1-Y0
    rgb=warp(bgr,X0,Y0,W,H); al=warp(a,X0,Y0,W,H)
    cv2.imwrite(out+'/standby_%s.png'%side,np.dstack([rgb,al]))
    info[side]=(X0,Y0,W,H)
    print(side,X0,Y0,W,H)
open(out+'/standby_layers.txt','w').write('\n'.join('%s %d %d %d %d'%((k,)+v) for k,v in sorted(info.items()))+'\n')
