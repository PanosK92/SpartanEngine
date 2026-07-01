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

//= INCLUDES ============================
#include "pch.h"
#include "Water.h"
#include "Render.h"
#include "Camera.h"
#include "../Entity.h"
#include "../World.h"
#include "../../Geometry/Mesh.h"
#include "../../Geometry/GeometryGeneration.h"
#include "../../Rendering/Material.h"
#include "../../Rendering/Renderer.h"
#include "../../FileSystem/FileSystem.h"
SP_WARNINGS_OFF
#include "../../IO/pugixml.hpp"
SP_WARNINGS_ON
//=======================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    Water::Water(Entity* entity) : Component(entity)
    {
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_cascade_count,     uint32_t);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_amplitude,         float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_choppiness,        float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_displacement_scale, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_normal_strength,   float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_sea_level,         float);
    }

    Water::~Water()
    {
        Remove();
    }

    void Water::Initialize()
    {
        BuildSurface();
        PushToRenderer();
    }

    void Water::Tick()
    {
        // keep the clipmap centered under the camera, snapped to the finest cell to avoid swimming
        Camera* camera = World::GetCamera();
        if (!camera || !camera->GetEntity())
        {
            return;
        }

        const Vector3 camera_position = camera->GetEntity()->GetPosition();
        const float snap              = m_clipmap_base_cell;
        const float x                 = floor(camera_position.x / snap) * snap;
        const float z                 = floor(camera_position.z / snap) * snap;
        GetEntity()->SetPosition(Vector3(x, m_sea_level, z));
    }

    void Water::Remove()
    {
        Renderer::DisableOcean();
    }

    void Water::BuildSurface()
    {
        // clipmap mesh, built once in local space, the gpu recenters it on the camera each frame
        m_mesh = make_shared<Mesh>();
        m_mesh->SetObjectName("ocean");
        m_mesh->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessOptimize), false);
        {
            vector<RHI_Vertex_PosTexNorTan> vertices;
            vector<uint32_t> indices;
            geometry_generation::generate_ocean_clipmap(&vertices, &indices, m_clipmap_resolution, m_clipmap_levels, m_clipmap_base_cell);
            m_mesh->AddGeometry(vertices, indices, false);
            m_mesh->CreateGpuBuffers();
        }

        // transparent water material, the fft passes supply displacement and normals
        // a small roughness is essential, a perfectly smooth surface gives a delta specular lobe with no visible sun glint
        // the albedo matches the deep in-scattering tint so the sky-lit body and the refracted depths read as the same water
        m_material = make_shared<Material>();
        m_material->SetResourceName("water_fft" + string(EXTENSION_MATERIAL));
        m_material->SetColor(Color(0.0f, 0.09f, 0.13f, 0.9f));
        m_material->SetProperty(MaterialProperty::Roughness,            0.05f);
        m_material->SetProperty(MaterialProperty::SubsurfaceScattering, 0.3f);
        m_material->SetProperty(MaterialProperty::IsWater,              1.0f);
        m_material->SetProperty(MaterialProperty::Ior,                  Material::EnumToIor(MaterialIor::Water));

        // render the surface through the standard transparent path
        if (Render* render = GetEntity()->AddComponent<Render>())
        {
            render->SetMesh(m_mesh.get());
            render->SetMaterial(m_material);
            render->SetFlag(RenderableFlags::CastsShadows, false);
            render->SetFlag(RenderableFlags::ExcludeFromRayTracing, true);
        }
    }

    void Water::PushToRenderer()
    {
        if (!m_mesh || !m_material)
        {
            return;
        }

        Renderer::OceanParams params;
        params.cascade_count     = m_cascade_count;
        params.cascade_length[0] = m_cascade_length[0];
        params.cascade_length[1] = m_cascade_length[1];
        params.cascade_length[2] = m_cascade_length[2];
        params.cascade_length[3] = m_cascade_length[3];
        params.amplitude         = m_amplitude;
        params.choppiness        = m_choppiness;
        params.displacement_scale = m_displacement_scale;
        params.normal_strength   = m_normal_strength;
        params.sea_level         = m_sea_level;

        Renderer::EnableOcean(m_mesh.get(), m_material.get(), params);
    }

    void Water::Save(pugi::xml_node& node)
    {
        pugi::xml_node water = node.append_child("water");
        water.append_attribute("cascade_count")     = m_cascade_count;
        water.append_attribute("amplitude")         = m_amplitude;
        water.append_attribute("choppiness")        = m_choppiness;
        water.append_attribute("displacement_scale") = m_displacement_scale;
        water.append_attribute("normal_strength")   = m_normal_strength;
        water.append_attribute("sea_level")         = m_sea_level;
    }

    void Water::Load(pugi::xml_node& node)
    {
        pugi::xml_node water = node.child("water");
        if (!water)
        {
            return;
        }

        m_cascade_count      = water.attribute("cascade_count").as_uint(m_cascade_count);
        m_amplitude          = water.attribute("amplitude").as_float(m_amplitude);
        m_choppiness         = water.attribute("choppiness").as_float(m_choppiness);
        m_displacement_scale = water.attribute("displacement_scale").as_float(m_displacement_scale);
        m_normal_strength    = water.attribute("normal_strength").as_float(m_normal_strength);
        m_sea_level          = water.attribute("sea_level").as_float(m_sea_level);

        PushToRenderer();
    }
}
