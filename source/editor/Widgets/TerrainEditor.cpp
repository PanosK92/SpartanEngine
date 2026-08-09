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
#include "TerrainEditor.h"
#include "Viewport.h"
#include "../Editor.h"
#include "World/World.h"
#include "World/Entity.h"
#include "World/Components/Terrain.h"
#include "World/Components/Camera.h"
#include "Input/Input.h"
#include "Core/Timer.h"
#include "Math/Helper.h"
//=========================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan;
using namespace spartan::math;
//============================

TerrainEditor::TerrainEditor(Editor* editor) : Widget(editor)
{
    m_title        = "Terrain Sculpt";
    m_visible      = false;
    m_size_initial = Vector2(360.0f, 520.0f);
    m_brush.target_height = 0.0f;
}

void TerrainEditor::OnTick()
{
    s_sculpt_active = m_visible && m_sculpt_enabled;
    TickSculpting();
}

void TerrainEditor::OnTickVisible()
{
    Terrain* terrain = ResolveTerrain();

    ImGui::TextUnformatted("Target");
    if (terrain && terrain->GetEntity())
    {
        ImGui::Text("Terrain: %s", terrain->GetEntity()->GetObjectName().c_str());
        ImGui::Text(
            "Grid: %u x %u",
            terrain->GetWidth() > 0 ? terrain->GetWidth() : 0,
            terrain->GetHeight() > 0 ? terrain->GetHeight() : 0
        );
        ImGui::Text("Heightfield: %s", terrain->HasHeightfield() ? "ready" : "empty");
    }
    else
    {
        ImGui::TextDisabled("No terrain in world / selection");
    }

    ImGui::Separator();

    if (!terrain)
    {
        ImGui::TextWrapped("Create a Terrain component on an entity, or use Create Flat below.");
    }

    ImGui::InputScalar("Flat resolution", ImGuiDataType_U32, &m_flat_resolution);
    if (m_flat_resolution < 2)
    {
        m_flat_resolution = 2;
    }

    if (terrain)
    {
        uint32_t density = terrain->GetDensity();
        uint32_t scale   = terrain->GetScale();
        uint32_t tiles   = terrain->GetTileCountAxis();
        if (ImGui::InputScalar("Density", ImGuiDataType_U32, &density))
        {
            terrain->SetDensity(max(density, 1u));
        }
        if (ImGui::InputScalar("Scale (m/sample)", ImGuiDataType_U32, &scale))
        {
            terrain->SetScale(max(scale, 1u));
        }
        if (ImGui::InputScalar("Tile grid (NxN)", ImGuiDataType_U32, &tiles))
        {
            terrain->SetTileCountAxis(max(tiles, 1u));
        }
        ImGui::TextDisabled("density/scale/tiles apply on regenerate");
    }

    if (ImGui::Button("Create flat terrain", ImVec2(-1, 0)))
    {
        Entity* entity = nullptr;
        if (terrain)
        {
            entity = terrain->GetEntity();
        }
        else
        {
            entity = World::CreateEntity();
            entity->SetObjectName("Terrain");
            terrain = entity->AddComponent<Terrain>();
        }

        if (terrain)
        {
            terrain->CreateFlat(m_flat_resolution, m_flat_resolution);
        }
    }

    if (terrain && ImGui::Button("Regenerate all", ImVec2(-1, 0)))
    {
        terrain->Regenerate();
        m_heights_dirty = false;
    }

    const int selected_tile = ResolveSelectedTileIndex();
    if (terrain && selected_tile >= 0)
    {
        ImGui::Text("Selected tile: tile_%d", selected_tile + 1);
        if (ImGui::Button("Regenerate selected tile", ImVec2(-1, 0)))
        {
            if (terrain->RegenerateTile(static_cast<uint32_t>(selected_tile)))
            {
                m_heights_dirty = false;
            }
        }
    }
    else
    {
        ImGui::TextDisabled("Select a tile_* child to regenerate one tile");
    }

    if (terrain && ImGui::Button("Rebuild mesh", ImVec2(-1, 0)))
    {
        terrain->RebuildSurface(true);
        m_heights_dirty = false;
    }

    if (terrain && ImGui::Button("Finalize placement data", ImVec2(-1, 0)))
    {
        terrain->RebuildSurface(true);
        m_heights_dirty = false;
    }

    ImGui::Separator();
    ImGui::Checkbox("Enable sculpt", &m_sculpt_enabled);
    ImGui::TextDisabled("LMB paints in the viewport, RMB still looks");

    const char* modes[] = { "Raise", "Lower", "Smooth", "Flatten" };
    int mode_index = static_cast<int>(m_brush.mode);
    if (ImGui::Combo("Brush mode", &mode_index, modes, IM_ARRAYSIZE(modes)))
    {
        m_brush.mode = static_cast<TerrainBrushMode>(mode_index);
    }

    ImGui::DragFloat("Radius", &m_brush.radius, 0.5f, 1.0f, 2000.0f, "%.1f m");
    ImGui::DragFloat("Strength / s", &m_strength_per_second, 0.1f, 0.1f, 200.0f, "%.1f");
    ImGui::DragFloat("Falloff", &m_brush.falloff, 0.01f, 0.0f, 1.0f, "%.2f");
    ImGui::DragFloat("Target height", &m_brush.target_height, 0.1f, -1000.0f, 1000.0f, "%.1f m");

    if (m_heights_dirty)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "Surface dirty - release mouse to rebuild");
    }

    ImGui::Separator();
    ImGui::TextWrapped(
        "Regenerate all resets sculpt from the heightmap or flat grid. "
        "Select a tile_* entity to reset only that region. "
        "Generate from a heightmap in Properties when you have one."
    );
}

