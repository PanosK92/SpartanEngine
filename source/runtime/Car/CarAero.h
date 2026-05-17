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
#include <algorithm>
#include "CarState.h"
//==========================================

// aero: drag / downforce / ground effect / drs / rolling resistance, plus one-time
// shape inference from the chassis mesh (silhouette area, drag coefficient, aero
// center) and a 2D convex-hull helper for the debug overlay.

namespace car
{
    // forward declaration - visualization helper lives below compute_aero_from_shape
    inline void compute_shape_visualization(const std::vector<PxVec3>& vertices, const PxVec3& min_pt, const PxVec3& max_pt);

    inline void compute_aero_from_shape(const std::vector<PxVec3>& vertices)
    {
        if (vertices.size() < 4)
        {
            return;
        }

        PxVec3 min_pt(FLT_MAX, FLT_MAX, FLT_MAX);
        PxVec3 max_pt(-FLT_MAX, -FLT_MAX, -FLT_MAX);

        for (const PxVec3& v : vertices)
        {
            min_pt.x = PxMin(min_pt.x, v.x);
            min_pt.y = PxMin(min_pt.y, v.y);
            min_pt.z = PxMin(min_pt.z, v.z);
            max_pt.x = PxMax(max_pt.x, v.x);
            max_pt.y = PxMax(max_pt.y, v.y);
            max_pt.z = PxMax(max_pt.z, v.z);
        }

        float width  = max_pt.x - min_pt.x;
        float height = max_pt.y - min_pt.y;
        float length = max_pt.z - min_pt.z;

        float frontal_fill_factor = 0.82f;
        float computed_frontal_area = width * height * frontal_fill_factor;

        float side_fill_factor = 0.75f;
        float computed_side_area = length * height * side_fill_factor;

        float length_height_ratio = length / PxMax(height, 0.1f);
        float base_cd = 0.32f;
        float ratio_factor = PxClamp(2.5f / length_height_ratio, 0.8f, 1.3f);
        float computed_drag_coeff = base_cd * ratio_factor;

        // only fill fields the preset left at 0 (sentinel) - presets with hand-tuned
        // values must not be stomped on every chassis / preset change
        if (computed_frontal_area > 0.5f && computed_frontal_area < 10.0f && tuning::spec.frontal_area == 0.0f)
        {
            tuning::spec.frontal_area = computed_frontal_area;
            SP_LOG_INFO("aero: frontal area = %.2f m²", computed_frontal_area);
        }

        if (computed_side_area > 1.0f && computed_side_area < 20.0f && tuning::spec.side_area == 0.0f)
        {
            tuning::spec.side_area = computed_side_area;
            SP_LOG_INFO("aero: side area = %.2f m²", computed_side_area);
        }

        if (computed_drag_coeff > 0.2f && computed_drag_coeff < 0.6f && tuning::spec.drag_coeff == 0.0f)
        {
            tuning::spec.drag_coeff = computed_drag_coeff;
            SP_LOG_INFO("aero: drag coefficient = %.3f", computed_drag_coeff);
        }

        float centroid_y = 0.0f;
        float centroid_z = 0.0f;
        float front_area = 0.0f;
        float rear_area = 0.0f;
        float mid_z = (min_pt.z + max_pt.z) * 0.5f;

        for (const PxVec3& v : vertices)
        {
            float h = v.y - min_pt.y;
            float weight = h * h;
            centroid_y += v.y * weight;
            centroid_z += v.z * weight;

            if (v.z > mid_z)
            {
                front_area += weight;
            }
            else
            {
                rear_area += weight;
            }
        }

        float total_weight = 0.0f;
        for (const PxVec3& v : vertices)
        {
            float h = v.y - min_pt.y;
            total_weight += h * h;
        }

        if (total_weight > 0.0f)
        {
            centroid_y /= total_weight;
            centroid_z /= total_weight;
        }

        float total_area = front_area + rear_area;
        float front_bias = (total_area > 0.0f) ? front_area / total_area : 0.5f;

        float computed_aero_center_front_z = max_pt.z * 0.8f;
        float computed_aero_center_rear_z  = min_pt.z * 0.8f;

        if (tuning::spec.aero_center_height == 0.0f)
        {
            tuning::spec.aero_center_height   = centroid_y;
        }
        if (tuning::spec.aero_center_front_z == 0.0f)
        {
            tuning::spec.aero_center_front_z  = computed_aero_center_front_z;
        }
        if (tuning::spec.aero_center_rear_z == 0.0f)
        {
            tuning::spec.aero_center_rear_z   = computed_aero_center_rear_z;
        }

        SP_LOG_INFO("aero: dimensions %.2f x %.2f x %.2f m (L x W x H)", length, width, height);
        SP_LOG_INFO("aero: center height=%.2f, front_z=%.2f, rear_z=%.2f",
            tuning::spec.aero_center_height, tuning::spec.aero_center_front_z, tuning::spec.aero_center_rear_z);
        SP_LOG_INFO("aero: front/rear area bias=%.0f%%/%.0f%%",
            front_bias * 100.0f, (1.0f - front_bias) * 100.0f);

        // 2D silhouettes for the debug overlay are computed in the dedicated visualization
        // helper below, not here. the sim routine stays focused on aero inference.
        compute_shape_visualization(vertices, min_pt, max_pt);
    }

