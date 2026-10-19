// Copyright(c) 2015-2026 Panos Karabelas
// Licensed under the Spartan Engine License. See license.md in the repository root.
// https://github.com/PanosK92/SpartanEngine/blob/master/license.md
// Commercial use requires written permission and negotiated payment terms.
#ifndef COMMON_ROAD_H
#define COMMON_ROAD_H

float road_hash(float2 p)
{
    float3 q=frac(float3(p.xyx)*float3(.1031,.1030,.0973));
    q+=dot(q,q.yzx+33.33);
    return frac((q.x+q.y)*q.z);
}

float2 road_hash2(float2 p)
{
    float3 q=frac(float3(p.xyx)*float3(.1031,.1030,.0973));
    q+=dot(q,q.yzx+33.33);
    return frac((q.xx+q.yz)*q.zy);
}

// The lattice wraps every 289 cells, the hash streaks at island coordinates times high frequencies.
float road_noise(float2 p)
{
    float2 i=floor(p),f=frac(p);
    f=f*f*(3-2*f);
    i-=floor(i/289)*289;
    float2 j=i+1;
    j-=floor(j/289)*289;
    return lerp(lerp(road_hash(i),road_hash(float2(j.x,i.y)),f.x),
                lerp(road_hash(float2(i.x,j.y)),road_hash(j),f.x),f.y);
}

// Two rotated octaves, so the value noise grid doesn't show as axis aligned boxes.
float road_noise_soft(float2 p)
{
    float2 r=float2(p.x*.8-p.y*.6,p.x*.6+p.y*.8);
    return road_noise(p)*.5+road_noise(r*1.7+float2(31,7))*.5;
}

// Metres per asphalt texture repeat, must match layered_road_surface::asphalt_repeat.
static const float road_asphalt_repeat=1.5;

// Island asphalt u is lateral metres / repeat from the road centre (Spline.cpp finish_uv) and
// both island widths put lane centres at +-1.75 and +-5.25 m, so a 3.5 m period finds the
// lane without knowing the width.
float road_lane_offset(float2 uv)
{
    return (frac(uv.x*(road_asphalt_repeat/3.5))-.5)*3.5;
}

// Tyres track ~0.85 m either side of the lane centre, polishing the stone and laying rubber.
float road_wheel_paths(float2 uv,float2 xz)
{
    float o=(abs(road_lane_offset(uv))-.85)/.3;
    float broken=smoothstep(.25,.75,road_noise_soft(xz*.08+float2(41,7)))*lerp(.6,1,road_noise_soft(xz*.7));
    return exp(-.5*o*o)*broken;
}

// Round stains scattered over unit cells, density is the share of cells holding one and
// radius the size range in cells. Returns coverage times a per stain strength.
float road_blobs(float2 p,float density,float2 radius,float2 seed)
{
    float2 n=floor(p),f=frac(p);
    float best=0;
    for (int j=-1;j<=1;j++)
    {
        for (int i=-1;i<=1;i++)
        {
            float2 g=float2(i,j);
            float2 cell=n+g+seed;
            if (road_hash(cell+float2(5.3,1.7))>density) continue;
            float2 c=g+road_hash2(cell)*.8+.1-f;
            float r=lerp(radius.x,radius.y,road_hash(cell+float2(.7,9.1)));
            float strength=lerp(.35,1,road_hash(cell+float2(3.9,4.4)));
            best=max(best,(1-smoothstep(.45,1,length(c)/r))*strength);
        }
    }
    return best;
}

// Half the asphalt width, the renderer packs it in quarter metres into flags bits 26-31
// (Renderer.cpp, MaterialProperty::RoadHalfWidth). 0 where it is unknown.
float road_half_width(uint flags)
{
    return float((flags>>26)&63u)*.25;
}

// Box filtered coverage of a band of half width w at unsigned distance d, so cracks thinner
// than a pixel fade to their true average instead of aliasing.
float road_band(float d,float w,float footprint)
{
    float h=max(footprint,1e-4)*.5;
    return saturate((min(w,d+h)-max(-w,d-h))/(2*h));
}

