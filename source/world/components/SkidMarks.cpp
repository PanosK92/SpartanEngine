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
#include "../../resource/ResourceCache.h"
#include "../../rhi/RHI_Texture.h"
#include "../../file_system/FileSystem.h"
#include "../../resource/import/ImageImporter.h"
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
    // half-size of the bounding box anchor, keeps the ribbons from ever being frustum culled
    static const float aabb_extent = 5000.0f;

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
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_center_smoothing, float);
    }

    SkidMarks::~SkidMarks()
    {

    }

    void SkidMarks::Tick()
    {
        // editor idle must not spawn trail entities from settle slip
        if (!Engine::IsFlagSet(EngineMode::Playing) || Engine::IsFlagSet(EngineMode::Paused))
        {
            return;
        }

        if (!m_physics)
        {
            m_physics = GetEntity()->GetComponent<Physics>();
        }

        if (!m_physics || m_physics->GetBodyType() != BodyType::Vehicle)
        {
            return;
        }

        // the car lateral axis gives a stable strip width direction, free of per-segment contact jitter
        Vector3 car_right = GetEntity()->GetRight();

        // once a strip is going, keep it alive down to a lower threshold so it does not fragment into
        // many short strips that each pop in and out, which is what kills the fade
        float end_threshold = m_slip_threshold * 0.6f;

        for (int i = 0; i < 4; i++)
        {
            WheelTrail& trail   = m_trails[i];
            WheelIndex wheel    = static_cast<WheelIndex>(i);
            bool grounded       = m_physics->IsWheelGrounded(wheel);
            float slip          = m_physics->GetWheelSlipMagnitude(wheel);
            bool skidding       = trail.active ? (slip >= end_threshold) : (slip >= m_slip_threshold);

            // not skidding, fade out the tail and stop the strip so the next one starts fresh
            if (!grounded || !skidding)
            {
                if (trail.active)
                {
                    FadeStripEnd(trail);
                }
                trail.active     = false;
                trail.has_smooth = false;
                continue;
            }

            // create trail meshes only when a tire actually starts skidding
            if (!m_initialized)
            {
                EnsureInitialized();
            }

            Vector3 normal  = m_physics->GetWheelContactNormal(wheel);
            Vector3 contact = m_physics->GetWheelContactPoint(wheel);

            // placed under the hub projected onto the contact plane, the sweep contact jitters laterally and zigzags the mark
            Vector3 ground_point = contact;
            if (Entity* wheel_entity = m_physics->GetWheelEntity(wheel))
            {
                Vector3 hub  = wheel_entity->GetPosition();
                ground_point = hub - normal * Vector3::Dot(hub - contact, normal);
            }

            if (!trail.has_smooth)
            {
                trail.smooth_center = ground_point;
                trail.has_smooth    = true;
            }
            else
            {
                trail.smooth_center = Vector3::Lerp(trail.smooth_center, ground_point, m_center_smoothing);
            }

            // strip width comes from the physical tire, scaled to the contact patch
            float half_width = m_physics->GetWheelWidth(wheel) * 0.5f * m_width_scale;
            if (half_width <= 0.0f)
            {
                continue;
            }

            // width runs along the tire axle, projected onto the ground plane
            Vector3 right = car_right - normal * Vector3::Dot(car_right, normal);
            if (right.Length() < 0.0001f)
            {
                continue;
            }
            right.Normalize();

            float slip_intensity = 0.40f + 0.60f * smooth_fade((slip - end_threshold) / 0.70f);

            // start (or restart) a strip, no quad is laid until the wheel has moved
            if (!trail.active)
            {
                trail.active         = true;
                trail.has_edge       = false;
                trail.strip_index++;
                trail.intensity_edge = slip_intensity;
                // each wheel and each successive pass sits at a slightly different height so stacked marks do not z-fight
                trail.height_offset = m_z_offset + i * 0.0012f + (trail.strip_index % 6) * 0.0025f;
                trail.anchor_center = trail.smooth_center + normal * trail.height_offset;
                trail.recent.clear();
                continue;
            }

            Vector3 center = trail.smooth_center + normal * trail.height_offset;

            float d = Vector3::Distance(center, trail.anchor_center);
            if (d < m_min_segment_distance)
            {
                continue;
            }

            Vector3 travel = (center - trail.anchor_center) / d;

            // full width from the first cross section, fade is alpha not a width taper
            if (!trail.has_edge)
            {
                trail.u_accum        = 0.0f;
                trail.edge_left      = trail.anchor_center - right * half_width;
                trail.edge_right     = trail.anchor_center + right * half_width;
                trail.intensity_edge = slip_intensity;
                trail.has_edge       = true;
            }

            Vector3 b_left  = center - right * half_width;
            Vector3 b_right = center + right * half_width;
            float u_a       = trail.u_accum;
            float u_b       = trail.u_accum + d;
            float fade_a    = smooth_fade(u_a / m_fade_distance) * trail.intensity_edge;
            float fade_b    = smooth_fade(u_b / m_fade_distance) * slip_intensity;

            DepositQuad(trail, b_left, b_right, u_a, u_b, fade_a, fade_b, trail.intensity_edge, slip_intensity, normal, travel);

            // advance, the new edge becomes the start of the next quad for seamless continuity
            trail.edge_left      = b_left;
            trail.edge_right     = b_right;
            trail.u_accum       += d;
            trail.anchor_center  = center;
            trail.intensity_edge = slip_intensity;
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
        trail.capacity_quads = m_max_segments;
        trail.head_quad      = 0;

        uint32_t quad_count   = trail.capacity_quads;
        uint32_t vertex_count = quad_count * 4 + 2; // +2 anchors for a large, never-culled bounding box
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

        // anchors are never referenced by an index, they only stretch the bounding box
        vertices[vertex_count - 2].set_position(Vector3( aabb_extent,  aabb_extent,  aabb_extent));
        vertices[vertex_count - 1].set_position(Vector3(-aabb_extent, -aabb_extent, -aabb_extent));

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

        float uv_a = u_a * m_uv_tiling;
        float uv_b = u_b * m_uv_tiling;

        // u tiles along travel, v spans the tire, fade lives in the tangent uint
        RecentQuad rq;
        rq.slot         = slot;
        rq.u_a          = u_a;
        rq.u_b          = u_b;
        rq.intensity_a  = intensity_a;
        rq.intensity_b  = intensity_b;
        rq.verts[0]     = RHI_Vertex_PosTexNorTan(trail.edge_left,  Vector2(uv_a, 0.0f), normal, tangent);
        rq.verts[1]     = RHI_Vertex_PosTexNorTan(trail.edge_right, Vector2(uv_a, 1.0f), normal, tangent);
        rq.verts[2]     = RHI_Vertex_PosTexNorTan(bl,               Vector2(uv_b, 0.0f), normal, tangent);
        rq.verts[3]     = RHI_Vertex_PosTexNorTan(br,               Vector2(uv_b, 1.0f), normal, tangent);
        pack_skid_fade(rq.verts[0], fade_a);
        pack_skid_fade(rq.verts[1], fade_a);
        pack_skid_fade(rq.verts[2], fade_b);
        pack_skid_fade(rq.verts[3], fade_b);

        GeometryBuffer::UpdateVertices(rq.verts, offset, 4);
        trail.head_quad = (trail.head_quad + 1) % trail.capacity_quads;

        // keep enough trailing quads to cover the fade distance, plus margin for larger segments
        uint32_t tail_quads = static_cast<uint32_t>(m_fade_distance / m_min_segment_distance) + 3;
        trail.recent.push_back(rq);
        if (trail.recent.size() > tail_quads)
        {
            trail.recent.erase(trail.recent.begin());
        }
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
            pack_skid_fade(rq.verts[0], fade_a);
            pack_skid_fade(rq.verts[1], fade_a);
            pack_skid_fade(rq.verts[2], fade_b);
            pack_skid_fade(rq.verts[3], fade_b);
            uint32_t offset = trail.global_vertex_offset + rq.slot * 4;
            GeometryBuffer::UpdateVertices(rq.verts, offset, 4);
        }
        trail.recent.clear();
    }

    void SkidMarks::CreateMaterial()
    {
        const string texture_path = string(ResourceCache::GetProjectDirectory()) + "materials/skid_marks/stain.png";
        if (!FileSystem::Exists(texture_path))
        {
            const uint32_t size = 256;
            vector<uint8_t> pixels;
            generate_skid_texture_rgba(pixels, size, size);
            FileSystem::CreateDirectory_(FileSystem::GetDirectoryFromFilePath(texture_path));
            ImageImporter::SaveSdrRgba8(texture_path, size, size, pixels.data());
        }

        if (FileSystem::Exists(texture_path))
        {
            m_texture = ResourceCache::Load<RHI_Texture>(texture_path);
        }

        if (m_texture)
        {
            // keep alpha for the stain mask, bc1 would drop it and leave a solid quad
            m_texture->SetFlag(RHI_Texture_Transparent, true);
        }

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
        }
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
        m_max_segments         = node.attribute("max_segments").as_uint(512);
        m_opacity              = node.attribute("opacity").as_float(0.92f);
        m_z_offset             = node.attribute("z_offset").as_float(0.02f);
        m_uv_tiling            = node.attribute("uv_tiling").as_float(1.25f);
        m_fade_distance        = node.attribute("fade_distance").as_float(1.1f);
    }
}
