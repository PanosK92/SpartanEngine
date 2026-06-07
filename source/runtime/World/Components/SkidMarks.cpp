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
#include "../../Geometry/Mesh.h"
#include "../../Math/Vector2.h"
#include "../../Rendering/Material.h"
#include "../../Rendering/Color.h"
#include "../../Rendering/GeometryBuffer.h"
#include "../../Resource/ResourceCache.h"
#include "../../RHI/RHI_Texture.h"
#include "../../FileSystem/FileSystem.h"
SP_WARNINGS_OFF
#include "../../IO/pugixml.hpp"
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

    SkidMarks::SkidMarks(Entity* entity) : Component(entity)
    {

    }

    SkidMarks::~SkidMarks()
    {

    }

    void SkidMarks::Tick()
    {
        if (!m_initialized)
        {
            EnsureInitialized();
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

            Vector3 normal  = m_physics->GetWheelContactNormal(wheel);
            Vector3 contact = m_physics->GetWheelContactPoint(wheel);

            // the visual wheel hub is far smoother than the convex sweep contact, which jitters laterally on a
            // spinning or locked wheel and is the source of the remaining zigzag, place the mark directly under
            // the hub projected onto the contact plane so it lines up with the tire exactly
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

            // strip width comes straight from the physical tire width defined on the car, never hardcoded here
            float half_width = m_physics->GetWheelWidth(wheel) * 0.5f;
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

            // start (or restart) a strip, no quad is laid until the wheel has moved
            if (!trail.active)
            {
                trail.active        = true;
                trail.has_edge      = false;
                trail.strip_index++;
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

            // first edge of a fresh strip is created at the anchor
            if (!trail.has_edge)
            {
                trail.edge_left  = trail.anchor_center - right * half_width;
                trail.edge_right = trail.anchor_center + right * half_width;
                trail.u_accum    = 0.0f;
                trail.has_edge   = true;
            }

            Vector3 b_left  = center - right * half_width;
            Vector3 b_right = center + right * half_width;
            float u_a       = trail.u_accum * m_uv_tiling;
            float u_b       = (trail.u_accum + d) * m_uv_tiling;

            // map slip into a 0..1 body intensity with a low floor so light skids taper instead of cutting off
            float t    = (slip - m_slip_threshold) / 0.6f;
            t          = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
            float body = 0.2f + 0.8f * t;

            // ramp the intensity up over the start of the strip so it fades in instead of popping
            float fade_a = trail.u_accum / m_fade_distance;
            float fade_b = (trail.u_accum + d) / m_fade_distance;
            fade_a       = fade_a > 1.0f ? 1.0f : fade_a;
            fade_b       = fade_b > 1.0f ? 1.0f : fade_b;

            DepositQuad(trail, b_left, b_right, u_a, u_b, normal, travel, body * fade_a, body * fade_b);

            // advance, the new edge becomes the start of the next quad for seamless continuity
            trail.edge_left     = b_left;
            trail.edge_right    = b_right;
            trail.u_accum      += d;
            trail.anchor_center = center;
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

        Render* renderable = trail.entity->AddComponent<Render>();
        renderable->SetMesh(trail.mesh.get(), 0);
        renderable->SetMaterial(m_material);
        renderable->SetFlag(RenderableFlags::CastsShadows, false);
        renderable->SetFlag(RenderableFlags::ExcludeFromRayTracing, true);

        trail.global_vertex_offset = renderable->GetVertexOffset();
    }

    void SkidMarks::DepositQuad(WheelTrail& trail, const Vector3& bl, const Vector3& br, float u_a, float u_b, const Vector3& normal, const Vector3& tangent, float intensity_a, float intensity_b)
    {
        uint32_t slot   = trail.head_quad % trail.capacity_quads;
        uint32_t offset = trail.global_vertex_offset + slot * 4;

        RecentQuad rq;
        rq.slot     = slot;
        rq.verts[0] = RHI_Vertex_PosTexNorTan(trail.edge_left,  Vector2(u_a, intensity_a), normal, tangent);
        rq.verts[1] = RHI_Vertex_PosTexNorTan(trail.edge_right, Vector2(u_a, intensity_a), normal, tangent);
        rq.verts[2] = RHI_Vertex_PosTexNorTan(bl,               Vector2(u_b, intensity_b), normal, tangent);
        rq.verts[3] = RHI_Vertex_PosTexNorTan(br,               Vector2(u_b, intensity_b), normal, tangent);

        GeometryBuffer::UpdateVertices(rq.verts, offset, 4);
        trail.head_quad = (trail.head_quad + 1) % trail.capacity_quads;

        // keep a short tail history so it can be faded out when the skid stops
        uint32_t tail_quads = static_cast<uint32_t>(m_fade_distance / m_min_segment_distance) + 1;
        trail.recent.push_back(rq);
        if (trail.recent.size() > tail_quads)
        {
            trail.recent.erase(trail.recent.begin());
        }
    }

    void SkidMarks::FadeStripEnd(WheelTrail& trail)
    {
        size_t n = trail.recent.size();
        for (size_t i = 0; i < n; i++)
        {
            // oldest tail quad keeps full intensity, the very last quad fades all the way to zero
            float factor = (n <= 1) ? 0.0f : static_cast<float>(n - 1 - i) / static_cast<float>(n - 1);
            RecentQuad& rq = trail.recent[i];
            for (int v = 0; v < 4; v++)
            {
                Vector2 uv = rq.verts[v].get_uv();
                rq.verts[v].set_uv(uv.x, uv.y * factor);
            }
            uint32_t offset = trail.global_vertex_offset + rq.slot * 4;
            GeometryBuffer::UpdateVertices(rq.verts, offset, 4);
        }
        trail.recent.clear();
    }

    void SkidMarks::CreateMaterial()
    {
        // procedural skid texture, u tiles along travel as streaks, v selects slip intensity
        const uint32_t w = 64;
        const uint32_t h = 64;
        RHI_Texture_Mip mip;
        mip.bytes.resize(w * h * 4);
        for (uint32_t y = 0; y < h; y++)
        {
            float v = static_cast<float>(y) / static_cast<float>(h - 1);
            for (uint32_t x = 0; x < w; x++)
            {
                // cheap per-column streak variation so the mark is not a flat band
                float n      = 0.6f + 0.4f * fabsf(sinf(static_cast<float>(x) * 1.7f) * 0.5f + sinf(static_cast<float>(x) * 0.37f) * 0.5f);
                float alpha  = v * n;
                alpha        = alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha);
                uint8_t a    = static_cast<uint8_t>(alpha * 255.0f);
                uint32_t idx = (y * w + x) * 4;
                mip.bytes[idx + 0] = std::byte{ 12 };
                mip.bytes[idx + 1] = std::byte{ 12 };
                mip.bytes[idx + 2] = std::byte{ 12 };
                mip.bytes[idx + 3] = std::byte{ a };
            }
        }

        RHI_Texture_Slice slice;
        slice.mips.push_back(move(mip));
        vector<RHI_Texture_Slice> data;
        data.push_back(move(slice));

        m_texture = make_shared<RHI_Texture>(
            RHI_Texture_Type::Type2D, w, h, 1, 1, RHI_Format::R8G8B8A8_Unorm, RHI_Texture_Srv, "skidmarks_texture", data);
        m_texture->SetResourceFilePath("skidmarks_color.png");
        m_texture = ResourceCache::Cache<RHI_Texture>(m_texture);

        m_material = make_shared<Material>();
        m_material->SetResourceName("skidmarks" + string(EXTENSION_MATERIAL));
        m_material->SetColor(Color(0.05f, 0.05f, 0.05f, m_opacity));
        m_material->SetProperty(MaterialProperty::Roughness, 0.95f);
        m_material->SetProperty(MaterialProperty::Metalness, 0.0f);
        m_material->SetProperty(MaterialProperty::CullMode, static_cast<float>(RHI_CullMode::None));
        m_material->SetTexture(MaterialTextureType::Color, m_texture);
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
    }

    void SkidMarks::Load(pugi::xml_node& node)
    {
        m_slip_threshold       = node.attribute("slip_threshold").as_float(0.35f);
        m_min_segment_distance = node.attribute("min_segment_distance").as_float(0.15f);
        m_max_segments         = node.attribute("max_segments").as_uint(256);
        m_opacity              = node.attribute("opacity").as_float(0.75f);
        m_z_offset             = node.attribute("z_offset").as_float(0.02f);
        m_uv_tiling            = node.attribute("uv_tiling").as_float(0.5f);
    }
}