// Distance to the nearest border of jittered unit cells, and the direction away from it.
float road_cell_border(float2 p,out float2 away)
{
    float2 n=floor(p),f=frac(p);
    float2 nearest_cell=0,nearest=0;
    float best=8;
    for (int j=-1;j<=1;j++)
    {
        for (int i=-1;i<=1;i++)
        {
            float2 g=float2(i,j);
            float2 r=g+road_hash2(n+g)*.8+.1-f;
            float d=dot(r,r);
            if (d<best)
            {
                best=d;
                nearest=r;
                nearest_cell=g;
            }
        }
    }
    best=8;
    away=float2(1,0);
    for (int y=-1;y<=1;y++)
    {
        for (int x=-1;x<=1;x++)
        {
            float2 g=nearest_cell+float2(x,y);
            float2 r=g+road_hash2(n+g)*.8+.1-f;
            float2 delta=r-nearest;
            if (dot(delta,delta)>1e-5)
            {
                float2 dir=normalize(delta);
                float d=dot(.5*(nearest+r),dir);
                if (d<best)
                {
                    best=d;
                    away=-dir;
                }
            }
        }
    }
    return best;
}

// Road space at a pixel. The deck maps lateral metres to u and distance along the road to v,
// rebased per segment by whole repeats, so frac(v) cuts the road into 1.5 m slices that
// line up across segments. Junction patches map world xz with a +x tangent instead.
struct RoadFrame
{
    float  lateral;    // metres from the centreline, right positive
    float  edge;       // metres to the asphalt edge, large where unknown
    float  along_t;    // 0-1 within the slice
    float2 slice;      // world xz of the slice centre on the centreline, constant across a slice
    float2 along;      // world xz direction of increasing v
    float2 right;
    float  half_width; // 0 where unknown
    bool   deck;
};

RoadFrame road_frame(uint flags,float3 world,float2 uv,float3 normal,float3 tangent)
{
    RoadFrame f;
    float shift=uv.x-world.x/road_asphalt_repeat;
    f.deck=!(tangent.x>.99999 && abs(frac(shift+.5)-.5)<.02);
    f.along_t=frac(uv.y);
    f.half_width=road_half_width(flags);
    if (f.deck)
    {
        f.right=normalize(tangent.xz+float2(1e-6,0));
        f.along=normalize(cross(normal,tangent).xz+float2(0,1e-6));
        f.lateral=uv.x*road_asphalt_repeat;
        f.edge=f.half_width>0 ? f.half_width-abs(f.lateral) : 100;
        f.slice=world.xz-f.along*(f.along_t-.5)*road_asphalt_repeat-f.right*f.lateral;
    }
    else
    {
        f.right=float2(1,0);
        f.along=float2(0,1);
        f.lateral=frac(world.x/3.5)*3.5;
        f.edge=100;
        f.slice=(floor(world.xz/road_asphalt_repeat)+.5)*road_asphalt_repeat;
    }
    return f;
}

// Everything a road has lived through, shared by the asphalt and the paint on top of it so a
// crack or a repair runs straight through the lines.
struct RoadWear
{
    float  crack;      // open crack coverage
    float  seal;       // bitumen crack sealant ("tar snakes")
    float2 slope;      // height gradient in world xz, tilts the normal into grooves
    float  dust;       // sand and dust blown off the verge
    float  ravel;      // broken edge where the aggregate has come loose
    float  fatigue;    // alligator cracked wheel path, slightly sunken and darker
    float  grime;      // dirt and moisture wicked into the asphalt either side of a crack
    float  oil;        // oil, coolant and fuel stains
    float  traffic;    // 1 on the deck, where lanes and wheel paths are known
};

RoadWear road_wear_none()
{
    RoadWear w;
    w.crack=0;w.seal=0;w.slope=0;
    w.dust=0;w.ravel=0;w.fatigue=0;w.grime=0;w.oil=0;w.traffic=0;
    return w;
}