    // 2D convex hull via graham scan - used by the visualization overlay to draw
    // side and front silhouettes of the chassis. kept out of the aero routine so
    // that physics changes do not require thinking about visualization code.
    inline std::vector<std::pair<float, float>> graham_scan_hull_2d(std::vector<std::pair<float, float>> points)
    {
        if (points.size() < 3)
        {
            return points;
        }

        size_t pivot_idx = 0;
        for (size_t i = 1; i < points.size(); i++)
        {
            if (points[i].second < points[pivot_idx].second ||
                (points[i].second == points[pivot_idx].second && points[i].first < points[pivot_idx].first))
            {
                pivot_idx = i;
            }
        }
        std::swap(points[0], points[pivot_idx]);
        auto pivot = points[0];

        auto cross = [](const std::pair<float,float>& o, const std::pair<float,float>& a, const std::pair<float,float>& b) -> float
        {
            return (a.first - o.first) * (b.second - o.second) - (a.second - o.second) * (b.first - o.first);
        };

        std::sort(points.begin() + 1, points.end(), [&](const auto& a, const auto& b)
        {
            float c = cross(pivot, a, b);
            if (fabsf(c) < 1e-9f)
            {
                float da = (a.first - pivot.first) * (a.first - pivot.first) + (a.second - pivot.second) * (a.second - pivot.second);
                float db = (b.first - pivot.first) * (b.first - pivot.first) + (b.second - pivot.second) * (b.second - pivot.second);
                return da < db;
            }
            return c > 0;
        });

        std::vector<std::pair<float, float>> hull;
        for (const auto& pt : points)
        {
            while (hull.size() > 1 && cross(hull[hull.size()-2], hull[hull.size()-1], pt) <= 0)
                hull.pop_back();
            hull.push_back(pt);
        }
        return hull;
    }

    inline void compute_shape_visualization(const std::vector<PxVec3>& vertices, const PxVec3& min_pt, const PxVec3& max_pt)
    {
        shape_2d& sd = shape_data_ref();
        sd.min_x = min_pt.x; sd.max_x = max_pt.x;
        sd.min_y = min_pt.y; sd.max_y = max_pt.y;
        sd.min_z = min_pt.z; sd.max_z = max_pt.z;

        // side view: (z, y) silhouette
        std::vector<std::pair<float, float>> side_points;
        side_points.reserve(vertices.size());
        for (const PxVec3& v : vertices)
            side_points.push_back({v.z, v.y});
        sd.side_profile = graham_scan_hull_2d(std::move(side_points));

        // front view: (x, y) silhouette
        std::vector<std::pair<float, float>> front_points;
        front_points.reserve(vertices.size());
        for (const PxVec3& v : vertices)
            front_points.push_back({v.x, v.y});
        sd.front_profile = graham_scan_hull_2d(std::move(front_points));

        sd.valid = sd.side_profile.size() >= 3 && sd.front_profile.size() >= 3;
    }

