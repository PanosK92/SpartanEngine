"""Inspect Exo Chora's terrain and existing road geometry before authoring."""
from pathlib import Path
import json, struct, xml.etree.ElementTree as ET
import numpy as np
from PIL import Image, ImageDraw
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'binaries/project/exo_chora'
OUT.mkdir(exist_ok=True)
root=ET.parse(ROOT/'worlds/plan.world').getroot()
with (ROOT/'binaries/project/plan_resources/terrain_cache.bin').open('rb') as f:
    hdr=struct.unpack('<Q6If',f.read(36)); f.seek(36+hdr[3]*4)
    xyz=np.fromfile(f,dtype='<f4',count=hdr[4]*3).reshape(hdr[6],hdr[5],3)
height=xyz[:,:,1].copy()
with (ROOT/'binaries/project/plan_resources/terrain_sculpt.bin').open('rb') as f:
    magic,version,ox,oz,cx,cz,cells,count=struct.unpack('<II4fII',f.read(32))
    for _ in range(count):
        tx,tz=struct.unpack('<ii',f.read(8)); tile=np.frombuffer(f.read(cells*cells*4),dtype='<f4').reshape(cells,cells)
        x,z=tx*cells,tz*cells
        if x>=0 and z>=0 and x<height.shape[1] and z<height.shape[0]:
            hh,ww=min(cells,height.shape[0]-z),min(cells,height.shape[1]-x)
            height[z:z+hh,x:x+ww]+=tile[:hh,:ww]
def ground(x,z):
    px=np.clip((x-float(xyz[0,0,0]))/25,0,height.shape[1]-1.001)
    pz=np.clip((z-float(xyz[0,0,2]))/25,0,height.shape[0]-1.001)
    ix,iz=int(px),int(pz);u,v=px-ix,pz-iz
    return float((height[iz,ix]*(1-u)+height[iz,ix+1]*u)*(1-v)+(height[iz+1,ix]*(1-u)+height[iz+1,ix+1]*u)*v)-5.9
if __name__=='__main__':
    center=(-10450,1050);span=650;size=1100
    def pix(x,z):return ((x-center[0]+span/2)*size/span,(center[1]+span/2-z)*size/span)
    arr=np.array([[ground(center[0]-span/2+i*span/size,center[1]+span/2-j*span/size) for i in range(0,size,5)] for j in range(0,size,5)])
    im=Image.fromarray(np.uint8((arr-arr.min())/(arr.max()-arr.min())*100+100)).resize((size,size)).convert('RGB');d=ImageDraw.Draw(im)
    nearby=[]
    for e in root.iter('Entity'):
        s=e.find('spline')
        if s is None:continue
        p=np.array(list(map(float,e.get('position').split())))
        points=[p+np.array(list(map(float,c.get('position').split()))) for c in e.findall('Entity') if c.get('name','').startswith('spline_point_')]
        if not points or min(np.linalg.norm(q[[0,2]]-center) for q in points)>500:continue
        near=[list(map(float,q)) for q in points if np.linalg.norm(q[[0,2]]-center)<500]
        nearby.append(dict(name=e.get('name'),id=e.get('id'),width=float(s.get('road_width')),points=near))
        d.line([pix(q[0],q[2]) for q in points],fill=(50,65,75),width=max(2,int(float(s.get('road_width'))*size/span)))
        for q in near:
            xy=pix(q[0],q[2]);d.ellipse((xy[0]-2,xy[1]-2,xy[0]+2,xy[1]+2),fill='white')
        if near:d.text(pix(near[len(near)//2][0],near[len(near)//2][2]),e.get('name'),fill='white')
    for z in range(800,1401,50):
        for x in range(-10750,-10149,50):d.text(pix(x,z),f'{ground(x,z):.1f}',fill=(60,30,20))
    for name,col in [('pin_exo_chora','yellow'),('landmark_exo_chora','red')]:
        e=next(e for e in root.iter('Entity') if e.get('name')==name);p=list(map(float,e.get('position').split()));xy=pix(p[0],p[2]);d.ellipse((xy[0]-7,xy[1]-7,xy[0]+7,xy[1]+7),fill=col);d.text(xy,name,fill=col)
    im.save(OUT/'site-survey.png')
    (OUT/'site-roads.json').write_text(json.dumps(nearby,indent=2))
    print(json.dumps({'height_range':[arr.min(),arr.max()],'roads':[{k:v for k,v in r.items() if k!='points'} for r in nearby]},indent=2))