Terrain* TerrainEditor::ResolveTerrain() const
{
    if (Camera* camera = World::GetCamera())
    {
        if (Entity* selected = camera->GetSelectedEntity())
        {
            if (Terrain* terrain = selected->GetComponent<Terrain>())
            {
                return terrain;
            }

            // selected tile under a terrain root
            if (Entity* parent = selected->GetParent())
            {
                if (Terrain* terrain = parent->GetComponent<Terrain>())
                {
                    return terrain;
                }
            }
        }
    }

    for (Entity* entity : World::GetEntities())
    {
        if (!entity)
        {
            continue;
        }

        if (Terrain* terrain = entity->GetComponent<Terrain>())
        {
            return terrain;
        }
    }

    return nullptr;
}

int TerrainEditor::ResolveSelectedTileIndex() const
{
    Camera* camera = World::GetCamera();
    if (!camera)
    {
        return -1;
    }

    Entity* selected = camera->GetSelectedEntity();
    if (!selected)
    {
        return -1;
    }

    return Terrain::ParseTileIndex(selected);
}

void TerrainEditor::DrawBrushRing(const Vector3& hit) const
{
    Camera* camera = World::GetCamera();
    if (!camera)
    {
        return;
    }

    ImDrawList* draw_list = ImGui::GetForegroundDrawList();
    const int segments = 48;
    ImU32 color = IM_COL32(80, 180, 255, 200);

    Vector2 prev_screen;
    bool has_prev = false;
    for (int i = 0; i <= segments; i++)
    {
        float angle = (static_cast<float>(i) / static_cast<float>(segments)) * pi_2;
        Vector3 world(
            hit.x + cosf(angle) * m_brush.radius,
            hit.y,
            hit.z + sinf(angle) * m_brush.radius
        );

        Vector2 screen;
        camera->WorldToScreenCoordinates(world, screen);

        const Vector2& vp_pos = Viewport::GetScreenPosition();
        ImVec2 point(vp_pos.x + screen.x, vp_pos.y + screen.y);

        if (has_prev)
        {
            draw_list->AddLine(
                ImVec2(vp_pos.x + prev_screen.x, vp_pos.y + prev_screen.y),
                point,
                color,
                2.0f
            );
        }

        prev_screen = screen;
        has_prev = true;
    }
}

void TerrainEditor::TickSculpting()
{
    if (!m_sculpt_enabled || !m_visible)
    {
        s_sculpt_active = false;
        return;
    }

    Terrain* terrain = ResolveTerrain();
    if (!terrain || !terrain->HasHeightfield())
    {
        return;
    }

    if (!Input::GetMouseIsInViewport())
    {
        if (m_heights_dirty && !Input::GetKey(KeyCode::Click_Left))
        {
            terrain->RebuildSurface(false);
            m_heights_dirty = false;
        }
        return;
    }

    Camera* camera = World::GetCamera();
    if (!camera)
    {
        return;
    }

    const Ray& pick_ray = camera->ComputePickingRay();
    Vector3 hit;
    if (!terrain->Raycast(pick_ray, hit))
    {
        return;
    }

    DrawBrushRing(hit);

    if (Input::GetKey(KeyCode::Click_Left) && !Input::GetKey(KeyCode::Click_Right))
    {
        TerrainBrush stroke = m_brush;
        const float dt = static_cast<float>(Timer::GetDeltaTimeSec());
        stroke.strength = m_strength_per_second * dt;
        terrain->ApplyBrush(hit, stroke);
        m_heights_dirty = true;
        m_rebuild_timer += dt;
        if (m_rebuild_timer >= 0.15f)
        {
            terrain->RebuildSurface(false);
            m_rebuild_timer = 0.0f;
            m_heights_dirty = false;
        }
    }
    else if (m_heights_dirty)
    {
        terrain->RebuildSurface(false);
        m_heights_dirty = false;
        m_rebuild_timer = 0.0f;
    }
}