    inline void apply_aero_and_resistance()
    {
        PxTransform pose = body->getGlobalPose();
        PxVec3 vel = body->getLinearVelocity();
        float speed = vel.magnitude();

        // aero application points from mesh-computed center - these are offsets from the
        // actor origin (pose.p), not from the center of mass. the chassis mesh is authored so
        // the actor origin coincides with the mesh origin, otherwise the debug arrows will not
        // sit on the car. the COM itself can still be set independently via set_center_of_mass
        float aero_height = tuning::spec.aero_center_height;
        PxVec3 front_pos = pose.p + pose.q.rotate(PxVec3(0, aero_height, tuning::spec.aero_center_front_z));
        PxVec3 rear_pos  = pose.p + pose.q.rotate(PxVec3(0, aero_height, tuning::spec.aero_center_rear_z));

        aero_debug.valid = false;
        aero_debug.position = pose.p;
        aero_debug.velocity = vel;
        aero_debug.front_aero_pos = front_pos;
        aero_debug.rear_aero_pos = rear_pos;
        float avg_r_aero = (cfg.front_wheel_radius + cfg.rear_wheel_radius) * 0.5f;
        aero_debug.ride_height = cfg.suspension_height + avg_r_aero;
        aero_debug.ground_effect_factor = 1.0f;
        aero_debug.yaw_angle = 0.0f;
        aero_debug.drag_force = PxVec3(0);
        aero_debug.front_downforce = PxVec3(0);
        aero_debug.rear_downforce = PxVec3(0);
        aero_debug.side_force = PxVec3(0);

        PxVec3 local_fwd   = pose.q.rotate(PxVec3(0, 0, 1));
        PxVec3 local_up    = pose.q.rotate(PxVec3(0, 1, 0));
        PxVec3 local_right = pose.q.rotate(PxVec3(1, 0, 0));

        float forward_speed = vel.dot(local_fwd);
        float lateral_speed = vel.dot(local_right);

        float yaw_angle = 0.0f;
        if (speed > 1.0f)
        {
            PxVec3 vel_norm = vel.getNormalized();
            float cos_yaw = PxClamp(vel_norm.dot(local_fwd), -1.0f, 1.0f);
            yaw_angle = acosf(fabsf(cos_yaw));
        }

        float front_compression = (wheels[front_left].compression + wheels[front_right].compression) * 0.5f;
        float rear_compression  = (wheels[rear_left].compression + wheels[rear_right].compression) * 0.5f;
        float pitch_angle = (rear_compression - front_compression) * cfg.suspension_travel / PxMax(cfg.wheelbase, 0.1f);

        float avg_compression = (front_compression + rear_compression) * 0.5f;
        float ride_height = cfg.suspension_height - avg_compression * cfg.suspension_travel + avg_r_aero;

        // drag (only above a tiny speed threshold to avoid normalizing the zero vector)
        PxVec3 drag_force_vec(0);
        if (speed > 0.5f)
        {
            float base_drag = 0.5f * tuning::air_density * tuning::spec.drag_coeff * tuning::spec.frontal_area * speed * speed;

            float yaw_drag_factor = 1.0f;
            if (tuning::spec.yaw_aero_enabled && yaw_angle > 0.01f)
            {
                float yaw_factor = sinf(yaw_angle);
                yaw_drag_factor = 1.0f + yaw_factor * (tuning::spec.yaw_drag_multiplier - 1.0f);
            }

            drag_force_vec = -vel.getNormalized() * base_drag * yaw_drag_factor;
            body->addForce(drag_force_vec, PxForceMode::eFORCE);
        }

        // side force, applied at the geometric centre of the side silhouette (mid-z between
        // the front and rear aero centres) so it produces a real yaw moment about the CG
        // this is the weather-vane effect, stabilising for forward motion when the CG is
        // ahead of the side aero centre, destabilising for rear-biased CGs which is realistic
        PxVec3 side_force_vec(0);
        if (tuning::spec.yaw_aero_enabled && fabsf(lateral_speed) > 1.0f)
        {
            float side_force = 0.5f * tuning::air_density * tuning::spec.yaw_side_force_coeff * tuning::spec.side_area * lateral_speed * fabsf(lateral_speed);
            side_force_vec = -local_right * side_force;
            float side_aero_z = (tuning::spec.aero_center_front_z + tuning::spec.aero_center_rear_z) * 0.5f;
            PxVec3 side_aero_pos = pose.p + pose.q.rotate(PxVec3(0, aero_height, side_aero_z));
            PxRigidBodyExt::addForceAtPos(*body, side_force_vec, side_aero_pos, PxForceMode::eFORCE);
        }

        // downforce
        PxVec3 front_downforce_vec(0);
        PxVec3 rear_downforce_vec(0);
        float ground_effect_factor = 1.0f;

        if (speed > 10.0f)
        {
            float dyn_pressure = 0.5f * tuning::air_density * speed * speed;

            float front_cl = tuning::spec.lift_coeff_front;
            float rear_cl  = tuning::spec.lift_coeff_rear;

            // drs reduces rear downforce for higher straight-line speed
            if (tuning::spec.drs_enabled && drs_active)
            {
                rear_cl *= tuning::spec.drs_rear_cl_factor;
            }

            if (tuning::spec.ground_effect_enabled)
            {
                if (ride_height < tuning::spec.ground_effect_height_max)
                {
                    float height_ratio = PxClamp((tuning::spec.ground_effect_height_max - ride_height) /
                                                 (tuning::spec.ground_effect_height_max - tuning::spec.ground_effect_height_ref), 0.0f, 1.0f);
                    ground_effect_factor = 1.0f + height_ratio * (tuning::spec.ground_effect_multiplier - 1.0f);
                }
            }

            if (tuning::spec.pitch_aero_enabled)
            {
                float pitch_shift = pitch_angle * tuning::spec.pitch_sensitivity;
                front_cl *= (1.0f - pitch_shift);
                rear_cl  *= (1.0f + pitch_shift);
            }

            float yaw_downforce_factor = 1.0f;
            if (tuning::spec.yaw_aero_enabled && yaw_angle > 0.1f)
            {
                yaw_downforce_factor = PxMax(0.3f, 1.0f - sinf(yaw_angle) * 0.7f);
            }

            float front_downforce = front_cl * dyn_pressure * tuning::spec.frontal_area * ground_effect_factor * yaw_downforce_factor;
            float rear_downforce  = rear_cl  * dyn_pressure * tuning::spec.frontal_area * ground_effect_factor * yaw_downforce_factor;

            front_downforce_vec = local_up * front_downforce;
            rear_downforce_vec  = local_up * rear_downforce;

            PxRigidBodyExt::addForceAtPos(*body, front_downforce_vec, front_pos, PxForceMode::eFORCE);
            PxRigidBodyExt::addForceAtPos(*body, rear_downforce_vec, rear_pos, PxForceMode::eFORCE);
        }

        // per-wheel rolling resistance: higher pressure = lower rr
        float rr_pressure_scale = 1.0f + (1.0f - tuning::spec.tire_pressure / PxMax(tuning::spec.tire_pressure_optimal, 0.1f)) * 0.3f;
        for (int i = 0; i < wheel_count; i++)
        {
            if (wheels[i].grounded && wheels[i].tire_load > 0.0f)
            {
                float rr_sign = (forward_speed > 0.0f) ? -1.0f : 1.0f;
                PxVec3 rr_force = local_fwd * rr_sign * tuning::spec.rolling_resistance * rr_pressure_scale * wheels[i].tire_load;
                PxVec3 wheel_pos = pose.transform(wheel_offsets[i]);
                PxRigidBodyExt::addForceAtPos(*body, rr_force, wheel_pos, PxForceMode::eFORCE);
            }
        }

        aero_debug.drag_force = drag_force_vec;
        aero_debug.front_downforce = front_downforce_vec;
        aero_debug.rear_downforce = rear_downforce_vec;
        aero_debug.side_force = side_force_vec;
        aero_debug.front_aero_pos = front_pos;
        aero_debug.rear_aero_pos = rear_pos;
        aero_debug.ride_height = ride_height;
        aero_debug.yaw_angle = yaw_angle;
        aero_debug.ground_effect_factor = ground_effect_factor;
        aero_debug.valid = true;
    }
}
