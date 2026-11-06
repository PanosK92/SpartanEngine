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
    float  age;        // 0 freshly laid, 1 oxidised grey with the stones worn bare
    float  polish;     // wheel paths, binder worn off and the stone tops polished by tyres
    float  rubber;     // tyre rubber streaks
    float  ghost;      // scar of a ground off marking, paint left in the voids
    float  repair;     // inside relaid asphalt
    float  paint_loss; // repairs cut through the lines and were not repainted
    float  bleach;     // sun bleached, dusty patches, lighter and matte
    float  stain;      // damp, dirty patches, darker
    float  flush;      // binder bled up over the stones, dark and glossy
    float  film;       // road dirt settled where tyres seldom run
    float  grit;       // loose pebbles lying on the surface, signed: light stones positive, dark negative
    float  texel;      // metres per filtered texel, what the aggregate is resolved at
};

RoadWear road_wear_none()
{
    RoadWear w;
    w.crack=0;w.seal=0;w.slope=0;
    w.dust=0;w.ravel=0;w.fatigue=0;w.grime=0;w.oil=0;w.traffic=0;
    w.age=.7;w.polish=0;w.rubber=0;w.ghost=0;w.repair=0;w.paint_loss=0;
    w.bleach=0;w.stain=0;w.flush=0;w.film=0;w.grit=0;w.texel=1;
    return w;
}

// Loose stones a few millimetres across, signed by tone. Fades once a stone is under a texel.
float road_grit(float2 xz,float density,float fp)
{
    float resolved=1-smoothstep(.002,.006,fp);
    if (resolved<=0 || density<=0) return 0;
    float light=road_blobs(xz/.012,density,float2(.18,.42),float2(3,11));
    float dark=road_blobs(xz/.009,density*.8,float2(.2,.45),float2(37,5));
    return (light-dark*.8)*resolved;
}

