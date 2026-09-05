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

//= INCLUDES ==============================
#include "pch.h"
#include "SkidMarks.h"
#include "SkidMarkDynamics.h"
#include "../../core/Timer.h"
#include "../../car/CarSimulation.h"
#include "../../physics/PhysicsWorld.h"
#include "Physics.h"
#include "Render.h"
#include "../Entity.h"
#include "../World.h"
#include "../../core/Engine.h"
#include "../../geometry/Mesh.h"
#include "../../math/Vector2.h"
#include "../../rendering/Material.h"
#include "../../rendering/Color.h"
#include "../../rendering/GeometryBuffer.h"

#include "../../rhi/RHI_Texture.h"


#include <cmath>
SP_WARNINGS_OFF
#include "../../io/pugixml.hpp"
SP_WARNINGS_ON
//=========================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    // eased 0..1 ramp, removes the visible line where a linear gradient would start
    static float smooth_fade(float t)
    {
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        return t * t * (3.0f - 2.0f * t);
    }

    static void pack_skid_fade(RHI_Vertex_PosTexNorTan& vertex, float fade)
    {
        vertex.tan = vertex_pack::pack_half2(fade, 0.0f);
    }

    static float hash01(int x, int y)
    {
        uint32_t n = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u;
        n = (n ^ (n >> 13)) * 1274126177u;
        return static_cast<float>(n & 0xffffffu) / 16777215.0f;
    }

    static float noise_fade(float t)
    {
        return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
    }

    static float noise_wrap(float x, float y, int period_x, int period_y)
    {
        float px = static_cast<float>(period_x);
        float py = static_cast<float>(period_y);
        x = fmodf(x, px);
        y = fmodf(y, py);
        if (x < 0.0f)
        {
            x += px;
        }
        if (y < 0.0f)
        {
            y += py;
        }

        int x0 = static_cast<int>(floorf(x)) % period_x;
        int y0 = static_cast<int>(floorf(y)) % period_y;
        if (x0 < 0)
        {
            x0 += period_x;
        }
        if (y0 < 0)
        {
            y0 += period_y;
        }
        int x1 = (x0 + 1) % period_x;
        int y1 = (y0 + 1) % period_y;
        float fx = noise_fade(x - floorf(x));
        float fy = noise_fade(y - floorf(y));
        float n00 = hash01(x0, y0);
        float n10 = hash01(x1, y0);
        float n01 = hash01(x0, y1);
        float n11 = hash01(x1, y1);
        float nx0 = n00 + (n10 - n00) * fx;
        float nx1 = n01 + (n11 - n01) * fx;
        return nx0 + (nx1 - nx0) * fy;
    }

    static float gauss(float x, float mu, float sigma)
    {
        float d = (x - mu) / sigma;
        return expf(-0.5f * d * d);
    }

    static void generate_skid_texture_rgba(vector<uint8_t>& bytes, uint32_t width, uint32_t height)
    {
        bytes.resize(static_cast<size_t>(width) * height * 4);
        for (uint32_t y = 0; y < height; y++)
        {
            float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(height);
            float edge = smooth_fade(v / 0.18f) * smooth_fade((1.0f - v) / 0.18f);
            float rib_l = gauss(v, 0.30f, 0.085f);
            float rib_r = gauss(v, 0.70f, 0.085f);
            float film  = gauss(v, 0.50f, 0.20f) * 0.42f;
            float profile = edge * (film + 0.95f * rib_l + 0.95f * rib_r);
            for (uint32_t x = 0; x < width; x++)
            {
                float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(width);
                float g0 = noise_wrap(u * 4.0f, v * 28.0f, 4, 28);
                float g1 = noise_wrap(u * 8.0f + 1.7f, v * 56.0f, 8, 56);
                float g2 = noise_wrap(u * 2.0f + 3.1f, v * 80.0f, 2, 80);
                float g3 = noise_wrap(u * 16.0f + 0.4f, v * 18.0f, 16, 18);
                float grain = g0 * 0.45f + g1 * 0.25f + g2 * 0.20f + g3 * 0.10f;
                float macro = 0.82f + 0.18f * noise_wrap(u * 3.0f, v * 2.0f, 3, 2);
                float breaks = 0.75f + 0.25f * noise_wrap(u * 5.0f + 2.2f, v * 3.0f, 5, 3);
                float alpha = profile * (0.50f + 0.50f * grain) * macro * breaks * 0.90f;
                alpha = alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha);

                float tone = 0.035f + 0.050f * grain;
                float r = tone * 1.15f;
                float g = tone * 0.92f;
                float b = tone * 0.78f;
                float polish = grain * grain * 0.025f;
                r += polish;
                g += polish * 0.9f;
                b += polish * 0.7f;

                auto to_u8 = [](float c) -> uint8_t
                {
                    c = c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
                    return static_cast<uint8_t>(c * 255.0f + 0.5f);
                };

                size_t i = (static_cast<size_t>(y) * width + x) * 4;
                bytes[i + 0] = to_u8(r);
                bytes[i + 1] = to_u8(g);
                bytes[i + 2] = to_u8(b);
                bytes[i + 3] = to_u8(alpha);
            }
        }
    }

    SkidMarks::SkidMarks(Entity* entity) : Component(entity)
    {
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_slip_threshold, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_min_segment_distance, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_max_segments, uint32_t);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_opacity, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_z_offset, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_uv_tiling, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_fade_distance, float);

    }

    SkidMarks::~SkidMarks()
    {

    }

    void SkidMarks::Tick()
    {
        if (Engine::IsFlagSet(EngineMode::Paused))
            return;

        ValidateSettings();
        if (m_material && m_material->GetProperty(MaterialProperty::ColorA) != m_opacity)
            m_material->SetProperty(MaterialProperty::ColorA, m_opacity);

        const float dt = static_cast<float>(Timer::GetDeltaTimeSec());
        m_time += std::min(dt, 0.25f);
        for (WheelTrail& trail : m_trails)
            AgeTrail(trail);

        if (!Engine::IsFlagSet(EngineMode::Playing))
        {
            for (WheelTrail& trail : m_trails)
            {
                FadeStripEnd(trail);
                trail.active = false;
                trail.intensity = 0.0f;
            }
            return;
        }
        if (!m_physics)
            m_physics = GetEntity()->GetComponent<Physics>();
        if (!m_physics || m_physics->GetBodyType() != BodyType::Vehicle || !m_physics->GetVehicleSimulation())
            return;

        auto* simulation = m_physics->GetVehicleSimulation();
        for (int i = 0; i < 4; ++i)
        {
            WheelTrail& trail = m_trails[i];
            const WheelIndex wheel_index = static_cast<WheelIndex>(i);
            const auto& wheel = simulation->get_wheel_state(i);
            auto* wheel_body = simulation->get_multibody_state().corners[i].wheel_body;
            auto stop = [&]()
            {
                FadeStripEnd(trail);
                trail.active = false;
                trail.has_edge = false;
                trail.intensity = 0.0f;
                trail.last_travel = Vector3::Zero;
                trail.stationary_patch = false;
                trail.patch_deposit = 0.0f;
            };
            if (!wheel.grounded || !wheel_body || wheel.tire_load <= 80.0f || !wheel.contact_point.isFinite() || !wheel.contact_normal.isFinite())
            {
                stop();
                continue;
            }

            const auto pose = wheel_body->getGlobalPose();
            auto ground_velocity = physx::PxVec3(0.0f);
            if (const auto* ground = wheel.contact_actor ? wheel.contact_actor->is<physx::PxRigidDynamic>() : nullptr)
            {
                const auto com = ground->getGlobalPose().transform(ground->getCMassLocalPose().p);
                ground_velocity = ground->getLinearVelocity() + ground->getAngularVelocity().cross(wheel.contact_point - com);
                // World-space ribbons cannot remain attached to a moving receiver.
                if (ground_velocity.magnitudeSquared() > 0.0225f)
                {
                    stop();
                    continue;
                }
            }
            auto normal_px = wheel.contact_normal.getNormalized();
            const float radius = std::max(wheel.effective_radius, 0.1f);
            auto patch_velocity = wheel_body->getLinearVelocity() - ground_velocity +
                wheel_body->getAngularVelocity().cross(-normal_px * radius);
            patch_velocity -= normal_px * patch_velocity.dot(normal_px);
            const float sliding_speed = patch_velocity.magnitude();
            float target = skid::demand(wheel.slip_ratio, wheel.slip_angle, sliding_speed, wheel.tire_load, m_slip_threshold, trail.active);
            // Loose/wet surfaces retain less visible rubber than dry pavement.
            const float surface_strength[] = { 1.0f, 0.95f, 0.50f, 0.45f, 0.30f, 0.04f };
            target *= surface_strength[std::clamp(static_cast<int>(wheel.contact_surface), 0, 5)];
            trail.intensity = skid::follow(trail.intensity, target, dt);
            if (trail.intensity < 0.006f && target < 0.006f)
            {
                stop();
                continue;
            }

            Vector3 normal(normal_px.x, normal_px.y, normal_px.z);
            // The physical hub is stable and includes steering/camber; the visual mesh has offsets.
            const auto hub_px = pose.p - normal_px * (pose.p - wheel.contact_point).dot(normal_px);
            Vector3 center = PhysicsWorld::ToWorldPosition(Vector3(hub_px.x, hub_px.y, hub_px.z));
            const auto axle_px = pose.q.rotate(physx::PxVec3(1.0f, 0.0f, 0.0f));
            Vector3 right(axle_px.x, axle_px.y, axle_px.z);
            right -= normal * Vector3::Dot(right, normal);
            if (!center.IsFinite() || right.LengthSquared() < 0.0001f)
            {
                stop();
                continue;
            }
            right.Normalize();
            // Preserve vertex ordering across right-side wheel orientation and reversing.
            if (Vector3::Dot(right, GetEntity()->GetRight()) < 0.0f)
                right = -right;
            const float half_width = m_physics->GetWheelWidth(wheel_index) * 0.5f * m_width_scale;
            if (half_width <= 0.0f)
            {
                stop();
                continue;
            }
            center += normal * m_z_offset;
            float distance = Vector3::Distance(center, trail.anchor_center);
            Vector3 travel = distance > 0.0001f ? (center - trail.anchor_center) / distance : trail.last_travel;
            const float speed = (wheel_body->getLinearVelocity() - ground_velocity).magnitude();
            if (trail.active && (fabsf(Vector3::Dot(center - trail.anchor_center, normal)) > 0.18f ||
                skid::discontinuity(distance, Vector3::Dot(normal, trail.edge_normal),
                    trail.last_travel.LengthSquared() > 0.0f ? Vector3::Dot(travel, trail.last_travel) : 1.0f, dt, speed)))
            {
                stop();
                continue;
            }
            if (!m_initialized)
                EnsureInitialized();
            if (!trail.active)
            {
                trail.active = true;
                trail.has_edge = true;
                trail.anchor_center = center;
                trail.edge_left = center - right * half_width;
                trail.edge_right = center + right * half_width;
                trail.edge_normal = normal;
                trail.intensity_edge = 0.0f;
                trail.u_accum = 0.0f;
                continue;
            }
            if (distance < m_min_segment_distance)
            {
                if (speed < 0.3f && target > 0.02f)
                    DepositStationaryPatch(trail, center, right, normal, half_width, wheel.contact_patch_length, dt);
                continue;
            }
            trail.stationary_patch = false;
            trail.patch_deposit = 0.0f;

            // A sliding tire can travel sideways. Its ribbon cross-section must stay
            // perpendicular to travel instead of collapsing along the spinning axle.
            right = Vector3::Cross(normal, travel).Normalized();
            if (Vector3::Dot(right, trail.edge_right - trail.edge_left) < 0.0f)
                right = -right;

            // Subdivide long frame steps so fades have enough vertices even at highway speed.
            const int steps = std::clamp(static_cast<int>(std::ceil(distance / 0.15f)), 1, 64);
            const Vector3 left_start = trail.edge_left;
            const Vector3 right_start = trail.edge_right;
            const Vector3 left_end = center - right * half_width;
            const Vector3 right_end = center + right * half_width;
            const Vector3 normal_start = trail.edge_normal;
            const float intensity_start = trail.intensity_edge;
            for (int step = 1; step <= steps; ++step)
            {
                float t = static_cast<float>(step) / steps;
                Vector3 left = Vector3::Lerp(left_start, left_end, t);
                Vector3 right_edge = Vector3::Lerp(right_start, right_end, t);
                Vector3 edge_normal = Vector3::Lerp(normal_start, normal, t).Normalized();
                float intensity = intensity_start + (trail.intensity - intensity_start) * t;
                float u_a = trail.u_accum;
                float u_b = u_a + distance / steps;
                float fade_a = smooth_fade(u_a / m_fade_distance) * trail.intensity_edge;
                float fade_b = smooth_fade(u_b / m_fade_distance) * intensity;
                DepositQuad(trail, left, right_edge, u_a, u_b, fade_a, fade_b, trail.intensity_edge, intensity, edge_normal, travel);
                trail.edge_left = left;
                trail.edge_right = right_edge;
                trail.edge_normal = edge_normal;
                trail.intensity_edge = intensity;
                trail.u_accum = u_b;
            }
            trail.anchor_center = center;
            trail.last_travel = travel;
            AgeTrail(trail);
        }
    }
    void SkidMarks::EnsureInitialized()
    {
        m_initialized = true;
        m_physics     = GetEntity()->GetComponent<Physics>();

        CreateMaterial();

        const char* names[4] = { "skidmarks_fl", "skidmarks_fr", "skidmarks_rl", "skidmarks_rr" };
        for (int i = 0; i < 4; i++)
        {
            BuildTrailMesh(m_trails[i], names[i]);
        }
    }

    void SkidMarks::BuildTrailMesh(WheelTrail& trail, const string& name)
    {
        trail.capacity_quads = std::clamp(m_max_segments, 32u, 16384u);
        trail.head_quad      = 0;
        trail.quads.resize(trail.capacity_quads);

        uint32_t quad_count   = trail.capacity_quads;
        uint32_t vertex_count = quad_count * 4;
        uint32_t index_count  = quad_count * 6;

        vector<RHI_Vertex_PosTexNorTan> vertices(vertex_count);
        vector<uint32_t> indices(index_count);

        // all quads start collapsed at the origin so nothing is visible until deposited
        for (uint32_t q = 0; q < quad_count; q++)
        {
            uint32_t base = q * 4;
            indices[q * 6 + 0] = base + 0;
            indices[q * 6 + 1] = base + 2;
            indices[q * 6 + 2] = base + 1;
            indices[q * 6 + 3] = base + 1;
            indices[q * 6 + 4] = base + 2;
            indices[q * 6 + 5] = base + 3;
        }

        trail.mesh = make_shared<Mesh>();
        trail.mesh->SetObjectName(name);
        trail.mesh->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessOptimize), false);
        trail.mesh->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessNormalizeScale), false);
        trail.mesh->AddGeometry(vertices, indices, false);
        trail.mesh->CreateGpuBuffers();

        // a standalone, identity-transform entity so world-space vertices are not transformed twice
        trail.entity = World::CreateEntity();
        trail.entity->SetObjectName(name);
        trail.entity->SetTransient(true);

        Render* render = trail.entity->AddComponent<Render>();
        render->SetMesh(trail.mesh.get(), 0);
        render->SetMaterial(m_material);
        render->SetFlag(RenderFlags::CastsShadows, false);
        render->SetFlag(RenderFlags::ExcludeFromRayTracing, true);

        trail.global_vertex_offset = render->GetVertexOffset();
    }

    void SkidMarks::DepositQuad(WheelTrail& trail, const Vector3& bl, const Vector3& br, float u_a, float u_b, float fade_a, float fade_b, float intensity_a, float intensity_b, const Vector3& normal, const Vector3& tangent)
    {
        uint32_t slot   = trail.head_quad % trail.capacity_quads;
        uint32_t offset = trail.global_vertex_offset + slot * 4;

        // Half-float UVs otherwise lose the tread pattern on long uninterrupted skids.
        const float uv_origin = floorf(u_a * m_uv_tiling / 32.0f) * 32.0f;
        float uv_a = u_a * m_uv_tiling - uv_origin;
        float uv_b = u_b * m_uv_tiling - uv_origin;

        // u tiles along travel, v spans the tire, fade lives in the tangent uint
        RecentQuad rq;
        rq.slot         = slot;
        rq.u_a          = u_a;
        rq.u_b          = u_b;
        rq.intensity_a  = intensity_a;
        rq.intensity_b  = intensity_b;
        rq.fade_a       = fade_a;
        rq.fade_b       = fade_b;
        rq.birth_time   = m_time;
        rq.sequence     = ++trail.quad_sequence;
        rq.occupied     = true;
        rq.verts[0]     = RHI_Vertex_PosTexNorTan(trail.edge_left,  Vector2(uv_a, 0.0f), trail.edge_normal, tangent);
        rq.verts[1]     = RHI_Vertex_PosTexNorTan(trail.edge_right, Vector2(uv_a, 1.0f), trail.edge_normal, tangent);
        rq.verts[2]     = RHI_Vertex_PosTexNorTan(bl,               Vector2(uv_b, 0.0f), normal, tangent);
        rq.verts[3]     = RHI_Vertex_PosTexNorTan(br,               Vector2(uv_b, 1.0f), normal, tangent);
        pack_skid_fade(rq.verts[0], fade_a);
        pack_skid_fade(rq.verts[1], fade_a);
        pack_skid_fade(rq.verts[2], fade_b);
        pack_skid_fade(rq.verts[3], fade_b);

        GeometryBuffer::UpdateVertices(rq.verts, offset, 4);
        trail.quads[slot] = rq;
        trail.head_quad = (trail.head_quad + 1) % trail.capacity_quads;

        // keep enough trailing quads to cover the fade distance, plus margin for larger segments
        // Remove records as soon as their slot is reused; an end fade must never
        // resurrect old geometry over a newer strip after a ring-buffer wrap.
        std::erase_if(trail.recent, [&](const RecentQuad& recent)
        {
            return recent.slot == slot || u_b - recent.u_b > m_fade_distance;
        });
        trail.recent.push_back(rq);
    }

    void SkidMarks::FadeStripEnd(WheelTrail& trail)
    {
        if (trail.recent.empty())
        {
            return;
        }

        // keep full width, ramp alpha to zero over the fade distance
        float u_end = trail.u_accum;
        for (RecentQuad& rq : trail.recent)
        {
            float fade_a = smooth_fade(rq.u_a / m_fade_distance) * smooth_fade((u_end - rq.u_a) / m_fade_distance) * rq.intensity_a;
            float fade_b = smooth_fade(rq.u_b / m_fade_distance) * smooth_fade((u_end - rq.u_b) / m_fade_distance) * rq.intensity_b;
            RecentQuad& stored = trail.quads[rq.slot];
            if (stored.sequence != rq.sequence)
                continue;
            stored.fade_a = fade_a;
            stored.fade_b = fade_b;
            fade_a *= stored.age_fade;
            fade_b *= stored.age_fade;
            pack_skid_fade(rq.verts[0], fade_a);
            pack_skid_fade(rq.verts[1], fade_a);
            pack_skid_fade(rq.verts[2], fade_b);
            pack_skid_fade(rq.verts[3], fade_b);
            uint32_t offset = trail.global_vertex_offset + rq.slot * 4;
            GeometryBuffer::UpdateVertices(rq.verts, offset, 4);
        }
        trail.recent.clear();
    }

    void SkidMarks::AgeTrail(WheelTrail& trail)
    {
        BoundingBox bounds;
        bool has_bounds = false;
        for (RecentQuad& quad : trail.quads)
        {
            if (!quad.occupied)
                continue;
            bounds.Merge(BoundingBox(quad.verts, 4));
            has_bounds = true;
            const float age = m_time - quad.birth_time;
            const float remaining = static_cast<float>(trail.capacity_quads - (trail.quad_sequence - quad.sequence));
            const float fade = skid::retirement(age, remaining);
            if (fabsf(fade - quad.age_fade) < 0.001f && fade != 0.0f)
                continue;
            pack_skid_fade(quad.verts[0], quad.fade_a * fade);
            pack_skid_fade(quad.verts[1], quad.fade_a * fade);
            pack_skid_fade(quad.verts[2], quad.fade_b * fade);
            pack_skid_fade(quad.verts[3], quad.fade_b * fade);
            GeometryBuffer::UpdateVertices(quad.verts, trail.global_vertex_offset + quad.slot * 4, 4);
            quad.age_fade = fade;
            if (fade == 0.0f)
                quad.occupied = false;
        }
        // The old fixed +/-5 km bounds could cull marks at the player's 6 km spawn.
        // Track the live geometry so culling works anywhere, including after origin shifts.
        if (trail.entity && has_bounds)
            trail.entity->GetComponent<Render>()->SetBoundingBoxOverride(bounds);
    }

    void SkidMarks::DepositStationaryPatch(WheelTrail& trail, const Vector3& center, const Vector3& right,
        const Vector3& normal, float half_width, float patch_length, float dt)
    {
        // A stationary burnout deposits into one footprint, rather than adding a
        // stack of coplanar quads every frame or waiting for the car to move.
        if (!trail.stationary_patch)
        {
            const Vector3 forward = Vector3::Cross(right, normal).Normalized();
            const float half_length = std::clamp(patch_length * 0.5f, 0.10f, 0.22f);
            const Vector3 saved_left = trail.edge_left;
            const Vector3 saved_right = trail.edge_right;
            trail.edge_left = center - forward * half_length - right * half_width;
            trail.edge_right = center - forward * half_length + right * half_width;
            for (int i = 0; i < 2; ++i)
            {
                Vector3 end = center + forward * (i == 0 ? 0.0f : half_length);
                trail.patch_slots[i] = trail.head_quad;
                DepositQuad(trail, end - right * half_width, end + right * half_width,
                    i * half_length, (i + 1) * half_length, 0, 0, 0, 0, normal, forward);
                trail.edge_left = end - right * half_width;
                trail.edge_right = end + right * half_width;
            }
            trail.edge_left = saved_left;
            trail.edge_right = saved_right;
            trail.recent.clear(); // the footprint already has feathered ends
            trail.stationary_patch = true;
        }
        trail.patch_deposit = std::min(1.0f, trail.patch_deposit + trail.intensity * dt * 1.5f);
        for (int i = 0; i < 2; ++i)
        {
            RecentQuad& quad = trail.quads[trail.patch_slots[i]];
            quad.fade_a = i == 0 ? 0.0f : trail.patch_deposit;
            quad.fade_b = i == 0 ? trail.patch_deposit : 0.0f;
            quad.birth_time = m_time;
            quad.age_fade = -1.0f; // force the changed coverage onto the GPU
            quad.occupied = true;
        }
        AgeTrail(trail);
    }

    void SkidMarks::CreateMaterial()
    {
        // Generated from code on each material creation: no stale PNG/cache can silently
        // replace the alpha mask. Upload RGBA8 explicitly, with its mip chain and alpha intact.
        constexpr uint32_t size = 256;
        vector<uint8_t> pixels;
        generate_skid_texture_rgba(pixels, size, size);
        vector<RHI_Texture_Slice> slices(1);
        slices[0].mips.resize(1);
        slices[0].mips[0].bytes.resize(pixels.size());
        memcpy(slices[0].mips[0].bytes.data(), pixels.data(), pixels.size());
        m_texture = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, size, size, 1, 1,
            RHI_Format::R8G8B8A8_Unorm, RHI_Texture_Srv | RHI_Texture_Transparent,
            "skid_rubber_mask", std::move(slices));
        m_material = make_shared<Material>();
        m_material->SetPersistent(false);
        m_material->SetResourceName("skidmarks" + string(EXTENSION_MATERIAL));
        if (m_texture)
        {
            m_material->SetColor(Color(1.0f, 1.0f, 1.0f, m_opacity));
        }
        else
        {
            m_material->SetColor(Color(0.04f, 0.035f, 0.03f, m_opacity));
        }
        m_material->SetProperty(MaterialProperty::Roughness, 0.88f);
        m_material->SetProperty(MaterialProperty::Metalness, 0.0f);
        m_material->SetProperty(MaterialProperty::IsSkidMark, 1.0f);
        m_material->SetProperty(MaterialProperty::TerrainBlend, 0.0f);
        m_material->SetProperty(MaterialProperty::Ior, 1.0f);
        m_material->SetProperty(MaterialProperty::CullMode, static_cast<float>(RHI_CullMode::None));
        if (m_texture)
        {
            m_material->SetTexture(MaterialTextureType::Color, m_texture);
        }
    }

    void SkidMarks::Remove()
    {
        for (int i = 0; i < 4; i++)
        {
            if (m_trails[i].entity)
            {
                World::RemoveEntity(m_trails[i].entity);
                m_trails[i].entity = nullptr;
            }
            m_trails[i] = WheelTrail{};
        }
        m_initialized = false;
        m_physics = nullptr;
        m_material.reset();
        m_texture.reset();
    }

    void SkidMarks::ValidateSettings()
    {
        auto finite_clamp = [](float value, float fallback, float low, float high)
        {
            return std::clamp(std::isfinite(value) ? value : fallback, low, high);
        };
        m_slip_threshold = finite_clamp(m_slip_threshold, 0.35f, 0.05f, 2.0f);
        m_min_segment_distance = finite_clamp(m_min_segment_distance, 0.05f, 0.02f, 0.25f);
        m_opacity = finite_clamp(m_opacity, 0.75f, 0.0f, 0.99f);
        m_z_offset = finite_clamp(m_z_offset, 0.02f, 0.005f, 0.04f);
        m_uv_tiling = finite_clamp(m_uv_tiling, 1.25f, 0.1f, 8.0f);
        m_fade_distance = finite_clamp(m_fade_distance, 1.1f, 0.15f, 5.0f);
        m_max_segments = std::clamp(m_max_segments, 32u, 16384u);
    }

    void SkidMarks::Save(pugi::xml_node& node)
    {
        node.append_attribute("slip_threshold")       = m_slip_threshold;
        node.append_attribute("min_segment_distance") = m_min_segment_distance;
        node.append_attribute("max_segments")         = m_max_segments;
        node.append_attribute("opacity")              = m_opacity;
        node.append_attribute("z_offset")             = m_z_offset;
        node.append_attribute("uv_tiling")            = m_uv_tiling;
        node.append_attribute("fade_distance")        = m_fade_distance;
    }

    void SkidMarks::Load(pugi::xml_node& node)
    {
        m_slip_threshold       = node.attribute("slip_threshold").as_float(0.35f);
        m_min_segment_distance = node.attribute("min_segment_distance").as_float(0.05f);
        m_max_segments         = node.attribute("max_segments").as_uint(4096);
        m_opacity              = node.attribute("opacity").as_float(0.75f);
        m_z_offset             = node.attribute("z_offset").as_float(0.02f);
        m_uv_tiling            = node.attribute("uv_tiling").as_float(1.25f);
        m_fade_distance        = node.attribute("fade_distance").as_float(1.1f);
        ValidateSettings();
    }
}
