/*
Copyright(c) 2015-2026 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

//= INCLUDES ===============================
#pragma once
//==========================================

// tire math: pacejka magic formula curves, load-sensitive grip, temperature factors,
// per-surface friction scalars, brake efficiency. all pure functions, no state mutation.

#include "CarState.h"
namespace car
{

    // peak grip scales force stiffness shapes pacejka and relaxation controls slip lag
    struct tire_condition_modifiers
    {
        float peak_grip = 1.0f;
        float stiffness = 1.0f;
        float relaxation = 1.0f;
        float temperature_grip = 1.0f;
        float wear_grip = 1.0f;
    };

    struct tire_force_result
    {
        float longitudinal = 0.0f;
        float lateral = 0.0f;
    };

    inline float magic_formula(float slip, float stiffness, float shape, float peak, float curvature)
    {
        float stiffness_slip = stiffness * slip;
        return peak * sinf(shape * atanf(stiffness_slip - curvature * (stiffness_slip - atanf(stiffness_slip))));
    }

    inline float combined_slip_weight(float slip, float stiffness, float shape, float curvature)
    {
        float stiffness_slip = stiffness * slip;
        float weight = cosf(shape * atanf(stiffness_slip - curvature * (stiffness_slip - atanf(stiffness_slip))));
        return PxClamp(weight, 0.0f, 1.0f);
    }

    inline tire_force_result evaluate_magic_formula(const car_preset& preset, float slip_ratio, float slip_angle, float camber, float tire_load, float peak_force_longitudinal, float peak_force_lateral, float stiffness_scale, float camber_thrust_sign)
    {
        tire_force_result result;
        float load_ratio = PxClamp(tire_load / PxMax(preset.load_reference, 1.0f), 0.25f, 3.0f);
        float load_stiffness = powf(1.0f / PxMax(load_ratio, preset.load_B_scale_min), 0.4f);
        float longitudinal_stiffness = preset.long_B * load_stiffness * stiffness_scale;
        float lateral_stiffness = preset.lat_B * load_stiffness * stiffness_scale;
        float longitudinal_curvature = preset.long_E * PxMin(stiffness_scale, 1.0f);
        float lateral_curvature = preset.lat_E * PxMin(stiffness_scale, 1.0f);
        float pure_longitudinal = magic_formula(slip_ratio, longitudinal_stiffness, preset.long_C, peak_force_longitudinal, longitudinal_curvature);
        float pure_lateral = -magic_formula(slip_angle, lateral_stiffness, preset.lat_C, peak_force_lateral, lateral_curvature);
        float camber_thrust = camber_thrust_sign * camber * tire_load * preset.camber_thrust_coeff;
        float longitudinal_weight = combined_slip_weight(slip_angle, preset.combined_long_B, preset.combined_long_C, preset.combined_long_E);
        float lateral_weight = combined_slip_weight(slip_ratio, preset.combined_lat_B, preset.combined_lat_C, preset.combined_lat_E);
        result.longitudinal = pure_longitudinal * longitudinal_weight;
        result.lateral = pure_lateral * lateral_weight + camber_thrust * lateral_weight;
        float normalized_longitudinal = result.longitudinal / PxMax(peak_force_longitudinal, 1.0f);
        float normalized_lateral = result.lateral / PxMax(peak_force_lateral, 1.0f);
        float normalized_force = sqrtf(normalized_longitudinal * normalized_longitudinal + normalized_lateral * normalized_lateral);
        if (normalized_force > 1.0f)
        {
            result.longitudinal /= normalized_force;
            result.lateral /= normalized_force;
        }
        return result;
    }

    // the deflected carcass presses a chord of the tread flat against the road, so the patch length
    // follows the deflection and the tread stiffness acting over that area is the slip stiffness
    struct brush_tire_params
    {
        float patch_half_length = 0.0f; // m
        float stiffness_long    = 0.0f; // n per unit practical slip
        float stiffness_lat     = 0.0f; // n per unit practical slip
    };

    inline brush_tire_params evaluate_brush_params(const car_preset& preset, float wheel_radius, float tread_width, float tire_load, float stiffness_scale)
    {
        brush_tire_params params;
        float radius = PxMax(wheel_radius, 0.05f);
        float deflection = PxClamp(tire_load / PxMax(preset.tire_vertical_stiffness, 1.0f), 0.0f, radius * 0.5f);
        // the chord of a circle cut this deep is sqrt of two r d, a real belt lifts off before the
        // chord ends so the length that actually carries tread force is root two shorter
        params.patch_half_length = PxMax(sqrtf(radius * deflection), 0.005f);
        float area = params.patch_half_length * params.patch_half_length * PxMax(tread_width, 0.05f) * 2.0f;
        float scale = PxMax(stiffness_scale, 0.05f);
        params.stiffness_long = PxMax(preset.tread_stiffness_long * area * scale, 1.0f);
        params.stiffness_lat  = PxMax(preset.tread_stiffness_lat  * area * scale, 1.0f);
        return params;
    }

    // tread elements enter the patch undeflected, bend as they travel and break away once local friction
    // runs out, so the curve and the friction ellipse come out of stiffness and mu, not shape factors
    inline tire_force_result evaluate_brush_model(const car_preset& preset, const brush_tire_params& params, float slip_ratio, float slip_angle, float camber, float tire_load, float peak_force_longitudinal, float peak_force_lateral, float camber_thrust_sign, float& saturation)
    {
        tire_force_result result;
        saturation = 0.0f;

        // the brush measures deflection per unit distance the tread travels, so both axes have to be
        // referenced to the wheel surface speed rather than to the road speed the slip ratio used
        float practical_long = slip_ratio;
        float lateral_scale  = 1.0f - slip_ratio;
        if (slip_ratio < 0.0f)
        {
            // braking, the slip ratio is already referenced to the road so it converts across
            float rolling = PxMax(1.0f + slip_ratio, 0.05f);
            practical_long = slip_ratio / rolling;
            lateral_scale  = 1.0f / rolling;
        }
        float practical_lat = tanf(PxClamp(slip_angle, -1.4f, 1.4f)) * lateral_scale;

        // each axis is normalised by its own peak so a tire with different longitudinal and lateral
        // mu still produces one consistent ellipse instead of two independent curves
        float normalized_long = params.stiffness_long * practical_long / PxMax(3.0f * peak_force_longitudinal, 1.0f);
        float normalized_lat  = params.stiffness_lat  * practical_lat  / PxMax(3.0f * peak_force_lateral, 1.0f);
        float combined = sqrtf(normalized_long * normalized_long + normalized_lat * normalized_lat);
        saturation = combined;

        float camber_thrust = camber_thrust_sign * camber * tire_load * preset.camber_thrust_coeff;
        if (combined < 1e-6f)
        {
            result.lateral = camber_thrust;
            return result;
        }

        // rubber grips less the faster it is dragged, so mu decays as the sliding part of the patch grows,
        // which is what turns the flat topped brush plateau into a real peak followed by a drop
        float slide = PxClamp(preset.tire_slide_friction_ratio, 0.2f, 1.0f);
        float decay = combined * combined;
        float mu_scale = slide + (1.0f - slide) / (1.0f + decay * decay);

        // one minus the cube of the adhesion length fraction, unit slope at the origin and full mu at one
        float adhesion = PxMax(1.0f - combined, 0.0f);
        float utilization = (1.0f - adhesion * adhesion * adhesion) * mu_scale;

        result.longitudinal =  peak_force_longitudinal * utilization * normalized_long / combined;
        result.lateral      = -peak_force_lateral      * utilization * normalized_lat  / combined;
        // camber thrust has to come out of the same friction budget, it cannot survive a full slide
        result.lateral += camber_thrust * PxMax(1.0f - PxMin(combined, 1.0f), 0.0f);
        return result;
    }

    // lateral force builds from the leading edge backwards, so its centroid sits behind the patch
    // centre by a third of the half length and walks forward to zero as the patch saturates
    inline float evaluate_brush_trail(float patch_half_length, float saturation)
    {
        return patch_half_length / 3.0f * PxClamp(1.0f - saturation, 0.0f, 1.0f);
    }

    inline float evaluate_pneumatic_trail(const car_preset& preset, float slip_angle, float tire_load, float stiffness_scale)
    {
        float normalized_angle = fabsf(slip_angle) / PxMax(preset.pneumatic_trail_peak, 0.01f);
        float trail_shape = PxMax(cosf(1.1f * atanf(normalized_angle)), 0.0f);
        float load_scale = powf(PxClamp(tire_load / PxMax(preset.load_reference, 1.0f), 0.25f, 3.0f), 0.1f);
        return preset.pneumatic_trail_max * trail_shape * load_scale * stiffness_scale;
    }

}