// Adds a crack at distance d. Sealant is a smeared band of bitumen a few centimetres wide,
// open cracks are millimetres wide with a ragged width and a V groove. Asphalt younger than a
// few years has not cracked yet.
void road_add_crack(inout RoadWear w,float d,float2 away,float sealed,float half_width,float presence,float footprint)
{
    presence*=smoothstep(.25,.55,w.age);
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

// Relaid asphalt, d is the signed distance to the saw cut in metres, positive inside. The joint
// is overbanded with a strip of sealant. Repairs through the lines lose their paint unless the
// crew repainted.
void road_add_repair(inout RoadWear w,float d,float age,float repainted,float fp)
{
    if (d<-.1) return;
    float inside=saturate(d/fp+.5);
    w.age=lerp(w.age,min(w.age,age),inside);
    w.repair=max(w.repair,inside);
    w.paint_loss=max(w.paint_loss,inside*(1-repainted));
    float overband=road_band(d,.028,fp)*.8+road_band(d,.05,fp)*.2;
    w.seal=max(w.seal,overband*.9);
}

// Repairs live in road space: transverse cuts are straight and square to the road, lane patches
// stop short of the lane lines. Runs of 1.5 m slices are picked from noise at the slice centre,
// so a repair continues across slices and its ends fall inside the first and last slice.
void road_add_repairs(inout RoadWear w,RoadFrame f,float2 xz,float fp)
{
    float along=f.along_t*road_asphalt_repeat;
    float2 prev=f.slice-f.along*road_asphalt_repeat;
    float2 next=f.slice+f.along*road_asphalt_repeat;
    float cut_a=(.1+.75*road_noise(f.slice*7.31+float2(3,17)))*road_asphalt_repeat;
    float cut_b=(.1+.75*road_noise(f.slice*6.17+float2(29,5)))*road_asphalt_repeat;

    // resurfacing contracts relay the full width for a hundred metres or so and repaint it
    {
        float2 seed=float2(71,13);
        if (road_noise(f.slice*.011+seed)>.72)
        {
            float y0=road_noise(prev*.011+seed)>.72 ? -100 : cut_a;
            float y1=road_noise(next*.011+seed)>.72 ? 100 : min(max(cut_b,y0+.3),1.45);
            road_add_repair(w,min(along-y0,y1-along),.18+.25*road_noise(f.slice*.003+seed),1,fp);
        }
    }

    // one lane relaid between two transverse cuts, the outer lane out to the asphalt edge
    float hw=f.half_width>0 ? f.half_width : 4;
    int lanes=max(1,(int)floor(hw/3.5));
    int lane=clamp((int)floor(f.lateral/3.5),-lanes,lanes-1);
    {
        float2 seed=float2(lane*13.7+5,lane*5.3+41);
        if (road_noise(f.slice*.045+seed)>.78)
        {
            float y0=road_noise(prev*.045+seed)>.78 ? -100 : cut_a;
            float y1=road_noise(next*.045+seed)>.78 ? 100 : min(max(cut_b,y0+.3),1.45);
            float x0=lane==-lanes ? -100 : lane*3.5+.12+.45*road_noise(xz*.02+seed);
            float x1=lane==lanes-1 ? 100 : lane*3.5+3.38-.45*road_noise(xz*.02+seed+float2(9,4));
            float d=min(min(f.lateral-x0,x1-f.lateral),min(along-y0,y1-along));
            float repainted=step(.5,road_noise(f.slice*.008+seed+float2(3,3)));
            road_add_repair(w,d,.4+.35*road_noise(f.slice*.01+seed),repainted,fp);
        }
    }

    // a utility trench across the road every hundred metres or so
    {
        float pick=road_noise(f.slice*5.17+float2(3,91));
        if (pick>.9)
        {
            float y0=(.1+.35*road_noise(f.slice*4.3+float2(11,2)))*road_asphalt_repeat;
            float y1=y0+.4+.35*road_noise(f.slice*3.9+float2(5,61));
            road_add_repair(w,min(along-y0,y1-along),.3+.3*road_noise(f.slice*2.7+float2(4,4)),0,fp);
        }
    }

    // pothole repairs, small rectangles cut square around the hole
    {
        float pick=road_noise(f.slice*3.73+float2(19,7));
        if (pick>.84)
        {
            float c=(road_noise(f.slice*2.9+float2(1,33))-.5)*2*max(hw-1,1);
            float half_lat=.25+.55*road_noise(f.slice*2.3+float2(47,9));
            float y0=(.05+.4*road_noise(f.slice*4.7+float2(8,8)))*road_asphalt_repeat;
            float y1=min(y0+.35+.7*road_noise(f.slice*3.1+float2(2,71)),1.45);
            float d=min(half_lat-abs(f.lateral-c),min(along-y0,y1-along));
            road_add_repair(w,d,.26+.35*road_noise(f.slice*1.9+float2(7,3)),0,fp);
        }
    }
}

// Tyre rubber, pairs of streaks a track width apart that drift across the lane and fade in and
// out. The tread leaves fine longitudinal ribs.
float road_rubber(RoadFrame f,float2 uv,float2 xz,float fp)
{
    float lane_offset=road_lane_offset(uv);
    float lane=floor(f.lateral/3.5);
    float marks=0;
    for (int k=0;k<2;k++)
    {
        float2 seed=float2(lane*7.1+k*31,lane*3.3+k*17);
        float run=smoothstep(.8,.9,road_noise(xz*.03+seed))*lerp(.3,1,road_noise(xz*.4+seed));
        if (run<=0) continue;
        float drift=(road_noise(xz*.05+seed+float2(5,5))-.5)*1.4;
        float track=.75+.08*k;
        float width=.07+.04*road_noise(xz*.2+seed);
        for (int side=-1;side<=1;side+=2)
        {
            float d=abs(lane_offset-drift-side*track);
            float ribs=.75+.25*sin(f.lateral*251+k*2);
            ribs=lerp(ribs,.75,saturate(fp*200));
            marks=max(marks,road_band(d,width,fp)*(1-.5*saturate(d/width))*ribs*run*(.55+.45*k));
        }
    }
    return saturate(marks);
}

RoadWear road_wear(uint flags,float3 world,float2 uv,float3 normal,float3 tangent,float footprint)
{
    RoadWear w=road_wear_none();
    if (!(flags & ((1u<<22)|(1u<<23)))) return w;

    RoadFrame f=road_frame(flags,world,uv,normal,tangent);
    float2 xz=world.xz;
    float fp=max(footprint,1e-4);
    w.traffic=f.deck ? 1 : 0;
    w.texel=fp;

    // Old sections crack, newer resurfacing doesn't. Sealing crews work whole stretches.
    float age=road_noise(xz*.025);
    float distress=smoothstep(.28,.75,age*.7+road_noise(xz*.011+float2(91,17))*.3);
    // junctions take braking, turning and standing traffic
    if (!f.deck) distress=max(distress,.6);
    float sealed_zone=smoothstep(.62,.72,road_noise(xz*.009+float2(7,57)));

    // most of the network is years old and greyed, with younger stretches between
    w.age=saturate(.3+.75*road_noise_soft(xz*.017+float2(5,83))+.2*(distress-.5));
    if (f.deck) road_add_repairs(w,f,xz,fp);

    // Transverse thermal cracks, one every 15-30 m, meandering across the road.
    if (f.deck)
    {
        float pick=road_noise(f.slice*3.1+float2(37,11));
        if (pick>lerp(.88,.72,distress))
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
    if (distress>.35 && w.age>.25)
    {
        float2 p=xz/1.6;
        p+=(float2(road_noise(p*.7),road_noise(p*.7+float2(31,17)))-.5)*.9;
        p+=(float2(road_noise(p*4.3+float2(5,9)),road_noise(p*4.3+float2(77,1)))-.5)*.12;
        float2 away;
        float d=road_cell_border(p,away)*1.6;
        float visible=smoothstep(.5,.62,road_noise(xz*.45+float2(13,77)))*smoothstep(.35,.7,distress);
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
            w.fatigue=smoothstep(.1,.6,mask)*smoothstep(.25,.55,w.age);
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

    if (f.deck)
    {
        float lane_offset=road_lane_offset(uv);
        w.polish=road_wheel_paths(uv,xz);

        // ruts a few millimetres deep under the wheels, invisible in albedo but they bend the
        // reflection of the sky running down the lane
        float o=(abs(lane_offset)-.85)/.3;
        float depth=.007*smoothstep(.3,.9,w.age)*(1-w.repair);
        w.slope+=f.right*(depth*o*exp(-.5*o*o)/.3*sign(lane_offset));

        w.rubber=road_rubber(f,uv,xz,fp);

        // a centre line ground off when the road was realigned, the old paint survives in the voids
        float ghost_offset=(road_noise(xz*.004+float2(11,59))-.5)*2.6;
        float ghost_run=smoothstep(.55,.65,road_noise(xz*.012+float2(83,7)))*smoothstep(.3,.45,abs(ghost_offset));
        float ragged=(road_noise(xz*9+float2(3,3))-.5)*.03;
        w.ghost=road_band(abs(f.lateral-ghost_offset),.075+ragged,fp)*ghost_run*(.6+.4*road_noise(xz*.8))*(1-w.repair);

        // drips between the wheel paths blend into a faint continuous stain down each lane
        float stripe=exp(-.5*(lane_offset/.32)*(lane_offset/.32))*smoothstep(.3,.6,road_noise_soft(xz*.05+float2(3,3)));
        w.oil=max(w.oil,stripe*.2*(1-w.dust));
    }

    // No road weathers evenly: sun, drainage and traffic leave patches metres across with
    // ragged edges. Fresh repairs have not had time to pick them up.
    float2 pw=(float2(road_noise_soft(xz*.23+float2(7,3)),road_noise_soft(xz*.23+float2(19,41)))-.5)*2.5;
    float ragged=road_noise(xz*2.3+float2(11,5))*.1+road_noise(xz*9.7+float2(3,29))*.06+road_noise(xz*31)*.03;
    float weathered=1-w.repair*(1-w.age)*.8;
    w.bleach=smoothstep(.6,.67,road_noise_soft(xz*.05+float2(23,71))*.5+road_noise_soft((xz+pw)*.21+float2(5,29))*.4+ragged)*lerp(.6,1,road_noise_soft(xz*.9+float2(8,2)))*weathered;
    w.stain=smoothstep(.62,.68,road_noise_soft((xz+pw)*.31+float2(61,2))*.75+ragged*1.4)*lerp(.5,1,road_noise_soft(xz*1.3+float2(3,17)))*weathered;
    w.bleach*=1-w.stain;

    // bleeding: in the heat the binder rises in the wheel paths and in whole panels laid
    // binder rich, a dark glaze over the stones that catches the sky
    float rich=smoothstep(.66,.72,road_noise_soft((xz+pw*.6)*.13+float2(13,88))+ragged*.9);
    float tracked=smoothstep(.55,.62,road_noise_soft(xz*.11+float2(71,23))*.85+ragged*.9)*w.polish;
    w.flush=saturate(max(rich*.85,tracked*.8))*(1-w.dust)*lerp(1,.4,w.repair*(1-w.age));

    // dirt drifts out of the wheel paths and settles between them and toward the edges
    float idle=f.deck ? saturate(1-w.polish*1.4) : .6;
    w.film=idle*smoothstep(.3,.7,road_noise_soft(xz*.37+float2(41,9))*.7+road_noise_soft(xz*2.3+float2(2,8))*.3)*lerp(.35,1,w.age);

    float scatter=.05+.25*w.dust+.12*w.film+.1*w.ravel;
    w.grit=road_grit(xz,scatter*smoothstep(.35,.6,road_noise_soft(xz*.9+float2(17,3))),fp);
    return w;
}

// Island asphalt tint, the paint material samples the same diffuse map untinted.
static const float3 road_asphalt_tint=float3(1.6,1.65,1.7);

// Mean linear luminance and chroma of the asphalt diffuse map, measured offline.
static const float road_texture_luminance=.0156;
static const float3 road_texture_chroma=float3(1.196,.941,.864);

// The diffuse map is fresh, brown track asphalt. A road is rebuilt from it as bitumen binder
// and aggregate: the bright, high texels are stones. New asphalt is ~0.05, traffic abrades the
// binder off the stone tops within a year and an old road settles around 0.12, speckled with
// bare stone. The stone mask loses contrast as the mips blur the stones, the bias keeps its
// mean (0.117) so the road doesn't darken with distance.
void road_asphalt_surface(RoadWear w,float2 xz,float3 tex,float height,inout float3 albedo,inout float roughness)
{
    float lum=max(dot(tex,float3(.2126,.7152,.0722)),1e-5);
    float mip=log2(max(w.texel,1e-6)*4096/road_asphalt_repeat);
    float far=smoothstep(1,5.5,mip);
    float stone=saturate(saturate((lum-.013)/.028)*smoothstep(.42,.74,height)+.076*far);
    float stone_lum=lerp(clamp(lum*12,.22,.6),.387,far);
    float3 hue=lerp(1,tex/lum/road_texture_chroma,.4*(1-far));
    float fines=lerp(sqrt(clamp(lum/road_texture_luminance,.25,4))/.946,1,far);

    // binder-rich and fines-rich blotches break the texture repeat
    float blotch=road_noise_soft(xz*1.1+float2(3,61))*.6+road_noise_soft(xz*2.9+float2(47,5))*.4;
    float3 binder=lerp(float3(.04,.041,.043),float3(.088,.085,.08),w.age)*lerp(1,fines,.5)*(.84+.32*blotch);
    float exposure=saturate(lerp(.3,.95,w.age)+.25*w.polish);
    float s=stone*exposure;
    albedo=lerp(binder,stone_lum*hue,s);
    // rubber and oil film darken the wheel paths, the strongest cue down a lane
    albedo*=1-.4*w.polish*smoothstep(.1,.5,w.age);

    float binder_rough=lerp(.72,.9,w.age)-.12*w.polish;
    float stone_rough=lerp(.6,.48,w.age)-.15*w.polish;
    roughness=lerp(binder_rough,stone_rough,s)*lerp(1,roughness/.83,.4);

    // the glaze drowns the stones but the tallest still poke through, dull
    float glaze=w.flush*lerp(1,.55,smoothstep(.65,.9,height));
    albedo=lerp(albedo,lerp(float3(.032,.031,.03),albedo*.6,.35),glaze*.85);
    roughness=lerp(roughness,lerp(.22,.34,w.age),glaze);
    roughness=clamp(roughness,.2,.97);
}

// Damage that sits on top of both the asphalt and the paint.
void road_damage_apply(RoadWear w,float height,inout float3 albedo,inout float roughness)
{
    // sun bleached patches go grey and chalky, damp dirty ones darken and stay a little satin
    float lum=dot(albedo,float3(.2126,.7152,.0722));
    albedo=lerp(albedo,lerp(albedo,lum,.5)*1.45+float3(.012,.011,.009),w.bleach*.8);
    roughness=lerp(roughness,.93,w.bleach*.6);
    albedo*=lerp(1,float3(.68,.67,.65),w.stain);
    roughness=lerp(roughness,.68,w.stain*.4);

    // road dirt is a brown grey film, thickest in the voids
    float film=w.film*lerp(1,.6,saturate(height));
    albedo=lerp(albedo,lerp(albedo,float3(.15,.135,.115),.6),film*.75);
    roughness=lerp(roughness,.9,film*.5);

    // grinding scars the stone flat and darker, the old paint hangs on in the voids
    float voids=saturate((.48-height)/.25);
    albedo*=1-.22*w.ghost;
    albedo=lerp(albedo,float3(.4,.39,.36),w.ghost*voids*.6);
    roughness=lerp(roughness,.8,w.ghost*.4);

    // tyre rubber is a near black, satin film
    albedo=lerp(albedo,float3(.02,.02,.021),w.rubber*.5);
    roughness=lerp(roughness,.6,w.rubber*.6);

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
    float3 bitumen=lerp(float3(.026,.025,.024),float3(.05,.048,.045),w.age);
    albedo=lerp(albedo,bitumen,saturate(w.seal*.9));
    roughness=lerp(roughness,lerp(.4,.6,w.age),w.seal);

    // cracks hold grit and shadow, not a black void
    albedo=lerp(albedo,albedo*.22,w.crack);
    roughness=lerp(roughness,1,w.crack);

    // loose stones: pale limestone chips and dark basalt grit
    albedo=lerp(albedo,float3(.42,.4,.36),saturate(w.grit)*.85);
    albedo=lerp(albedo,float3(.03,.03,.03),saturate(-w.grit)*.7);
    roughness=lerp(roughness,.85,abs(w.grit)*.6);
}

// How much paint covers the asphalt, 0 to 1. Paint u is lateral metres / repeat like the
// asphalt, and every island line is centred on 0, +-0.12, +-3.5 or +-7 m with a ~0.06 m half
// width. Lines elsewhere (width transitions) keep a clean edge. Tyres wear the paint off the
// stone tops first, leaving it in the voids. Paint is as old as the asphalt under it.
float road_paint_coverage(float2 uv,float2 xz,float height,float footprint,float age)
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
    float exposed=saturate(worn*.85+scuff*.5+faded)*smoothstep(.3,.75,height)*lerp(.12,1.15,smoothstep(.1,.7,age));
    return edge*(1-saturate(exposed));
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
        road_asphalt_surface(wear,world.xz,albedo/road_asphalt_tint,height,albedo,roughness);
    }
    if (flags & (1u<<23))
    {
        float3 under=albedo;
        float under_roughness=roughness;
        road_asphalt_surface(wear,world.xz,albedo,height,under,under_roughness);

        // white thermoplastic yellows and greys with age and picks up road grime
        float wear_noise=road_noise(world.xz*.6);
        float grime=road_noise(world.xz*.13+float2(9,3));
        float3 paint=lerp(float3(.74,.74,.72),float3(.56,.54,.49),wear.age)*(.86+.14*wear_noise)*lerp(float3(1,1,1),float3(.88,.85,.78),grime*.6);
        float paint_roughness=lerp(.48,.68,wear.age);

        // glass beads dropped into the hot paint, tiny glossy points while they are resolved
        float2 bead_cell=floor(float2(uv.x,frac(uv.y))*road_asphalt_repeat/.0025);
        float bead=step(.86,road_hash(bead_cell))*(1-smoothstep(.0006,.0025,wear.texel))*(1-.6*wear.age);
        paint*=1+.3*bead;
        paint_roughness=lerp(paint_roughness,.18,bead);

        coverage=road_paint_coverage(uv,world.xz,height,footprint,wear.age)*(1-wear.paint_loss);
        albedo=lerp(under,paint,coverage);
        roughness=lerp(under_roughness,paint_roughness,coverage);
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

    // bitumen, dust and rubber fill the texture, grinding flattens it, cracks cut into it
    float filled=wear.seal*.8+wear.dust*.35+wear.oil*.3+wear.rubber*.35+wear.ghost*.5+wear.repair*(1-wear.age)*.2+wear.flush*.7+wear.film*.2;
    normal=normalize(lerp(normal,geometric_normal,saturate(filled)));
    float3 slope=float3(wear.slope.x,0,wear.slope.y);
    slope-=geometric_normal*dot(slope,geometric_normal);
    normal=normalize(normal-slope);
    occlusion*=1-.55*wear.crack-.2*wear.ravel-.15*saturate(-wear.grit);
}

// Aggregate height for the wet film, 0 in the voids to 1 on the stone tops. -1 elsewhere.
// Water lingers in cracks and the sunken wheel paths.
float road_macro_height(uint flags,RoadWear wear,float height)
{
    if (!(flags & ((1u<<22)|(1u<<23)))) return -1;
    return height*(1-.8*wear.crack)*(1-.3*wear.fatigue);
}
#endif
