// Copyright(c) 2015-2026 Panos Karabelas. Distributed under the MIT license.
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

// Shared by raster and ray hits. The metre-scale variation is independent of
// texture repeats and road segments, so junctions don't restart the weathering.
void road_weathering(uint flags,float3 world,inout float3 albedo,inout float roughness,float footprint)
{
    if (flags & (1u<<22))
    {
        float age=road_noise(world.xz*.025);
        float patches=road_noise(world.xz*.17+float2(17,29));
        albedo*=.86+.20*age+.07*patches;
        roughness=clamp(roughness+.045*(age-.5)-.025*patches,.68,.96);
    }
    if (flags & (1u<<23))
    {
        float wear=road_noise(world.xz*.6);
        float grain=lerp(road_noise(world.xz*95),.5,saturate(footprint*95-.5));
        albedo*=.76+.24*wear;
        albedo=lerp(albedo,float3(.055,.057,.060),smoothstep(.65,.84,grain)*.58);
        roughness=max(roughness,.78);
    }
}
#endif