// Adds a crack at distance d. Sealant is a smeared band of bitumen a few centimetres wide,
// open cracks are millimetres wide with a ragged width and a V groove.
void road_add_crack(inout RoadWear w,float d,float2 away,float sealed,float half_width,float presence,float footprint)
{
    if (presence<=0) return;
    // the squeegee leaves a feathered overband of uneven width
    float seal_width=.012+half_width*3;
    float seal=(road_band(d,seal_width,footprint)*.75+road_band(d,seal_width*1.8,footprint)*.25)*sealed*presence;
    float crack=road_band(d,half_width,footprint)*(1-sealed)*presence;
    // the groove walls face the crack, only resolved while the crack covers a few pixels
    float groove=crack*saturate(half_width*6/max(footprint,1e-4));
    // the stained margin is what makes a crack read from a car length away
    float halo=road_band(d,.006+half_width*3,footprint)*.7+road_band(d,.015+half_width*5,footprint)*.3;
    w.seal=max(w.seal,seal);
    w.crack=max(w.crack,crack);
    w.grime=max(w.grime,halo*(1-sealed*.6)*presence);
    w.slope+=away*groove*.9;
}

RoadWear road_wear(uint flags,float3 world,float2 uv,float3 normal,float3 tangent,float footprint)
{
    RoadWear w=road_wear_none();
    if (!(flags & ((1u<<22)|(1u<<23)))) return w;

    RoadFrame f=road_frame(flags,world,uv,normal,tangent);
    float2 xz=world.xz;
    float fp=max(footprint,1e-4);
    w.traffic=f.deck ? 1 : 0;

    // Old sections crack, newer resurfacing doesn't. Sealing crews work whole stretches.
    float age=road_noise(xz*.025);
    float distress=smoothstep(.28,.75,age*.7+road_noise(xz*.011+float2(91,17))*.3);
    // junctions take braking, turning and standing traffic
    if (!f.deck) distress=max(distress,.6);
    float sealed_zone=smoothstep(.62,.72,road_noise(xz*.009+float2(7,57)));

    // Transverse thermal cracks, one every 15-30 m, meandering across the road.
    if (f.deck)
    {
        float pick=road_noise(f.slice*3.1+float2(37,11));
        if (pick>lerp(.92,.8,distress))
        {
            float c=(.3+.4*road_noise(f.slice*2.3+float2(3,47)))*road_asphalt_repeat;
            float wobble=(road_noise(xz*.9+float2(17,5))-.5)*.25+(road_noise(xz*7.3)-.5)*.04;
            float signed_d=f.along_t*road_asphalt_repeat-c-wobble;
            float sealed=max(step(.68,road_noise(f.slice*1.7+float2(9,23))),sealed_zone);
            float presence=smoothstep(.24,.38,road_noise(xz*.35+float2(29,61)));
            float half_w=.0016+.0022*road_noise(xz*4.1);
            road_add_crack(w,abs(signed_d),f.along*sign(signed_d),saturate(sealed),half_w,presence,fp);
        }

        // Longitudinal cracks open along the paving joints: the centre and, on wide roads,
        // the lane joints. Long runs come and go along the road.
        float joint=0;
        float best=100;
        float joints[3]={0,3.5,-3.5};
        for (int k=0;k<(f.half_width>5 ? 3 : 1);k++)
        {
            float d=abs(f.lateral-joints[k]);
            if (d<best)
            {
                best=d;
                joint=joints[k];
            }
        }
        float jw=(road_noise(xz*.13+float2(joint*5,1))-.5)*.32+(road_noise(xz*1.9)-.5)*.05;
        float signed_j=f.lateral-joint-jw;
        float run=smoothstep(.44,.56,road_noise(xz*.021+float2(joint*7,33)))*smoothstep(.15,.3,road_noise(xz*.5+float2(5,1)));
        road_add_crack(w,abs(signed_j),f.right*sign(signed_j),sealed_zone,.0014+.002*road_noise(xz*3.3),run*(.5+.5*distress),fp);
    }

    // Block cracking in the oldest stretches: a coarse domain warped network of hairlines.
    if (distress>.45)
    {
        float2 p=xz/1.6;
        p+=(float2(road_noise(p*.7),road_noise(p*.7+float2(31,17)))-.5)*.9;
        p+=(float2(road_noise(p*4.3+float2(5,9)),road_noise(p*4.3+float2(77,1)))-.5)*.12;
        float2 away;
        float d=road_cell_border(p,away)*1.6;
        float visible=smoothstep(.55,.66,road_noise(xz*.45+float2(13,77)))*smoothstep(.45,.8,distress);
        road_add_crack(w,d,away,0,.0008+.0011*road_noise(xz*5.7),visible,fp);
    }

    // Fatigue (alligator) cracking where heavy wheels pass, and along the unsupported edge.
    if (f.deck)
    {
        float wheel=road_wheel_paths(uv,xz);
        float edge_zone=f.half_width>0 ? 1-smoothstep(.3,1.1,f.edge) : 0;
        float zone=smoothstep(.62,.75,road_noise(xz*.045+float2(53,19)))*smoothstep(.25,.55,road_noise(xz*.6+float2(9,4)));
        float mask=max(wheel*zone*distress,edge_zone*smoothstep(.45,.65,road_noise(xz*.08+float2(2,71))));
        if (mask>.05)
        {
            float2 p=xz/.14;
            p+=(float2(road_noise(p*.6),road_noise(p*.6+float2(7,3)))-.5)*.7;
            p+=(float2(road_noise(p*2.9+float2(3,1)),road_noise(p*2.9+float2(1,8)))-.5)*.25;
            float2 away;
            float d=road_cell_border(p,away)*.14;
            // not every cell edge has opened yet
            float opened=smoothstep(.35,.55,road_noise(xz*5.3+float2(17,2)));
            road_add_crack(w,d,away,0,.0006+.0007*road_noise(xz*9),smoothstep(.05,.45,mask)*opened*.85,fp);
            w.fatigue=smoothstep(.1,.6,mask);
        }
    }

    // Edges: the last decimetres crumble, sand and dust blow in off the verge and settle
    // where tyres never run.
    if (f.deck)
    {
        float edge_dust=0;
        if (f.half_width>0)
        {
            float reach=.55+.5*road_noise(xz*.31+float2(3,9))+.25*road_noise(xz*2.1);
            edge_dust=pow(saturate(1-f.edge/reach),1.6)*.9;
            float crumble=.05+.12*road_noise(xz*1.3+float2(8,1))+.04*road_noise(xz*11);
            w.ravel=saturate((crumble-f.edge)/fp+.5);
        }
        float speckle=smoothstep(.62,.8,road_noise(xz*17+float2(1,9)))*smoothstep(.4,.7,road_noise(xz*.7));
        w.dust=saturate(edge_dust*(.75+.5*road_noise_soft(xz*1.7))+speckle*edge_dust*.6);
    }

    // Oil: drips land between the wheel paths and come and go with the traffic, cars standing
    // at a junction leave a dense cluster, and the odd large spill sits anywhere.
    float2 warp=(float2(road_noise_soft(xz*1.9),road_noise_soft(xz*1.9+float2(13,5)))-.5)*.45;
    float lane_centre=f.deck ? 1-smoothstep(.25,.75,abs(road_lane_offset(uv))) : 1;
    float drip_run=smoothstep(.6,.8,road_noise_soft(xz*.06+float2(23,3)));
    float drips=road_blobs(xz/.6+warp,.12,float2(.1,.3),float2(17,3))*lane_centre*drip_run*.7;
    float spills=road_blobs(xz/5+warp*.25,.015+.025*distress,float2(.08,.18),float2(61,29))*.75;
    float old_stains=road_blobs(xz/2.2+warp*.6,.05,float2(.15,.35),float2(5,91))*.3;
    w.oil=saturate(max(max(drips,spills),old_stains)*(1-w.dust));
    return w;
}

