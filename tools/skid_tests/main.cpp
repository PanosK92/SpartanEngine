#include "world/components/SkidMarkDynamics.h"
#include "rhi/RHI_Vertex.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

static void check(bool condition, const char* description)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", description);
        std::exit(1);
    }
}

int main()
{
    using namespace spartan::skid;
    check(demand(1, 1, 0.05f, 4000, 0.35f, false) == 0, "settling/idle slip must not mark");
    check(demand(1, 1, 20, 0, 0.35f, true) == 0, "unloaded spinning wheel must not mark");
    check(demand(0.02f, 0.01f, 5, 4000, 0.35f, false) == 0, "normal rolling must not mark");
    check(demand(1, 0, 10, 4000, 0.35f, false) > 0.9f, "burnout must mark");
    check(demand(-1, 0, 10, 4000, 0.35f, false) > 0.9f, "locked braking must mark");
    check(demand(0, 0.4f, 6, 4000, 0.35f, false) > 0.9f, "sideways drift must mark");
    check(demand(0.8f, 0.2f, 8, 4000, 0.35f, false) == demand(-0.8f, -0.2f, 8, 4000, 0.35f, false), "reverse symmetry");
    check(demand(0.3f, 0, 3, 4000, 0.35f, true) > demand(0.3f, 0, 3, 4000, 0.35f, false), "hysteresis around onset");
    check(demand(std::numeric_limits<float>::quiet_NaN(), 0, 8, 4000, 0.35f, true) == 0, "reject corrupt telemetry");
    check(follow(0, 1, 1.0f / 60) > 0 && follow(0, 1, 1.0f / 60) < 0.25f, "first frame ramps in");
    check(follow(1, 0, 1.0f / 60) > 0.8f, "first release frame does not pop out");
    float reference = 0;
    for (int rate : {30, 60, 144})
    {
        float intensity = 0;
        for (int i = 0; i < rate; ++i) intensity = follow(intensity, 1, 1.0f / rate);
        for (int i = 0; i < rate; ++i) intensity = follow(intensity, 0, 1.0f / rate);
        check(intensity < 0.001f, "release reaches invisibility");
        if (reference != 0) check(std::fabs(intensity - reference) < 0.00001f, "frame-rate independent intensity");
        reference = intensity;
    }
    check(discontinuity(25, 1, 1, 0.016f, 20), "teleport breaks strip");
    check(discontinuity(0.2f, 0.3f, 1, 0.016f, 20), "curb/wall normal breaks strip");
    check(discontinuity(0.2f, 1, -0.5f, 0.016f, 20), "reversal breaks strip");
    check(discontinuity(0.2f, 1, 1, 0.4f, 20), "long frame hitch breaks strip");
    check(!discontinuity(3.3f, 1, 0.9f, 1.0f / 30, 100), "legitimate high speed remains connected");
    check(ramp(0) == 0 && ramp(1) == 1 && ramp(0.5f) == 0.5f, "fade endpoints and midpoint");
    check(retirement(0, 4096) == 1 && retirement(48, 4096) == 1, "fresh marks retain coverage");
    check(retirement(54, 4096) == 0.5f && retirement(60, 4096) == 0, "old marks fade instead of popping");
    check(retirement(0, 13) == 0.5f && retirement(0, 1) == 0, "ring slot is invisible before reuse");
    for (float alpha : {0.0f, 0.001f, 0.05f, 0.5f, 1.0f})
    {
        auto packed = spartan::vertex_pack::pack_half2(alpha, 0.0f);
        check(std::fabs(spartan::vertex_pack::unpack_half2(packed).x - alpha) < 0.0005f, "vertex fade survives GPU half-float packing");
    }
    std::puts("PASS: skid deposition, contact gating, reverse symmetry, fade response at 30/60/144 Hz, and discontinuities");
}
