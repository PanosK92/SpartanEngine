/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once
#include "CarState.h"

namespace car
{
    struct suspension_pickups
    {
        PxVec3 inner[4], outer[4], toe_inner, toe_outer, shock_top, shock_bottom;
        // macpherson: the lower arm locates the ball joint and the damper body slides through its top mount
        bool strut = false;
    };

    namespace kinematics_design
    {
        // toe change per metre of bump travel, radians, toe in positive. both ends are set up for
        // mild roll understeer: the loaded front wheel toes out and the loaded rear wheel toes in.
        // about 0.02 and 0.015 degrees per centimetre, the range a road car is shimmed to.
        const float front_bump_steer = -0.035f;
        const float rear_bump_steer  = 0.026f;
        // virtual steer axis of a generated multi link. it meets the ground just outboard of the
        // contact patch (negative scrub) and leans inboard on the way up, so brake force at the patch
        // and drive force at the hub both toe the wheel in instead of out.
        const float multi_link_scrub       = -0.015f;
        const float multi_link_kpi_tan     = 0.158f;
        const float multi_link_trail_front = 0.02f;
        const float multi_link_trail_rear  = -0.02f;
    }

    inline bool solve_suspension_pose(const suspension_pickups& p, const PxVec3& hub, float bump, PxTransform& pose, float rack = 0, float tolerance = 2e-6f);

    // toe in angle of the solved upright relative to its design pose, radians
    inline bool solve_toe_in(const suspension_pickups& p, const PxVec3& hub, float bump, float& toe_in)
    {
        PxTransform pose(PxIdentity);
        if (!solve_suspension_pose(p, hub, bump, pose, 0, 4e-7f))
        {
            return false;
        }
        const PxVec3 axle = pose.q.rotate(PxVec3(1, 0, 0));
        const float side = hub.x < 0 ? -1.0f : 1.0f;
        // a positive rotation about chassis y points the wheel right, which is toe in on the left side
        toe_in = -side * atan2f(-axle.z, axle.x);
        return true;
    }

    // toe in gained per metre of bump travel around the design position, radians per metre
    inline bool bump_steer_gradient(const suspension_pickups& p, const PxVec3& hub, float& gradient)
    {
        const float travel = 0.03f;
        float bump = 0, droop = 0;
        if (!solve_toe_in(p, hub, travel, bump) || !solve_toe_in(p, hub, -travel, droop))
        {
            return false;
        }
        gradient = (bump - droop) / (2.0f * travel);
        return true;
    }

    // The toe link decides how the wheel steers as it travels. Its inner pivot height sets the
    // slope of that curve and its length sets the curvature, so both are solved here against the
    // same links the physical assembly is built from. Leaving the link level across converging
    // arms toed every car in by half a degree per centimetre of bump: roll steered the front
    // into the turn, squat steered the rear out of it and the car diverged at speed.
    inline bool tune_toe_link(suspension_pickups& p, const PxVec3& hub, float target_gradient)
    {
        const suspension_pickups original = p;
        const float side = hub.x < 0 ? -1.0f : 1.0f;
        const float travel = 0.03f;
        bool solved = true;
        auto toe = [&](float bump)
        {
            float value = 0;
            solved = solved && solve_toe_in(p, hub, bump, value);
            return value;
        };
        auto slope_error = [&](float y)
        {
            p.toe_inner.y = y;
            return (toe(travel) - toe(-travel)) / (2.0f * travel) - target_gradient;
        };
        auto curve_error = [&](float x)
        {
            p.toe_inner.x = side * x;
            return toe(travel) + toe(-travel) - 2.0f * toe(0);
        };
        auto secant = [&](auto& error, float start, float step, float low, float high, float tolerance)
        {
            float a = start, b = PxClamp(start + step, low, high);
            float error_a = error(a), error_b = error(b);
            for (int iteration = 0; iteration < 8 && solved; ++iteration)
            {
                if (fabsf(error_b) < tolerance || fabsf(error_b - error_a) < 1e-9f)
                {
                    break;
                }
                const float c = PxClamp(b - error_b * (b - a) / (error_b - error_a), low, high);
                if (c == b)
                {
                    break;
                }
                a = b; error_a = error_b;
                b = c; error_b = error(c);
            }
            // leave the pickup at the value the last evaluation used
            error(b);
            return b;
        };

        // the rack must keep a usable half length and the link cannot fold onto its outer joint
        const float inner_low = 0.10f;
        const float inner_high = fabsf(original.toe_outer.x) - 0.12f;
        const float y_low = original.toe_inner.y - 0.15f, y_high = original.toe_inner.y + 0.15f;
        for (int pass = 0; pass < 3 && solved; ++pass)
        {
            secant(slope_error, p.toe_inner.y, 0.005f, y_low, y_high, 1e-3f);
            if (inner_high > inner_low)
            {
                secant(curve_error, PxClamp(fabsf(p.toe_inner.x), inner_low, inner_high), -0.005f, inner_low, inner_high, 2e-5f);
            }
        }
        if (solved)
        {
            secant(slope_error, p.toe_inner.y, 0.005f, y_low, y_high, 1e-3f);
        }
        if (!solved)
        {
            p = original;
            return false;
        }
        return true;
    }

