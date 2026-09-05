#pragma once
#include "CarState.h"

namespace car
{
    struct suspension_pickups
    {
        PxVec3 inner[4], outer[4], toe_inner, toe_outer, shock_top, shock_bottom;
    };

    inline suspension_pickups resolve_pickups(const suspension_geometry& g, const PxVec3& wheel)
    {
        suspension_pickups p;
        const float side = wheel.x < 0 ? -1.0f : 1.0f;
        const float half = fabsf(wheel.x);
        if (g.explicit_hardpoints)
        {
            auto point = [&](int i) { return wheel + PxVec3(-side * g.hardpoints[i][0], g.hardpoints[i][1], g.hardpoints[i][2]); };
            for (int i = 0; i < 4; ++i) { p.inner[i] = point(i * 2); p.outer[i] = point(i * 2 + 1); }
            p.toe_inner = point(8); p.toe_outer = point(9); p.shock_top = point(10); p.shock_bottom = point(11);
            return p;
        }
        const bool multi = g.mechanism == suspension_mechanism::multi_link;
        for (int i = 0; i < 4; ++i)
        {
            const bool upper = i < 2;
            const float z = (i % 2 == 0 ? 1.0f : -1.0f) * (multi ? g.link_spread_z : g.arm_span);
            p.inner[i] = PxVec3(wheel.x * (upper ? g.upper_chassis_inset : g.chassis_inset), wheel.y + (upper ? g.upper_inner_y : g.lower_inner_y), wheel.z + z);
            p.outer[i] = wheel + PxVec3(-side * (upper ? g.upper_upright_inset : g.lower_upright_inset),
                multi ? (upper ? g.link_spread_y : -g.link_spread_y) : (upper ? g.upper_upright_y : g.lower_upright_y), multi ? z * 0.45f : 0.0f);
        }
        float ti = PxClamp((g.tie_rod_y - g.lower_inner_y) / PxMax(g.upper_inner_y - g.lower_inner_y, 0.01f), 0.0f, 1.0f);
        float to = PxClamp((g.tie_rod_y - g.lower_upright_y) / PxMax(g.upper_upright_y - g.lower_upright_y, 0.01f), 0.0f, 1.0f);
        p.toe_inner = PxVec3(side * half * (g.chassis_inset + ti * (g.upper_chassis_inset - g.chassis_inset)), wheel.y + g.tie_rod_y, wheel.z + g.tie_rod_z);
        p.toe_outer = wheel + PxVec3(-side * (g.lower_upright_inset + to * (g.upper_upright_inset - g.lower_upright_inset)), g.tie_rod_y, g.tie_rod_z);
        p.shock_top = wheel + PxVec3(-side * g.strut_top_inset, g.strut_top_y, 0);
        p.shock_bottom = wheel + PxVec3(0, g.lower_upright_y, 0);
        return p;
    }

    // Rigid upright kinematic sweep: four link lengths + toe link + hub height.
    // A wishbone is two links meeting at the same ball joint. Rotation here is
    // relative to the authored design pose, so static alignment is preserved.
    inline bool solve_suspension_pose(const suspension_pickups& p, const PxVec3& hub, float bump, PxTransform& pose, float rack = 0)
    {
        float x[6] = {0, bump, 0, 0, 0, 0};
        auto transform = [&](const float* a) {
            PxVec3 r(a[3], a[4], a[5]); float angle = r.magnitude();
            return PxTransform(hub + PxVec3(a[0], a[1], a[2]), angle > 1e-8f ? PxQuat(angle, r / angle) : PxQuat(PxIdentity));
        };
        auto residual = [&](const float* a, float* f) {
            PxTransform t = transform(a);
            for (int i = 0; i < 5; ++i)
            {
                PxVec3 in = i < 4 ? p.inner[i] : p.toe_inner + PxVec3(rack, 0, 0);
                PxVec3 out = i < 4 ? p.outer[i] : p.toe_outer;
                PxVec3 rest_in = i < 4 ? p.inner[i] : p.toe_inner;
                f[i] = (t.transform(out - hub) - in).magnitude() - (out - rest_in).magnitude();
            }
            f[5] = a[1] - bump;
        };
        for (int iteration = 0; iteration < 24; ++iteration)
        {
            float f[6]; residual(x, f);
            float error = 0; for (float v : f) error = PxMax(error, fabsf(v));
            if (error < 2e-6f) { pose = transform(x); return true; }
            float m[6][7];
            for (int j = 0; j < 6; ++j)
            {
                float a[6]; for (int k = 0; k < 6; ++k) a[k] = x[k];
                a[j] += 0.0001f; float shifted[6]; residual(a, shifted);
                for (int i = 0; i < 6; ++i) m[i][j] = (shifted[i] - f[i]) / 0.0001f;
            }
            for (int i = 0; i < 6; ++i) m[i][6] = -f[i];
            for (int j = 0; j < 6; ++j)
            {
                int pivot = j; for (int i = j + 1; i < 6; ++i) if (fabsf(m[i][j]) > fabsf(m[pivot][j])) pivot = i;
                if (fabsf(m[pivot][j]) < 1e-7f) return false;
                for (int k = j; k < 7; ++k) { float v = m[j][k]; m[j][k] = m[pivot][k]; m[pivot][k] = v; }
                float divisor = m[j][j]; for (int k = j; k < 7; ++k) m[j][k] /= divisor;
                for (int i = 0; i < 6; ++i) if (i != j) { float v = m[i][j]; for (int k = j; k < 7; ++k) m[i][k] -= v * m[j][k]; }
            }
            for (int i = 0; i < 6; ++i) x[i] += PxClamp(m[i][6], -0.15f, 0.15f);
        }
        return false;
    }

    inline float suspension_motion_ratio(const suspension_pickups& p, const PxVec3& hub)
    {
        PxTransform plus, minus;
        if (!solve_suspension_pose(p, hub, 0.001f, plus) || !solve_suspension_pose(p, hub, -0.001f, minus)) return 0;
        float lp = (p.shock_top - plus.transform(p.shock_bottom - hub)).magnitude();
        float lm = (p.shock_top - minus.transform(p.shock_bottom - hub)).magnitude();
        return (lm - lp) / 0.002f;
    }
}