// Island asphalt tint, the paint material samples the same diffuse map untinted.
static const float3 road_asphalt_tint=float3(1.6,1.65,1.7);

void road_asphalt_weathering(float2 xz,float2 uv,float traffic,inout float3 albedo,inout float roughness,float footprint)
{
    float age=road_noise_soft(xz*.025);
    float patches=road_noise_soft(xz*.17+float2(17,29));
    // Binder-rich and fines-rich blotches break the texture repeat.
    float blotch=road_noise_soft(xz*1.1+float2(3,61))*.6+road_noise_soft(xz*2.9+float2(47,5))*.4;
    // old asphalt oxidises to a pale warm grey, newer stretches stay dark and cool
    albedo*=(.82+.28*age+.06*patches)*(.92+.16*blotch);
    albedo*=lerp(float3(.97,.98,1.01),float3(1.04,1.01,.95),age);
    roughness+=.04*(age-.5)-.02*(blotch-.5);

    float wheel=road_wheel_paths(uv,xz)*traffic;
    albedo*=1-.1*wheel;
    roughness-=.05*wheel;
    roughness=clamp(roughness,.3,.96);
}

// Damage that sits on top of both the asphalt and the paint.
void road_damage_apply(RoadWear w,float height,inout float3 albedo,inout float roughness)
{
    // sand settles between the stones first
    float dust=w.dust*lerp(1.25,.7,saturate(height));
    albedo=lerp(albedo,float3(.3,.27,.22),saturate(dust*.85));
    roughness=lerp(roughness,.92,dust*.7);

    albedo*=1-.3*w.ravel;
    roughness=lerp(roughness,.95,w.ravel);

    albedo*=1-.2*w.fatigue;
    albedo*=1-.25*w.grime;
    roughness=lerp(roughness,1,w.grime*.3);

    // fresh oil is a glossy brown black, it soaks in and dulls with age
    float3 oil_tone=lerp(float3(.55,.5,.45),float3(.2,.18,.16),w.oil);
    albedo=lerp(albedo,albedo*oil_tone,w.oil);
    roughness=lerp(roughness,roughness*.8,smoothstep(.5,1,w.oil));

    // sealant weathers from black gloss to a dusty dark grey
    float3 bitumen=float3(.03,.029,.027);
    albedo=lerp(albedo,bitumen,saturate(w.seal*.9));
    roughness=lerp(roughness,.42,w.seal);

    // cracks hold grit and shadow, not a black void
    albedo=lerp(albedo,albedo*.22,w.crack);
    roughness=lerp(roughness,1,w.crack);
}