    inline suspension_pickups resolve_pickups(const suspension_geometry& g, const PxVec3& wheel, float wheel_radius, bool front_axle)
    {
        suspension_pickups p;
        p.strut = g.mechanism == suspension_mechanism::macpherson;
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
            if (!multi)
            {
                p.outer[i] = wheel + PxVec3(-side * (upper ? g.upper_upright_inset : g.lower_upright_inset), upper ? g.upper_upright_y : g.lower_upright_y, 0.0f);
                continue;
            }
            // Each link pair aims at a virtual ball joint and the two virtual joints form the steer
            // axis. Pairs that converged outboard of the wheel put that axis a quarter metre outside
            // the hub, where every newton of drive force levered the wheel into toe out.
            const float y = upper ? g.link_spread_y : -g.link_spread_y;
            const float trail = front_axle ? kinematics_design::multi_link_trail_front : kinematics_design::multi_link_trail_rear;
            const float apex_inset = kinematics_design::multi_link_scrub + (y + wheel_radius) * kinematics_design::multi_link_kpi_tan;
            const float inner_inset = half - fabsf(p.inner[i].x);
            const float outer_inset = upper ? g.upper_upright_inset : g.lower_upright_inset;
            const float reach = PxClamp((outer_inset - apex_inset) / PxMax(inner_inset - apex_inset, 0.05f), 0.02f, 0.9f);
            p.outer[i] = wheel + PxVec3(-side * outer_inset, y, trail + (z - trail) * reach);
        }

