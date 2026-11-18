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

#endif