// How much paint covers the asphalt, 0 to 1. Paint u is lateral metres / repeat like the
// asphalt, and every island line is centred on 0, +-0.12, +-3.5 or +-7 m with a ~0.06 m half
// width. Lines elsewhere (width transitions) keep a clean edge. Tyres wear the paint off the
// stone tops first, leaving it in the voids.
float road_paint_coverage(float2 uv,float2 xz,float height,float footprint)
{
    float a=abs(uv.x*road_asphalt_repeat);
    float d=min(min(a,abs(a-.12)),min(abs(a-3.5),abs(a-7)));
    float ragged=(road_noise(xz*60)-.5)*.016+(road_noise(xz*9+float2(13,7))-.5)*.012+(height-.5)*.012;
    float aa=max(footprint,.0015);
    float edge=smoothstep(-aa,aa,.052-d+ragged);
    edge=lerp(1,edge,(1-saturate(footprint*30))*step(d,.09));

    float2 turned=float2(xz.x*.8-xz.y*.6,xz.x*.6+xz.y*.8);
    float worn=smoothstep(.5,.85,road_noise(xz*1.7+float2(29,3))*.55+road_noise(turned*7+float2(5,41))*.25+road_noise(xz*23)*.2);
    float scuff=smoothstep(.72,.9,road_noise(xz*.35+float2(61,17)));
    // whole stretches of line are faded where the paint is old
    float faded=smoothstep(.55,.85,road_noise(xz*.04+float2(37,3)))*.35;
    float exposed=saturate(worn*.85+scuff*.5+faded)*smoothstep(.3,.75,height);
    return edge*(1-exposed);
}