        // both tie rod pivots start on the lines through the arm pivots at the tie rod height, the
        // strut stands in for the upper arm on a macpherson corner
        const float lower_inner_x = half * g.chassis_inset;
        const float upper_inner_x = p.strut ? half - g.strut_top_inset : half * g.upper_chassis_inset;
        const float upper_inner_y = p.strut ? g.strut_top_y : g.upper_inner_y;
        const float upper_outer_inset = p.strut ? 0.0f : PxMax(g.upper_upright_inset, 0.0f);
        const float upper_outer_y = p.strut ? g.strut_top_y : g.upper_upright_y;
        const float inner_t = PxClamp((g.tie_rod_y - g.lower_inner_y) / PxMax(upper_inner_y - g.lower_inner_y, 0.01f), 0.0f, 1.0f);
        const float outer_t = PxClamp((g.tie_rod_y - g.lower_upright_y) / PxMax(upper_outer_y - g.lower_upright_y, 0.01f), 0.0f, 1.0f);
        const float inner_x = PxClamp(lower_inner_x + (upper_inner_x - lower_inner_x) * inner_t, 0.1f, PxMax(half - 0.1f, 0.1f));
        const float outer_inset = PxMax(PxMax(g.lower_upright_inset, 0.0f) + (upper_outer_inset - PxMax(g.lower_upright_inset, 0.0f)) * outer_t, 0.0f);
        // a rear toe link trails the axle. the arms give a little under cornering load while the
        // toe link barely carries any, so a link behind the hub lets the loaded wheel toe in.
        const float toe_z = front_axle ? g.tie_rod_z : -fabsf(g.tie_rod_z);
        p.toe_inner = PxVec3(side * inner_x, wheel.y + g.tie_rod_y, wheel.z + toe_z);
        p.toe_outer = wheel + PxVec3(-side * outer_inset, g.tie_rod_y, toe_z);
        p.shock_top = wheel + PxVec3(-side * g.strut_top_inset, g.strut_top_y, 0);
        p.shock_bottom = wheel + PxVec3(0, g.lower_upright_y, 0);
        tune_toe_link(p, wheel, front_axle ? kinematics_design::front_bump_steer : kinematics_design::rear_bump_steer);
        return p;
    }

    // Rigid upright kinematic sweep: four link lengths + toe link + hub height.
    // A wishbone is two links meeting at the same ball joint. A strut corner swaps the
    // upper pair for the damper axis, which is fixed in the upright and must keep
    // passing through the top mount. Rotation here is relative to the authored
    // design pose, so static alignment is preserved.
    inline bool solve_suspension_pose(const suspension_pickups& p, const PxVec3& hub, float bump, PxTransform& pose, float rack, float tolerance)
    {
        // Link lengths do not depend on where the hub sits. A 0.0001 finite-difference
        // step is smaller than float32 ulp once the hub is kilometres from the origin,
        // the Jacobian goes singular and the corner is rejected.
        suspension_pickups local = p;
        for (int i = 0; i < 4; ++i)
        {
            local.inner[i] -= hub;
            local.outer[i] -= hub;
        }
        local.toe_inner -= hub;
        local.toe_outer -= hub;
        local.shock_top -= hub;
        local.shock_bottom -= hub;
        PxVec3 strut_axis = local.shock_top - local.shock_bottom;
        if (p.strut && strut_axis.normalize() < 1e-4f)
        {
            return false;
        }

        float x[6] = {0, bump, 0, 0, 0, 0};
        auto transform = [&](const float* a) {
            PxVec3 r(a[3], a[4], a[5]); float angle = r.magnitude();
            return PxTransform(PxVec3(a[0], a[1], a[2]), angle > 1e-8f ? PxQuat(angle, r / angle) : PxQuat(PxIdentity));
        };
        auto link_error = [&](const PxTransform& t, const PxVec3& in, const PxVec3& rest_in, const PxVec3& out)
        {
            return (t.transform(out) - in).magnitude() - (out - rest_in).magnitude();
        };
        auto residual = [&](const float* a, float* f) {
            PxTransform t = transform(a);
            if (p.strut)
            {
                f[0] = link_error(t, local.inner[2], local.inner[2], local.outer[2]);
                f[1] = link_error(t, local.inner[3], local.inner[3], local.outer[2]);
                const PxVec3 axis = t.q.rotate(strut_axis);
                PxVec3 offset = local.shock_top - t.transform(local.shock_bottom);
                offset -= axis * offset.dot(axis);
                PxVec3 across = axis.cross(PxVec3(0, 0, 1));
                across.normalize();
                f[2] = offset.dot(across);
                f[3] = offset.dot(axis.cross(across));
            }
            else
            {
                for (int i = 0; i < 4; ++i)
                {
                    f[i] = link_error(t, local.inner[i], local.inner[i], local.outer[i]);
                }
            }
            f[4] = link_error(t, local.toe_inner + PxVec3(rack, 0, 0), local.toe_inner, local.toe_outer);
            f[5] = a[1] - bump;
        };
        auto largest = [](const float* f)
        {
            float error = 0;
            for (int i = 0; i < 6; ++i) error = PxMax(error, fabsf(f[i]));
            return error;
        };
        // a caller may ask for more than the float solve can deliver, the default bound still decides success
        const float accepted = 2e-6f;
        for (int iteration = 0; iteration < 24; ++iteration)
        {
            float f[6]; residual(x, f);
            if (largest(f) < tolerance) { pose = transform(x); pose.p += hub; return true; }
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
        float f[6]; residual(x, f);
        if (largest(f) < accepted) { pose = transform(x); pose.p += hub; return true; }
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
