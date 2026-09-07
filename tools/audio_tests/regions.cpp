// Copyright(c) 2015-2026 Panos Karabelas. Licensed under the MIT license.
#include "../../source/world/AudioRegion.h"
#include <cstdio>
#include <stdexcept>

void check(bool condition, const char* label)
{
    if (!condition) throw std::runtime_error(label);
    std::printf("PASS %s\n", label);
}

int main()
{
    using namespace spartan::audio_region;
    std::vector<Point> polygon = {{0,0},{100,0},{100,100},{0,100}};
    check(std::abs(signed_distance(polygon,{50,50})-50)<.001f, "inside distance");
    check(std::abs(signed_distance(polygon,{130,140})+50)<.001f, "outside corner distance");
    std::reverse(polygon.begin(),polygon.end());
    check(signed_distance(polygon,{50,50})==50, "either polygon winding");
    polygon = {{0,0},{100,0},{100,30},{30,30},{30,100},{0,100}};
    check(signed_distance(polygon,{60,60})<0 && signed_distance(polygon,{15,60})>0, "concave coast inlets");
    check(weight(0,180,false)==.5f, "boundary is midpoint");
    for (int i=-1800;i<=1800;++i)
    {
        const float distance=i*.1f;
        if (std::abs(weight(distance,180,false)+weight(-distance,180,false)-1)>1e-5f) throw std::runtime_error("crossfade power");
    }
    check(weight(1000,180,true)==0 && weight(-1000,180,true)==0 && weight(0,180,true)==1, "surf only near shoreline");
    check(weight(-1000,180,false)==0 && weight(1000,180,false)==1, "no ambience leakage far outside");
    float gain=0;
    for(int i=0;i<48000;++i) gain=slew(gain,1,48000,.5f);
    check(std::abs(gain-(1-std::exp(-2.f)))<.002f, "sample-rate envelope timing");
    for(int i=0;i<48000*6;++i) gain=slew(gain,0,48000,.5f);
    check(gain<.0001f, "silent voices retire after fade");
    check(std::isfinite(weight(0,0,false)), "zero fade is safe");
}
