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

float road_noise(float2 p)
{
    float2 i=floor(p),f=frac(p);
    f=f*f*(3-2*f);
    return lerp(lerp(road_hash(i),road_hash(i+float2(1,0)),f.x),
                lerp(road_hash(i+float2(0,1)),road_hash(i+1),f.x),f.y);
}

// Metres per asphalt texture repeat, must match island_road_surface::asphalt_repeat.
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
    float o=(abs(road_lane_offset(uv))-.85)/.26;
    return exp(-.5*o*o)*lerp(.55,1,road_noise(xz*.3+float2(41,7)));
}

// Oil and coolant drip down the lane centre, between the wheel paths.
float road_oil(float2 uv,float2 xz,float footprint)
{
    float lane=1-smoothstep(.2,.55,abs(road_lane_offset(uv)));
    float drips=lerp(.16,smoothstep(.6,.82,road_noise(xz*2.7+float2(5,19))),1-saturate(footprint*20-.5));
    float streak=smoothstep(.3,.8,road_noise(xz*.19+float2(23,3)));
    return lane*streak*(.3+.7*drips);
}

// Island asphalt tint, the paint material samples the same diffuse map untinted.
static const float3 road_asphalt_tint=float3(1.6,1.65,1.7);

void road_asphalt_weathering(float2 xz,float2 uv,inout float3 albedo,inout float roughness,float footprint)
{
    float age=road_noise(xz*.025);
    float patches=road_noise(xz*.17+float2(17,29));
    // Binder-rich and fines-rich blotches break the 3 m texture repeat.
    float blotch=road_noise(xz*1.1+float2(3,61))*.6+road_noise(xz*2.9+float2(47,5))*.4;
    albedo*=(.86+.20*age+.07*patches)*(.9+.2*blotch);
    roughness+=.045*(age-.5)-.025*patches-.04*(blotch-.5);

    float wheel=road_wheel_paths(uv,xz);
    albedo*=1-.14*wheel;
    roughness-=.1*wheel;

    float oil=road_oil(uv,xz,footprint);
    albedo*=1-.3*oil;
    roughness-=.16*oil;
    roughness=clamp(roughness,.3,.96);
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
    float exposed=saturate(worn*.85+scuff*.5)*smoothstep(.3,.75,height);
    return edge*(1-exposed);
}

// Shared by raster and ray hits, height is the aggregate height (0.6 where it is unknown).
// The metre-scale variation is independent of texture repeats and road segments, so junctions
// don't restart the weathering. Returns the paint coverage, 0 off paint.
float road_weathering(uint flags,float3 world,float2 uv,float height,inout float3 albedo,inout float roughness,float footprint)
{
    if (flags & (1u<<22))
    {
        road_asphalt_weathering(world.xz,uv,albedo,roughness,footprint);
    }
    if (flags & (1u<<23))
    {
        float3 under=albedo*road_asphalt_tint;
        float under_roughness=roughness;
        road_asphalt_weathering(world.xz,uv,under,under_roughness,footprint);

        float wear=road_noise(world.xz*.6);
        float grime=road_noise(world.xz*.13+float2(9,3));
        float3 paint=float3(.6,.59,.55)*(.82+.18*wear)*lerp(float3(1,1,1),float3(.9,.87,.8),grime*.7);
        float coverage=road_paint_coverage(uv,world.xz,height,footprint);
        albedo=lerp(under,paint,coverage);
        roughness=lerp(under_roughness,max(roughness,.62),coverage);
        return coverage;
    }
    return 0;
}

// Raster only. The asphalt normal map is shallow for aggregate this coarse, so its slopes are
// doubled. Paint is ~3 mm of thermoplastic: it softens the stones, and grime settles in the
// voids between them, which is what makes a line read as painted onto asphalt.
void road_surface_detail(uint flags,float3 world,float2 uv,float3 geometric_normal,float height,float paint,
    inout float3 albedo,inout float3 normal,inout float occlusion)
{
    if (!(flags & ((1u<<22)|(1u<<23)))) return;
    normal=normalize(geometric_normal+(normal-geometric_normal*dot(normal,geometric_normal))*2.2);
    if (flags & (1u<<22))
    {
        normal=normalize(lerp(normal,geometric_normal,road_wheel_paths(uv,world.xz)*.35));
    }
    if (flags & (1u<<23))
    {
        normal=normalize(lerp(normal,geometric_normal,paint*.45));
        float void_mask=saturate((.32-height)/.22);
        albedo=lerp(albedo,float3(.05,.05,.052),void_mask*.8*paint);
        occlusion=lerp(occlusion,lerp(1,occlusion,.6),paint);
    }
}

// Aggregate height for the wet film, 0 in the voids to 1 on the stone tops. -1 elsewhere.
float road_macro_height(uint flags,float height)
{
    return (flags & ((1u<<22)|(1u<<23))) ? height : -1;
}
#endif