// Shared by raster and ray hits, height is the aggregate height (0.6 where it is unknown).
// The metre-scale variation is independent of texture repeats and road segments, so junctions
// don't restart the weathering. Returns the paint coverage, 0 off paint.
float road_weathering(uint flags,RoadWear wear,float3 world,float2 uv,float height,inout float3 albedo,inout float roughness,float footprint)
{
    if (!(flags & ((1u<<22)|(1u<<23)))) return 0;
    float coverage=0;
    if (flags & (1u<<22))
    {
        road_asphalt_weathering(world.xz,uv,wear.traffic,albedo,roughness,footprint);
    }
    if (flags & (1u<<23))
    {
        float3 under=albedo*road_asphalt_tint;
        float under_roughness=roughness;
        road_asphalt_weathering(world.xz,uv,wear.traffic,under,under_roughness,footprint);

        float wear_noise=road_noise(world.xz*.6);
        float grime=road_noise(world.xz*.13+float2(9,3));
        float3 paint=float3(.6,.59,.55)*(.82+.18*wear_noise)*lerp(float3(1,1,1),float3(.9,.87,.8),grime*.7);
        coverage=road_paint_coverage(uv,world.xz,height,footprint);
        albedo=lerp(under,paint,coverage);
        roughness=lerp(under_roughness,max(roughness,.62),coverage);
    }
    road_damage_apply(wear,height,albedo,roughness);
    return coverage;
}

float road_weathering(uint flags,float3 world,float2 uv,float3 normal,float3 tangent,float height,inout float3 albedo,inout float roughness,float footprint)
{
    if (!(flags & ((1u<<22)|(1u<<23)))) return 0;
    RoadWear wear=road_wear(flags,world,uv,normal,tangent,footprint);
    return road_weathering(flags,wear,world,uv,height,albedo,roughness,footprint);
}

// Raster only. The asphalt normal map is shallow for aggregate this coarse, so its slopes are
// doubled. Paint is ~3 mm of thermoplastic: it softens the stones, and grime settles in the
// voids between them, which is what makes a line read as painted onto asphalt.
void road_surface_detail(uint flags,RoadWear wear,float3 world,float2 uv,float3 geometric_normal,float height,float paint,
    inout float3 albedo,inout float3 normal,inout float occlusion)
{
    if (!(flags & ((1u<<22)|(1u<<23)))) return;
    normal=normalize(geometric_normal+(normal-geometric_normal*dot(normal,geometric_normal))*2.2);
    if (flags & (1u<<22))
    {
        normal=normalize(lerp(normal,geometric_normal,road_wheel_paths(uv,world.xz)*.15*wear.traffic));
    }
    if (flags & (1u<<23))
    {
        normal=normalize(lerp(normal,geometric_normal,paint*.45));
        float void_mask=saturate((.32-height)/.22);
        albedo=lerp(albedo,float3(.05,.05,.052),void_mask*.8*paint);
        occlusion=lerp(occlusion,lerp(1,occlusion,.6),paint);
    }

    // bitumen and dust fill the texture, cracks cut into it
    normal=normalize(lerp(normal,geometric_normal,saturate(wear.seal*.8+wear.dust*.35+wear.oil*.3)));
    float3 slope=float3(wear.slope.x,0,wear.slope.y);
    slope-=geometric_normal*dot(slope,geometric_normal);
    normal=normalize(normal-slope);
    occlusion*=1-.55*wear.crack-.2*wear.ravel;
}

// Aggregate height for the wet film, 0 in the voids to 1 on the stone tops. -1 elsewhere.
// Water lingers in cracks and the sunken wheel paths.
float road_macro_height(uint flags,RoadWear wear,float height)
{
    if (!(flags & ((1u<<22)|(1u<<23)))) return -1;
    return height*(1-.8*wear.crack)*(1-.3*wear.fatigue);
}
#endif
