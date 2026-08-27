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
#include "FileDialog.h"
#include "../Editor.h"
#include "../imgui/ImGui_Extension.h"
#include "../imgui/ImGui_Properties.h"
#include "world/World.h"
#include "world/Entity.h"
#include "world/WorldHelpers.h"
#include "world/components/Terrain.h"
#include "world/components/Camera.h"
#include "rendering/Material.h"
#include "resource/IResource.h"
#include "resource/ResourceCache.h"
#include "rhi/RHI_Texture.h"
#include "core/ThreadPool.h"
#include "input/Input.h"
#include "core/Timer.h"
#include "math/Helper.h"
//=========================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan;
using namespace spartan::math;
using namespace editor_ui;
//============================

namespace
{
    // 12483 reads as nothing, 12,483 reads as a number
    string format_count(const uint32_t value)
    {
        string digits = to_string(value);
        string result;
        result.reserve(digits.size() + digits.size() / 3);

        const size_t leading = digits.size() % 3;
        for (size_t i = 0; i < digits.size(); i++)
        {
            if (i > 0 && (i - leading) % 3 == 0)
            {
                result += ',';
            }
            result += digits[i];
        }

        return result;
    }

    bool is_ready(RHI_Texture* texture)
    {
        return texture && texture->GetResourceState() == ResourceState::PreparedForGpu;
    }

    // a labelled map preview, the terrain grid runs from -z so the v axis is flipped
    void draw_map(const char* caption, RHI_Texture* texture, const float size, const char* tooltip)
    {
        ImGui::BeginGroup();
        {
            if (is_ready(texture))
            {
                ImGuiSp::image(
                    texture,
                    ImVec2(size, size),
                    Vector2(0.0f, 1.0f),
                    Vector2(1.0f, 0.0f)
                );
            }
            else
            {
                const ImVec2 position = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddRectFilled(
                    position,
                    ImVec2(position.x + size, position.y + size),
                    ImGui::EditorUi::color(ImGui::Style::color_canvas_deep),
                    ImGui::EditorUi::scaled(4.0f)
                );
                ImGui::Dummy(ImVec2(size, size));
            }
            ImGuiSp::tooltip(tooltip);

            layout::caption(caption);
        }
        ImGui::EndGroup();
    }

    // the ranges in a rule carry a huge sentinel for unlimited, author them inside a sane band and
    // let the far end of the band mean unlimited again
    bool band_row(
        const char* label,
        float* value_min,
        float* value_max,
        const float limit_min,
        const float limit_max,
        const char* tooltip,
        const char* format = "%.0f m"
    )
    {
        const float epsilon = (limit_max - limit_min) * 0.002f;
        float shown_min     = ImClamp(*value_min, limit_min, limit_max);
        float shown_max     = ImClamp(*value_max, limit_min, limit_max);

        if (!property_range(label, &shown_min, &shown_max, limit_min, limit_max, tooltip, format))
        {
            return false;
        }

        *value_min = shown_min;
        *value_max = shown_max >= limit_max - epsilon ? 100000.0f : shown_max;

        return true;
    }

    // one row of a layer stack, the content is drawn straight into the draw list so the row height
    // stays exactly one line no matter how much is shown on it
    struct row_layout
    {
        ImVec2 min;
        ImVec2 max;
        float text_y;
        bool clicked;
    };

    row_layout row_begin(const char* id, const bool selected, const float height)
    {
        row_layout row;
        row.clicked = ImGui::Selectable(id, selected, ImGuiSelectableFlags_AllowOverlap, ImVec2(0.0f, height));
        row.min     = ImGui::GetItemRectMin();
        row.max     = ImGui::GetItemRectMax();
        row.text_y  = row.min.y + (height - ImGui::GetTextLineHeight()) * 0.5f;

        return row;
    }

    void row_end(const row_layout& row)
    {
        // the row content is drawn straight into the draw list, so the cursor has to be put back
        // under the selectable, imgui needs an item after a cursor move to grow the parent bounds
        ImGui::SetCursorScreenPos(ImVec2(row.min.x, row.max.y));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
        ImGui::PopStyleVar();
    }

    void row_text(const row_layout& row, const float x, const char* text, const ImVec4& color)
    {
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(x, row.text_y),
            ImGui::EditorUi::color(color),
            text
        );
    }

    void row_thumbnail(const row_layout& row, const float x, const float size, RHI_Texture* texture)
    {
        const ImVec2 top_left(x, row.min.y + (row.max.y - row.min.y - size) * 0.5f);
        const ImVec2 bottom_right(top_left.x + size, top_left.y + size);
        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        if (is_ready(texture))
        {
            draw_list->AddImageRounded(
                reinterpret_cast<ImTextureID>(texture),
                top_left,
                bottom_right,
                ImVec2(0.0f, 0.0f),
                ImVec2(1.0f, 1.0f),
                IM_COL32_WHITE,
                ImGui::EditorUi::scaled(3.0f)
            );
        }
        else
        {
            draw_list->AddRectFilled(
                top_left,
                bottom_right,
                ImGui::EditorUi::color(ImGui::Style::color_canvas_deep),
                ImGui::EditorUi::scaled(3.0f)
            );
        }
    }

    // a small square toggle sitting on top of a row, e for enabled and s for solo
    bool row_toggle(
        const char* id,
        const ImVec2& position,
        const float size,
        const bool active,
        const char* glyph,
        const char* tooltip
    )
    {
        ImGui::SetCursorScreenPos(position);
        ImGui::PushID(id);
        const bool pressed = ImGui::InvisibleButton("##toggle", ImVec2(size, size));
        const bool hovered = ImGui::IsItemHovered();
        ImGuiSp::tooltip(tooltip);
        ImGui::PopID();

        ImVec4 background = active ? ImGui::Style::color_accent_2 : ImGui::Style::color_surface;
        if (hovered)
        {
            background = ImGui::Style::lerp(background, ImGui::Style::color_surface_hover, 0.55f);
        }

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddRectFilled(
            position,
            ImVec2(position.x + size, position.y + size),
            ImGui::EditorUi::color(background),
            ImGui::EditorUi::scaled(4.0f)
        );

        const ImVec2 text_size = ImGui::CalcTextSize(glyph);
        draw_list->AddText(
            ImVec2(position.x + (size - text_size.x) * 0.5f, position.y + (size - text_size.y) * 0.5f),
            ImGui::EditorUi::color(active ? ImGui::Style::color_text : ImGui::Style::color_text_muted),
            glyph
        );

        return pressed;
    }
}

TerrainEditor::TerrainEditor(Editor* editor) : Widget(editor)
{
    m_title               = "Terrain";
    m_visible             = false;
    m_size_initial        = Vector2(460.0f, 760.0f);
    // the header packs three buttons, a status line and the opacity slider, below this it wraps
    m_size_min            = Vector2(380.0f, 300.0f);
    m_toolbar_order       = 7;
    m_toolbar_icon        = static_cast<int>(IconType::Terrain);
    m_brush.target_height = 0.0f;
}

TerrainEditor::~TerrainEditor() = default;

void TerrainEditor::OnTick()
{
    // the base widget pushes this as the window alpha, -1 means leave the style alone
    m_alpha = m_opacity < 1.0f ? m_opacity : k_widget_default_property;

    s_sculpt_active = m_visible && m_sculpt_enabled;
    TickSculpting();

    // ticks even while the window is hidden so a pending edit still lands if it gets closed mid tune
    TickScatter();
}

void TerrainEditor::OnTickVisible()
{
    Terrain* terrain = ResolveTerrain();
    if (!terrain)
    {
        DrawNoTerrain();
        TickBrowse();
        return;
    }

    DrawSummary(terrain);
    DrawActionBar(terrain);

    uint32_t mode = static_cast<uint32_t>(m_mode);
    if (segmented_control("mode", { "Shape", "Ground", "Life", "Analysis" }, &mode))
    {
        m_mode = static_cast<Mode>(mode);
    }

    ImGui::Dummy(ImVec2(0.0f, design::spacing_md));

    // everything below the action bar scrolls, the bar itself never leaves the screen
    ImGui::BeginChild("##mode_content");
    {
        switch (m_mode)
        {
            case Mode::Shape:    DrawShape(terrain);    break;
            case Mode::Ground:   DrawGround(terrain);   break;
            case Mode::Life:     DrawLife(terrain);     break;
            case Mode::Analysis: DrawAnalysis(terrain); break;
            default: break;
        }
    }
    ImGui::EndChild();

    TickBrowse();
}

void TerrainEditor::DrawSummary(Terrain* terrain)
{
    const float thumbnail = ImGui::EditorUi::scaled(56.0f);

    ImGui::BeginGroup();
    {
        RHI_Texture* preview = terrain->GetHeightMapFinal();
        const ImVec2 position = ImGui::GetCursorScreenPos();

        if (is_ready(preview))
        {
            ImGui::GetWindowDrawList()->AddImageRounded(
                reinterpret_cast<ImTextureID>(preview),
                position,
                ImVec2(position.x + thumbnail, position.y + thumbnail),
                ImVec2(0.0f, 1.0f),
                ImVec2(1.0f, 0.0f),
                IM_COL32_WHITE,
                ImGui::EditorUi::scaled(5.0f)
            );
        }
        else
        {
            ImGui::GetWindowDrawList()->AddRectFilled(
                position,
                ImVec2(position.x + thumbnail, position.y + thumbnail),
                ImGui::EditorUi::color(ImGui::Style::color_canvas_deep),
                ImGui::EditorUi::scaled(5.0f)
            );
        }

        ImGui::Dummy(ImVec2(thumbnail, thumbnail));
    }
    ImGui::EndGroup();

    ImGui::SameLine(0.0f, design::spacing_md);

    ImGui::BeginGroup();
    {
        ImGui::PushFont(Editor::font_bold, 0.0f);
        ImGui::TextUnformatted(
            terrain->GetEntity() ? terrain->GetEntity()->GetObjectName().c_str() : "Terrain"
        );
        ImGui::PopFont();

        char line[192];
        snprintf(
            line,
            sizeof(line),
            "%.1f km2   %u x %u samples   %u x %u tiles",
            terrain->GetArea(),
            terrain->GetWidth(),
            terrain->GetHeight(),
            terrain->GetTileCountAxis(),
            terrain->GetTileCountAxis()
        );
        layout::caption(line);

        // props live in the summary because their count is the fastest read on whether a rule works
        uint32_t instances = 0;
        uint32_t layers    = 0;
        for (const TerrainScatterLayer& layer : terrain->GetScatterLayers())
        {
            if (terrain->IsScatterActive(layer))
            {
                layers++;
                instances += layer.instance_count;
            }
        }

        if (terrain->IsGenerating())
        {
            chip("generating", design::warning());
        }
        else if (!terrain->HasHeightfield())
        {
            chip("no surface", ImGui::Style::color_error);
        }
        else
        {
            snprintf(line, sizeof(line), "%s props in %u layers", format_count(instances).c_str(), layers);
            chip(line, design::ok());
        }
    }
    ImGui::EndGroup();

    ImGui::Dummy(ImVec2(0.0f, design::spacing_sm));
    layout::separator();
}

void TerrainEditor::DrawActionBar(Terrain* terrain)
{
    const bool generating = terrain->IsGenerating();
    const bool can_build  = terrain->GetHeightMapSeed() != nullptr ||
                            (terrain->GetWidth() > 1 && terrain->GetHeight() > 1);

    const float spacing = design::spacing_sm;
    const float width   = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;

    ImGui::BeginDisabled(generating || !can_build);
    if (attention_button(generating ? "Building..." : "Generate", m_shape_dirty, ImVec2(width, 0.0f)))
    {
        Rebuild(terrain);
    }
    ImGui::EndDisabled();
    ImGuiSp::tooltip(
        can_build
            ? "rebuild the surface from the height map on a worker thread, this clears sculpt edits "
              "and respawns the props when it lands"
            : "assign a height map in shape first, or create a flat terrain"
    );

    ImGui::SameLine(0.0f, spacing);

    ImGui::BeginDisabled(!terrain->HasHeightfield());
    if (m_sculpt_enabled)
    {
        if (primary_button("Sculpting", ImVec2(width, 0.0f)))
        {
            m_sculpt_enabled = false;
        }
    }
    else if (ImGuiSp::button("Sculpt", ImVec2(width, 0.0f)))
    {
        m_sculpt_enabled = true;
        m_mode           = Mode::Shape;
    }
    ImGui::EndDisabled();
    ImGuiSp::tooltip("paint the surface with the brush in the viewport, left mouse paints and right mouse still looks");

    // one line that says what is out of date, this is the whole reason the bar exists, the opacity
    // control rides along on the right of it so it costs no height of its own
    const float row_width     = ImGui::GetContentRegionAvail().x;
    const float opacity_width = ImGui::EditorUi::scaled(120.0f);

    const bool placing = m_scatter_running->load();

    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        (m_shape_dirty || m_scatter_dirty || placing) ? design::warning() : ImGui::Style::color_text_muted
    );
    if (m_shape_dirty)
    {
        ImGui::TextUnformatted("shape edits pending, generate");
    }
    else if (placing)
    {
        ImGui::TextUnformatted("placing props");
    }
    else if (m_scatter_dirty)
    {
        ImGui::TextUnformatted("rule edits land in a moment");
    }
    else
    {
        ImGui::TextUnformatted("viewport matches what is authored");
    }
    ImGui::PopStyleColor();

    ImGui::SameLine(row_width - opacity_width);
    DrawOpacity(opacity_width);

    ImGui::Dummy(ImVec2(0.0f, design::spacing_sm));
}

void TerrainEditor::DrawOpacity(const float width)
{
    // authoring a terrain means watching the terrain, not the panel in front of it
    int percent = static_cast<int>(m_opacity * 100.0f + 0.5f);

    ImGui::SetNextItemWidth(width);
    if (ImGui::SliderInt("##opacity", &percent, 25, 100, "opacity %d%%", ImGuiSliderFlags_AlwaysClamp))
    {
        m_opacity = static_cast<float>(percent) / 100.0f;
    }
    ImGuiSp::tooltip("fade the whole window so the ground stays readable behind it, right click to go back to full");

    if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
    {
        m_opacity = 1.0f;
    }
}

void TerrainEditor::DrawNoTerrain()
{
    card_begin("No Terrain", "this world has no terrain component yet, start from a flat grid and sculpt, or add the component and assign a height map");
    {
        property_uint(
            "Flat Resolution",
            &m_flat_resolution,
            1.0f,
            2,
            4096,
            "samples per axis of the starting grid, scale turns that into meters"
        );

        if (primary_button("Create Flat Terrain", ImVec2(-1.0f, 0.0f)))
        {
            Entity* entity = World::CreateEntity();
            entity->SetObjectName("Terrain");

            if (Terrain* terrain = entity->AddComponent<Terrain>())
            {
                terrain->CreateFlat(max(m_flat_resolution, 2u), max(m_flat_resolution, 2u));
            }
        }
        ImGuiSp::tooltip("spawns a terrain entity with a flat heightfield at sea level, ready to sculpt");
    }
    card_end();
}

void TerrainEditor::Rebuild(Terrain* terrain)
{
    m_shape_dirty   = false;
    m_scatter_dirty = false;
    m_scatter_timer = 0.0f;
    m_heights_dirty = false;

    ThreadPool::AddTask([terrain]()
    {
        terrain->Regenerate();
    });
}

void TerrainEditor::Rescatter(Terrain* terrain)
{
    m_scatter_dirty = false;
    m_scatter_timer = 0.0f;

    shared_ptr<atomic<bool>> running = m_scatter_running;
    running->store(true);

    ThreadPool::AddTask([terrain, running]()
    {
        WorldHelpers::PopulateTerrainBiomeProps(terrain);
        running->store(false);
    });
}

void TerrainEditor::MarkScatterDirty()
{
    m_scatter_dirty = true;
    m_scatter_timer = 0.0f;
}

void TerrainEditor::TickScatter()
{
    if (!m_scatter_dirty)
    {
        return;
    }

    Terrain* terrain = ResolveTerrain();
    if (!terrain || !terrain->HasHeightfield() || terrain->IsGenerating())
    {
        return;
    }

    // placing every prop layer again walks all the tiles, so it waits for the hand to come off the
    // slider instead of firing mid drag. the timer restarts on every edit, so this measures the quiet
    // since the last one rather than the age of the first, and a whole drag collapses into one rebuild
    const float quiet_period = 0.6f;

    m_scatter_timer += static_cast<float>(Timer::GetDeltaTimeSec());
    if (m_scatter_timer < quiet_period)
    {
        return;
    }

    // one already on a worker would be racing this one over the same tiles, let it land and retry
    if (m_scatter_running->load())
    {
        return;
    }

    Rescatter(terrain);
}

void TerrainEditor::Browse(const function<void(const string&)>& on_selected)
{
    if (!m_dialog)
    {
        m_dialog = make_unique<FileDialog>(true, FileDialog_Type_FileSelection, FileDialog_Op_Load, FileDialog_Filter_All);
    }

    m_dialog_callback = on_selected;
    m_dialog_visible  = true;
}

void TerrainEditor::TickBrowse()
{
    if (!m_dialog_visible || !m_dialog)
    {
        return;
    }

    string selected_path;
    if (m_dialog->Show(&m_dialog_visible, m_editor, nullptr, &selected_path))
    {
        if (m_dialog_callback && !selected_path.empty())
        {
            m_dialog_callback(selected_path);
        }

        m_dialog_visible  = false;
        m_dialog_callback = nullptr;
    }
}

void TerrainEditor::DrawShape(Terrain* terrain)
{
    card_begin("Height Map", "an 8 bit grayscale image, black lands on the low end of the elevation band and white on the high end");
    {
        const float slot = ImGui::EditorUi::scaled(80.0f);

        ImGui::BeginGroup();
        {
            auto setter = [this, terrain](RHI_Texture* texture)
            {
                terrain->SetHeightMapSeed(texture);
                m_shape_dirty = true;
            };

            if (ImGuiSp::image_slot(terrain->GetHeightMapSeed(), setter))
            {
                Browse([this, terrain](const string& path)
                {
                    if (!FileSystem::IsSupportedImageFile(path))
                    {
                        return;
                    }

                    if (RHI_Texture* texture = ResourceCache::Load<RHI_Texture>(path).get())
                    {
                        texture->PrepareForGpu();
                        terrain->SetHeightMapSeed(texture);
                        m_shape_dirty = true;
                    }
                });
            }
            ImGuiSp::tooltip("click to browse, or drag a texture in from the asset viewer");
            layout::caption("source");
        }
        ImGui::EndGroup();

        ImGui::SameLine(0.0f, design::spacing_xl);
        draw_map("baked", terrain->GetHeightMapFinal(), slot, "the heightfield as it came out of the last generate, including erosion and sculpt");
    }
    card_end();

    card_begin("Elevation", "where the image maps to in world space");
    {
        float min_y     = terrain->GetMinY();
        float max_y     = terrain->GetMaxY();
        float sea_level = terrain->GetSeaLevel();

        if (property_range(
            "Height Band",
            &min_y,
            &max_y,
            -1000.0f,
            1000.0f,
            "world y for the darkest and the brightest pixel, drag either handle. zakynthos peaks at about 755 m",
            "%.0f m"
        ))
        {
            terrain->SetMinY(min_y);
            terrain->SetMaxY(max_y);
            m_shape_dirty = true;
        }

        if (fabsf(max_y - min_y) < 0.001f)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, design::warning());
            ImGui::TextUnformatted("the band is flat, widen it before generate");
            ImGui::PopStyleColor();
        }

        if (property_float(
            "Sea Level",
            &sea_level,
            0.1f,
            -1000.0f,
            1000.0f,
            "world y of the ocean, erosion, the border and the island tools all meet the water here",
            "%.1f m"
        ))
        {
            terrain->SetSeaLevel(sea_level);
            m_shape_dirty = true;
        }
    }
    card_end();

    card_begin("Resolution", "how finely the image becomes geometry, all of it applies on generate");
    {
        uint32_t density   = terrain->GetDensity();
        uint32_t scale     = terrain->GetScale();
        uint32_t tiles     = terrain->GetTileCountAxis();
        uint32_t smoothing = terrain->GetSmoothingPasses();
        bool border        = terrain->GetCreateBorder();

        if (property_uint("Density", &density, 1.0f, 1, 16, "extra samples between each height map pixel, 1 is one to one with the image"))
        {
            terrain->SetDensity(max(density, 1u));
            m_shape_dirty = true;
        }
        if (property_uint("Scale", &scale, 1.0f, 1, 1000, "meters between height map samples, 25 fits zakynthos into roughly 36 km"))
        {
            terrain->SetScale(max(scale, 1u));
            m_shape_dirty = true;
        }
        if (property_uint("Tiles", &tiles, 1.0f, 1, 64, "splits the mesh into an n by n grid of tile children for culling and streaming"))
        {
            terrain->SetTileCountAxis(max(tiles, 1u));
            m_shape_dirty = true;
        }
        if (property_uint("Smoothing", &smoothing, 1.0f, 0, 32, "blur passes on the height map before meshing, softens hard dem steps, 0 keeps the raw data"))
        {
            terrain->SetSmoothingPasses(smoothing);
            m_shape_dirty = true;
        }
        if (property_toggle("Sealed Border", &border, "raise the map edges so the player cannot walk off the heightfield, islands want this off"))
        {
            terrain->SetCreateBorder(border);
            m_shape_dirty = true;
        }

        char line[160];
        snprintf(
            line,
            sizeof(line),
            "%s vertices, %s indices in the current mesh",
            format_count(terrain->GetVertexCount()).c_str(),
            format_count(terrain->GetIndexCount()).c_str()
        );
        layout::caption(line);
    }
    card_end();

    card_begin("Shoreline", "bend the map into an island so the ocean clips it cleanly");
    {
        float shore_width = terrain->GetShoreWidth();
        if (property_float(
            "Shore Width",
            &shore_width,
            1.0f,
            1.0f,
            50000.0f,
            "how far inland make island bends the rim down to sea level, wider is a gentler beach",
            "%.0f m"
        ))
        {
            terrain->SetShoreWidth(shore_width);
        }

        const float width = (ImGui::GetContentRegionAvail().x - design::spacing_sm) * 0.5f;

        ImGui::BeginDisabled(!terrain->HasHeightfield());
        if (ImGuiSp::button("Make Island", ImVec2(width, 0.0f)))
        {
            terrain->MakeIslandShore();
            MarkScatterDirty();
        }
        ImGuiSp::tooltip("bend the map borders down to sea level, run generate first");

        ImGui::SameLine(0.0f, design::spacing_sm);

        if (ImGuiSp::button("Lock Shoreline", ImVec2(width, 0.0f)))
        {
            terrain->LockShoreline();
            MarkScatterDirty();
        }
        ImGuiSp::tooltip("raise the real coastline above the waves and cut a beach");
        ImGui::EndDisabled();
    }
    card_end();

    DrawSculpt(terrain);

    card_begin("Start Over", "throw the height map away and begin from a flat grid");
    {
        property_uint("Flat Resolution", &m_flat_resolution, 1.0f, 2, 4096, "samples per axis of the flat grid");

        if (ImGuiSp::button("Create Flat Terrain", ImVec2(-1.0f, 0.0f)))
        {
            terrain->CreateFlat(max(m_flat_resolution, 2u), max(m_flat_resolution, 2u));
            m_shape_dirty   = false;
            m_scatter_dirty = false;
        }
        ImGuiSp::tooltip("replaces the surface with a flat heightfield at sea level, this discards the current one");
    }
    card_end();
}

void TerrainEditor::DrawSculpt(Terrain* terrain)
{
    card_begin("Sculpt", "raise, lower, smooth or flatten by hand, the surface rebuilds as you release");
    {
        if (property_toggle("Brush Active", &m_sculpt_enabled, "while this is on the left mouse button paints instead of selecting"))
        {
            s_sculpt_active = m_visible && m_sculpt_enabled;
        }

        ImGui::BeginDisabled(!m_sculpt_enabled);
        {
            uint32_t mode = static_cast<uint32_t>(m_brush.mode);
            layout::begin_property("Mode", "raise and lower push the ground, smooth averages it, flatten pulls it towards the target height");
            if (segmented_control("brush_mode", { "Raise", "Lower", "Smooth", "Flatten" }, &mode, ImGui::CalcItemWidth()))
            {
                m_brush.mode = static_cast<TerrainBrushMode>(mode);
            }

            property_float("Radius", &m_brush.radius, 0.5f, 1.0f, 2000.0f, "brush footprint on the ground", "%.0f m");
            property_float("Strength", &m_strength_per_second, 0.1f, 0.1f, 200.0f, "meters moved per second of holding the button", "%.1f");
            property_float("Falloff", &m_brush.falloff, 0.01f, 0.0f, 1.0f, "0 is a hard edged stamp, 1 fades all the way to the center", "%.2f");
            property_float("Target Height", &m_brush.target_height, 0.1f, -1000.0f, 1000.0f, "the height flatten pulls towards", "%.0f m");

            const int tile = ResolveSelectedTileIndex();
            if (tile >= 0)
            {
                char label[64];
                snprintf(label, sizeof(label), "Reset tile_%d", tile + 1);
                if (ImGuiSp::button(label, ImVec2(-1.0f, 0.0f)))
                {
                    terrain->RegenerateTile(static_cast<uint32_t>(tile));
                    m_heights_dirty = false;
                }
                ImGuiSp::tooltip("restore this one tile from the pre sculpt baseline");
            }
            else
            {
                layout::caption("select a tile child in the hierarchy to reset one tile on its own");
            }
        }
        ImGui::EndDisabled();

        if (m_heights_dirty)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, design::warning());
            ImGui::TextUnformatted("surface dirty, release the mouse to rebuild");
            ImGui::PopStyleColor();
        }
    }
    card_end();
}

void TerrainEditor::DrawGround(Terrain* terrain)
{
    card_begin("Surface", "global controls over the material blend, these are live, nothing needs rebuilding");
    {
        uint32_t quality = terrain->GetLayerQuality();
        float snow       = terrain->GetSnowAmount();
        float wetness    = terrain->GetWetness();
        float blend      = terrain->GetBlendHeight();

        if (property_uint(
            "Quality",
            &quality,
            1.0f,
            1,
            4,
            "how many of the highest weighted layers get sampled per pixel, 3 is the sweet spot, "
            "2 reads as a two tone surface, 4 costs more than it shows"
        ))
        {
            terrain->SetLayerQuality(quality);
            MarkScatterDirty();
        }

        if (property_float("Snow", &snow, 0.01f, 0.0f, 1.0f, "multiplier on the snow layer, 0 removes snow whatever the altitude", "%.2f"))
        {
            terrain->SetSnowAmount(snow);
            terrain->PushToRenderer();
        }

        if (property_float("Wetness", &wetness, 0.01f, 0.0f, 1.0f, "wetness floor added on top of the flow driven amount, rain and storms drive this", "%.2f"))
        {
            terrain->SetWetness(wetness);
            terrain->PushToRenderer();
        }

        if (property_float(
            "Blend",
            &blend,
            0.01f,
            0.0f,
            2.0f,
            "metres of ground that creep up anything intersecting the surface, so rocks and props sit "
            "bedded in rather than parked on top, 0 turns it off, a material can opt out on its own",
            "%.2f"
        ))
        {
            terrain->SetBlendHeight(blend);
            terrain->PushToRenderer();
        }
    }
    card_end();

    card_begin("Layers", "eight procedural materials, each one claims the ground its rule scores highest on. a layer needs a folder of the same name under project materials");
    {
        const array<TerrainLayerRule, terrain_layer_max>& rules = terrain->GetLayerRules();
        const float row_height = ImGui::EditorUi::scaled(30.0f);
        const float thumbnail  = ImGui::EditorUi::scaled(22.0f);
        const float padding    = design::spacing_sm;

        for (uint32_t i = 0; i < terrain_layer_max; i++)
        {
            const TerrainLayerRule& rule = rules[i];
            const bool enabled           = terrain->IsLayerEnabled(i);

            ImGui::PushID(static_cast<int>(i));

            char id[32];
            snprintf(id, sizeof(id), "##ground_row_%u", i);
            const row_layout row = row_begin(id, m_ground_selected == i, row_height);
            if (row.clicked)
            {
                m_ground_selected = i;
            }

            RHI_Texture* albedo = nullptr;
            if (Material* material = terrain->GetLayerMaterial(i))
            {
                albedo = material->GetTexture(MaterialTextureType::Color);
            }

            float x = row.min.x + padding;
            row_thumbnail(row, x, thumbnail, albedo);
            x += thumbnail + design::spacing_md;

            char index_text[8];
            snprintf(index_text, sizeof(index_text), "%u", i);
            row_text(row, x, index_text, ImGui::Style::color_text_muted);
            x += ImGui::EditorUi::scaled(16.0f);

            row_text(
                row,
                x,
                rule.name.empty() ? "(unused)" : rule.name.c_str(),
                enabled ? ImGui::Style::color_text : ImGui::Style::color_text_muted
            );

            char status[32];
            if (rule.name.empty())
            {
                status[0] = '\0';
            }
            else if (!enabled)
            {
                snprintf(status, sizeof(status), "no textures");
            }
            else
            {
                snprintf(status, sizeof(status), "x%.2f", rule.weight_bias);
            }

            if (status[0] != '\0')
            {
                const ImVec2 text_size = ImGui::CalcTextSize(status);
                row_text(
                    row,
                    row.max.x - text_size.x - padding,
                    status,
                    enabled ? ImGui::Style::color_text_muted : design::warning()
                );
            }

            row_end(row);
            ImGui::PopID();
        }
    }
    card_end();

    DrawGroundLayer(terrain, m_ground_selected);
}

void TerrainEditor::DrawGroundLayer(Terrain* terrain, const uint32_t index)
{
    if (index >= terrain_layer_max)
    {
        return;
    }

    TerrainLayerRule& rule = terrain->GetLayerRules()[index];
    bool changed           = false;

    char title[160];
    snprintf(
        title,
        sizeof(title),
        "Rule  %u  %s",
        index,
        rule.name.empty() ? "(unused)" : rule.name.c_str()
    );

    card_begin(title, "the rule decides where this material wins, the ranges are soft so weight ramps in over the first half of a band and out over the second");
    {
        ImGui::PushID(static_cast<int>(index));

        property_input_text("Name", &rule.name, false, "must match a folder under project materials holding albedo, normal, roughness, occlusion and height png");
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            terrain->RefreshLayers();
            changed = true;
        }

        changed |= property_range("Slope", &rule.slope_min, &rule.slope_max, 0.0f, 90.0f, "the slope band this material covers, 0 is flat ground and 90 is a wall", "%.0f deg");
        changed |= band_row("Altitude", &rule.height_min, &rule.height_max, -2000.0f, 2000.0f, "the altitude band, drag the top handle to the far right for no upper limit");
        changed |= property_float("Priority", &rule.weight_bias, 0.01f, 0.0f, 4.0f, "overall priority against the other layers, 0 switches the layer off", "%.2f");

        layout::group_spacing();
        layout::section_header("Response");
        layout::caption("what the baked analysis pushes this material towards, zero ignores the channel");

        changed |= property_influence("Curvature",  &rule.curvature_influence,  "positive favours concave gullies, negative favours convex ridges");
        changed |= property_influence("Flow",       &rule.flow_influence,       "positive favours water channels");
        changed |= property_influence("Occlusion",  &rule.occlusion_influence,  "positive favours crevices and valley floors");
        changed |= property_influence("Insolation", &rule.insolation_influence, "positive favours sun facing slopes, negative favours shaded ones");
        changed |= property_influence("Wear",       &rule.wear_influence,       "positive favours scoured bedrock");
        changed |= property_influence("Deposition", &rule.deposition_influence, "positive favours accumulated sediment");
        changed |= property_influence("Talus",      &rule.talus_influence,      "positive favours scree fans below cliffs");

        layout::group_spacing();
        layout::section_header("Look");

        changed |= property_float("Tiling",   &rule.tiling_scale,   0.01f, 0.05f, 8.0f, "multiplies the terrain uv, higher is finer texel density", "%.2f");
        changed |= property_float("Blend",    &rule.blend_contrast, 0.01f, 0.01f, 1.0f, "height blend band width, smaller is a sharper interface with the neighbouring material", "%.2f");
        changed |= property_float("Porosity", &rule.porosity,       0.01f, 0.0f,  1.0f, "how much the material darkens when wet, sand is high and rock is low", "%.2f");
        changed |= property_float("Macro",    &rule.macro_strength, 0.01f, 0.0f,  1.0f, "large scale colour breakup amount", "%.2f");

        auto flag_toggle = [&rule, &changed](const char* label, const uint32_t bit, const char* tooltip)
        {
            bool value = (rule.flags & bit) != 0;
            if (property_toggle(label, &value, tooltip))
            {
                rule.flags = value ? (rule.flags | bit) : (rule.flags & ~bit);
                changed    = true;
            }
        };

        flag_toggle("Biplanar",  TerrainLayerFlags_Biplanar, "project on the two dominant axes, this is what stops cliff faces from smearing");
        flag_toggle("Snow",      TerrainLayerFlags_Snow,     "weight comes from the snow accumulation model instead of the slope and altitude bands");
        flag_toggle("Below Sea", TerrainLayerFlags_BelowSea, "the altitude band is measured against sea level rather than absolute world y");

        ImGui::PopID();
    }
    card_end();

    if (changed)
    {
        // the surface reacts this frame, the props only follow on the next scatter
        terrain->PushToRenderer();
        MarkScatterDirty();
    }
}

void TerrainEditor::DrawLife(Terrain* terrain)
{
    array<TerrainScatterLayer, terrain_scatter_max>& layers = terrain->GetScatterLayers();

    card_begin("Props", "eight scatter rules sharing the vocabulary the ground rules use, a slope band and an analysis response mean the same thing here");
    {
        bool spawn = terrain->GetSpawnBiomeProps();
        if (property_toggle("Enabled", &spawn, "the master switch, off clears every prop in the world and switches gpu grass off"))
        {
            terrain->SetSpawnBiomeProps(spawn);
            Rescatter(terrain);
        }

        ImGui::BeginDisabled(!spawn);
        if (ImGuiSp::button("Clear All Props", ImVec2(-1.0f, 0.0f)))
        {
            WorldHelpers::RemoveTerrainProps();
        }
        ImGuiSp::tooltip(
            "delete every tree, rock and flower in the world wherever it sits in the hierarchy, and "
            "switch gpu grass off. rescatter brings them back"
        );
        ImGui::EndDisabled();

        if (terrain->IsScatterSoloed())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, design::warning());
            ImGui::TextUnformatted("a layer is soloed, the others are hidden");
            ImGui::PopStyleColor();

            if (ImGuiSp::button("Clear Solo", ImVec2(-1.0f, 0.0f)))
            {
                for (TerrainScatterLayer& layer : layers)
                {
                    layer.solo = false;
                }
                Rescatter(terrain);
            }
        }
    }
    card_end();

    card_begin("Layers", "e switches a layer on, s hides everything except the soloed layers. the bar under each row is how much of the surface its rule accepted");
    {
        const float row_height = ImGui::EditorUi::scaled(32.0f);
        const float toggle     = ImGui::EditorUi::scaled(18.0f);
        const float padding    = design::spacing_sm;

        for (uint32_t i = 0; i < terrain_scatter_max; i++)
        {
            TerrainScatterLayer& layer = layers[i];
            const bool active          = terrain->IsScatterActive(layer);
            const bool is_grass        = layer.kind == TerrainScatterKind::Grass;
            const bool is_gpu          = layer.kind != TerrainScatterKind::Mesh;

            ImGui::PushID(static_cast<int>(i));

            char id[32];
            snprintf(id, sizeof(id), "##life_row_%u", i);
            const row_layout row = row_begin(id, m_life_selected == i, row_height);
            if (row.clicked)
            {
                m_life_selected = i;
            }

            float x = row.min.x + padding;

            // the two toggles sit on top of the row, they are the fast loop when tuning one rule
            const float toggle_y = row.min.y + (row_height - toggle) * 0.5f;
            if (row_toggle("enabled", ImVec2(x, toggle_y), toggle, layer.enabled, "E", "switch this layer on or off"))
            {
                layer.enabled   = !layer.enabled;
                MarkScatterDirty();
            }
            x += toggle + design::spacing_xs;

            if (row_toggle("solo", ImVec2(x, toggle_y), toggle, layer.solo, "S", "show only this layer, the fastest way to see what one rule does on its own"))
            {
                layer.solo      = !layer.solo;
                MarkScatterDirty();
            }
            x += toggle + design::spacing_md;

            char index_text[8];
            snprintf(index_text, sizeof(index_text), "%u", i);
            row_text(row, x, index_text, ImGui::Style::color_text_muted);
            x += ImGui::EditorUi::scaled(16.0f);

            const char* name = layer.name.empty() ? "(unused)" : layer.name.c_str();
            row_text(row, x, name, active ? ImGui::Style::color_text : ImGui::Style::color_text_muted);
            x += ImGui::CalcTextSize(name).x + design::spacing_md;

            const char* kind_text = is_grass ? "gpu grass" : (is_gpu ? "gpu detail" : "mesh");
            row_text(row, x, kind_text, ImGui::Style::color_text_muted);

            char amount[32];
            if (layer.name.empty())
            {
                amount[0] = '\0';
            }
            else if (is_gpu)
            {
                snprintf(amount, sizeof(amount), "%.0f%% fill", layer.density * 100.0f);
            }
            else
            {
                snprintf(amount, sizeof(amount), "%s", format_count(layer.instance_count).c_str());
            }

            if (amount[0] != '\0')
            {
                const ImVec2 text_size = ImGui::CalcTextSize(amount);
                row_text(row, row.max.x - text_size.x - padding, amount, ImGui::Style::color_text_muted);
            }

            // accepted ground, a rule with a flat bar has gates that are too tight
            if (layer.coverage > 0.0f)
            {
                const float bar_y = row.max.y - ImGui::EditorUi::scaled(3.0f);
                const float bar_w = (row.max.x - row.min.x - padding * 2.0f) * ImClamp(layer.coverage, 0.0f, 1.0f);
                ImGui::GetWindowDrawList()->AddRectFilled(
                    ImVec2(row.min.x + padding, bar_y),
                    ImVec2(row.min.x + padding + bar_w, bar_y + ImGui::EditorUi::scaled(2.0f)),
                    ImGui::EditorUi::color(
                        ImGui::EditorUi::alpha(ImGui::Style::color_accent_1, active ? 0.9f : 0.35f)
                    )
                );
            }

            row_end(row);
            ImGui::PopID();
        }
    }
    card_end();

    DrawLifeLayer(terrain, m_life_selected);
}

void TerrainEditor::DrawLifeLayer(Terrain* terrain, const uint32_t index)
{
    if (index >= terrain_scatter_max)
    {
        return;
    }

    TerrainScatterLayer& layer                             = terrain->GetScatterLayers()[index];
    const array<TerrainLayerRule, terrain_layer_max>& rules = terrain->GetLayerRules();
    const bool is_grass                                    = layer.kind == TerrainScatterKind::Grass;
    const bool is_gpu                                      = layer.kind != TerrainScatterKind::Mesh;
    bool changed                                           = false;

    ImGui::PushID(static_cast<int>(1000 + index));

    card_begin("Asset", "what gets placed, the gpu kinds cost nothing per instance and nothing at all past their reach, but they own no entities so nothing can be clicked or collided with");
    {
        property_input_text("Name", &layer.name, false, "names the layer and the entities it spawns");
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            changed = true;
        }

        uint32_t kind = static_cast<uint32_t>(layer.kind);
        layout::begin_property(
            "Kind",
            "mesh instances become entities under each tile. gpu grass and gpu detail are the same ring "
            "populate pass around the camera, grass bends in the wind, detail is solid, pebbles and chips"
        );
        if (segmented_control("kind", { "Mesh", "GPU Grass", "GPU Detail" }, &kind, ImGui::CalcItemWidth()))
        {
            layer.kind = static_cast<TerrainScatterKind>(kind);
            changed    = true;
        }

        if (property_path("Mesh", layer.mesh_path, "model to scatter, builtin/grass_blade, builtin/flower and builtin/pebble are the generated meshes"))
        {
            Browse([this, terrain, index](const string& path)
            {
                terrain->GetScatterLayers()[index].mesh_path = path;
                MarkScatterDirty();
            });
        }

        property_input_text(
            "Material Folder",
            &layer.material_folder,
            false,
            "optional folder under project materials, when set it replaces whatever materials the model imported"
        );
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            changed = true;
        }
    }
    card_end();

    card_begin("Amount", "density is resolution independent, changing the mesh resolution does not change the prop count");
    {
        if (is_gpu)
        {
            changed |= property_float(
                "Fill",
                &layer.density,
                0.01f,
                0.01f,
                1.0f,
                "how much of the per ring budget gets spent, 1 is the cap. spacing below is the other "
                "half of this, a tighter cell is more instances for the same fill",
                "%.2f"
            );
        }
        else
        {
            changed |= property_float(
                "Density",
                &layer.density,
                0.5f,
                0.0f,
                4000.0f,
                "instances per hectare on ground the rule fully accepts",
                "%.1f /ha"
            );
            changed |= property_uint("Max Per Tile", &layer.max_per_tile, 10.0f, 0, 100000, "hard cap per terrain tile, 0 is uncapped");
        }

        changed |= property_uint("Seed", &layer.seed, 1.0f, 0, 100000, "the same rule arranged differently");
        if (ImGuiSp::button("Reroll", ImVec2(-1.0f, 0.0f)))
        {
            layer.seed++;
            changed = true;
        }
        ImGuiSp::tooltip("bump the seed, same rule and same count, different arrangement");
    }
    card_end();

    card_begin("Where", "the gates a point has to pass, all of them multiply so a soft gate thins the layer out instead of cutting it off");
    {
        changed |= property_range("Slope", &layer.slope_min, &layer.slope_max, 0.0f, 90.0f, "the slope band this layer accepts", "%.0f deg");
        changed |= property_float(
            "Slope Bias",
            &layer.slope_bias,
            0.05f,
            -4.0f,
            4.0f,
            "0 spreads evenly across the band, positive crowds the steep end, negative the flat end",
            "%.2f"
        );
        changed |= band_row("Altitude", &layer.height_min, &layer.height_max, -100.0f, 1500.0f, "metres above sea level, drag the top handle to the far right for no upper limit");
        changed |= property_float("Altitude Fade", &layer.height_fade, 1.0f, 0.0f, 500.0f, "metres of ramp above the low edge, this is what stops a hard line along a shore", "%.0f m");

        uint32_t mask_channel = static_cast<uint32_t>(layer.mask_channel + 1);
        if (property_combo(
            "Biome",
            { "Ignore", "Grass", "Trees", "Rocks" },
            &mask_channel,
            "gate on a channel of the biome mask, the channel value also scales density so a half strength meadow gets half the props"
        ))
        {
            layer.mask_channel = static_cast<int>(mask_channel) - 1;
            changed            = true;
        }

        if (layer.mask_channel >= 0)
        {
            changed |= property_float("Biome Floor", &layer.mask_min, 0.01f, 0.0f, 1.0f, "reject ground below this mask strength", "%.2f");
        }

        if (ImGui::TreeNodeEx("Ground Types", ImGuiTreeNodeFlags_SpanAvailWidth))
        {
            layout::caption("nothing checked means any ground, otherwise the dominant material has to be one of these");

            for (uint32_t rule_index = 0; rule_index < terrain_layer_max; rule_index++)
            {
                const uint32_t bit = 1u << rule_index;
                bool allowed       = (layer.ground_mask & bit) != 0;

                ImGui::PushID(static_cast<int>(rule_index));
                if (property_toggle(
                    rules[rule_index].name.empty() ? "(unused)" : rules[rule_index].name.c_str(),
                    &allowed,
                    "only scatter where this ground material is the dominant one"
                ))
                {
                    layer.ground_mask = allowed ? (layer.ground_mask | bit) : (layer.ground_mask & ~bit);
                    changed           = true;
                }
                ImGui::PopID();
            }

            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Response", ImGuiTreeNodeFlags_SpanAvailWidth))
        {
            layout::caption("what the baked analysis pushes this layer towards, zero ignores the channel");

            changed |= property_influence("Curvature",  &layer.curvature_influence,  "positive favours concave gullies, negative favours convex ridges");
            changed |= property_influence("Flow",       &layer.flow_influence,       "positive favours water channels and damp ground");
            changed |= property_influence("Occlusion",  &layer.occlusion_influence,  "positive favours crevices and valley floors");
            changed |= property_influence("Insolation", &layer.insolation_influence, "positive favours sun facing slopes, negative favours shaded ones");
            changed |= property_influence("Wear",       &layer.wear_influence,       "positive favours scoured bedrock");
            changed |= property_influence("Deposition", &layer.deposition_influence, "positive favours accumulated sediment");
            changed |= property_influence("Talus",      &layer.talus_influence,      "positive favours scree fans below cliffs");

            ImGui::TreePop();
        }
    }
    card_end();

    if (!is_gpu)
    {
        card_begin("Grouping", "nature does not scatter evenly, a radius turns an even spread into patches");
        {
            changed |= property_float("Clump Radius", &layer.clump_radius, 0.5f, 0.0f, 500.0f, "0 scatters evenly instead of in patches", "%.1f m");
            changed |= property_uint("Clump Count", &layer.clump_count, 1.0f, 1, 5000, "instances per patch, 1 is an even scatter");
            changed |= property_float("Raggedness", &layer.clump_raggedness, 0.01f, 0.0f, 1.0f, "0 is a clean circle, 1 is an organic blob edge", "%.2f");
        }
        card_end();
    }
    else
    {
        card_begin("Grouping", "ground cover grows in pockets, and the ring budget the pockets free up is spent inside them, so less coverage is denser cover rather than less of it");
        {
            changed |= property_float("Patch Size", &layer.clump_radius, 0.5f, 0.0f, 500.0f, "pocket scale, 0 turns pockets off and spreads evenly, around 26 m reads as meadows and 8 to 12 m as tufty ground", "%.1f m");
            changed |= property_float("Coverage", &layer.clump_coverage, 0.01f, 0.05f, 1.0f, "share of the eligible ground the pockets take, 1 covers it all evenly, lower packs the same budget into tighter and thicker pockets", "%.2f");
            changed |= property_float("Raggedness", &layer.clump_raggedness, 0.01f, 0.0f, 1.0f, "frays the pocket edge into a fringe and breaks the interior with bare scars", "%.2f");
            changed |= property_toggle("Invert", &layer.clump_invert, "take the ground the pockets left bare instead, for chips and litter that belong between the tufts, it only lines up against a layer using the same patch size");
        }
        card_end();
    }

    // grass sizes itself from the blade mesh, everything else earns its variety here, and for gpu
    // detail this is the whole reason a chip reads as gravel instead of as a repeated prop
    if (!is_grass)
    {
        card_begin("Size", "one asset carries a whole stand when the size comes from the ground it stands on");
        {
            changed |= property_float("Mesh Scale", &layer.mesh_scale, 0.001f, 0.0001f, 1000.0f, "asset unit fix, the sizes below multiply this", "%.4f");
            changed |= property_range("Size", &layer.size_min, &layer.size_max, 0.0f, 8.0f, "the size range, relative to mesh scale", "%.2f");

            if (!is_gpu)
            {
                changed |= property_float("From Slope", &layer.size_from_slope, 0.01f, 0.0f, 1.0f, "steeper ground picks nearer the top of the range instead of rolling at random", "%.2f");
                changed |= property_float("From Altitude", &layer.size_from_altitude, 0.01f, 0.0f, 1.0f, "higher ground picks nearer the top of the range, this is what makes peaks carry the big ones", "%.2f");
                changed |= property_float("Altitude Span", &layer.altitude_span, 1.0f, 16.0f, 2000.0f, "metres of climb over which from altitude reaches the top of the range", "%.0f m");
                changed |= property_float("Giant Chance", &layer.giant_chance, 0.001f, 0.0f, 1.0f, "odds of a landmark sized instance, keep this tiny", "%.3f");
                changed |= property_float("Giant Size", &layer.giant_size, 0.1f, 0.0f, 500.0f, "size of that landmark, 0 falls back to the top of the range", "%.2f");
            }
        }
        card_end();
    }

    card_begin("Seating", "how the prop meets the ground, this is the difference between placed and floating");
    {
        if (!is_gpu)
        {
            changed |= property_float("Align To Normal", &layer.align_to_normal, 0.01f, 0.0f, 1.0f, "0 stands the prop upright, 1 lies it flat on the slope. trunks want 0, boulders want 1", "%.2f");
        }

        changed |= property_float("Surface Offset", &layer.surface_offset, 0.01f, -5.0f, 5.0f, "metres lifted off the ground, go negative to push the instance down into it", "%.2f m");

        if (!is_gpu)
        {
            changed |= property_float("Sink", &layer.sink, 0.01f, 0.0f, 1.0f, "fraction of the final size pushed into the ground, this is what stops a rock floating", "%.2f");
        }

        changed |= property_float(
            "Blend Height",
            &layer.blend_height,
            0.05f,
            0.0f,
            4.0f,
            "trims the band this prop gets, how far the ground washes up it, 0 opts out. "
            "the band itself is already cut from the prop's own size, so 1 is the right answer for most",
            "%.2f"
        );

        changed |= property_float(
            "Blend Sharpness",
            &layer.blend_sharpness,
            0.01f,
            0.0f,
            1.0f,
            "0 washes the ground the whole way up the band, 1 cuts a hard waterline across the middle of it",
            "%.2f"
        );
    }
    card_end();

    card_begin("Rendering", "cost controls, these are the knobs to reach for when a layer is heavy");
    {
        if (!is_gpu)
        {
            changed |= property_float("Render Distance", &layer.render_distance, 10.0f, 0.0f, 20000.0f, "metres, 0 is unlimited and lets the gpu cull decide", "%.0f m");
            changed |= property_float("Shadow Distance", &layer.shadow_distance, 5.0f, 0.0f, 5000.0f, "beyond this the instance stops casting a shadow", "%.0f m");

            auto flag_toggle = [&layer, &changed](const char* label, const uint32_t bit, const char* tooltip)
            {
                bool value = (layer.flags & bit) != 0;
                if (property_toggle(label, &value, tooltip))
                {
                    layer.flags = value ? (layer.flags | bit) : (layer.flags & ~bit);
                    changed     = true;
                }
            };

            flag_toggle("Cast Shadows",    TerrainScatterFlags_CastShadows,    "shadow casting for this layer");
            flag_toggle("Wind",            TerrainScatterFlags_Wind,           "animate the alpha masked parts, leaves and twigs, in the wind");
            flag_toggle("Color Variation", TerrainScatterFlags_ColorVariation, "tint each instance a little differently so a grove does not read as one clone");
            flag_toggle("Collision",       TerrainScatterFlags_Collision,      "convex hull body on the solid parts, leaves stay walk through");
            flag_toggle("Tumble",          TerrainScatterFlags_Tumble,         "fully random rotation, for debris that has come to rest at any angle");
            flag_toggle("Log Size",        TerrainScatterFlags_LogSize,        "sample the size range logarithmically, a wide range reads better with many small and few large");
        }
        else
        {
            for (uint32_t ring = 0; ring < 3; ring++)
            {
                char radius_label[32];
                char cell_label[32];
                snprintf(radius_label, sizeof(radius_label), "Ring %u Reach", ring);
                snprintf(cell_label, sizeof(cell_label), "Ring %u Spacing", ring);

                changed |= property_float(radius_label, &layer.grass_ring_radius[ring], 1.0f, 1.0f, 5000.0f, "how far this ring reaches from the camera, nothing exists past the last one", "%.0f m");
                changed |= property_float(cell_label, &layer.grass_cell_size[ring], 0.01f, 0.05f, 16.0f, "spacing inside the ring, smaller is denser and heavier", "%.2f m");
            }
        }
    }
    card_end();

    card_begin("Result", "what the last scatter actually produced");
    {
        char text[64];
        if (is_gpu)
        {
            property_text("Instances", "decided on the gpu every frame", "a gpu layer has no entities, the populate pass fills the rings each frame");
        }
        else
        {
            snprintf(text, sizeof(text), "%s", format_count(layer.instance_count).c_str());
            property_text("Instances", text, "how many instances the last scatter placed");
        }

        snprintf(text, sizeof(text), "%.1f %%", layer.coverage * 100.0f);
        property_meter("Accepted Ground", layer.coverage, text, "how much of the surface the gates accepted, 0 means they are too tight");
    }
    card_end();

    ImGui::PopID();

    if (changed)
    {
        // a gpu layer owns no entities, its entire state is the params block the populate pass reads
        // every frame, so pushing it now makes the sliders answer immediately. routing it through the
        // rescatter button instead meant every tweak looked like it did nothing
        const bool gpu_now = layer.kind != TerrainScatterKind::Mesh;
        if (gpu_now)
        {
            WorldHelpers::RefreshTerrainGpuScatter(terrain);
        }

        // a mesh layer has to place entities again, and so does one that just stopped being gpu
        if (!gpu_now || gpu_now != is_gpu)
        {
            MarkScatterDirty();
        }
    }
}

void TerrainEditor::DrawAnalysis(Terrain* terrain)
{
    card_begin("Debug View", "paint the surface with what the rule system is actually reading, this is the fastest way to understand why a layer landed where it did");
    {
        static const vector<string> views =
        {
            "Off", "Layer Weights", "Dominant Layer", "Curvature", "Flow",
            "Occlusion", "Insolation", "Deposition", "Wear", "Talus", "Wetness"
        };

        uint32_t view = static_cast<uint32_t>(terrain->GetDebugView());
        if (property_combo("View", views, &view, "layer weights shows the top three picks as red, green and blue, the rest show one analysis channel"))
        {
            terrain->SetDebugView(static_cast<TerrainDebugView>(view));
        }

        if (terrain->GetDebugView() != TerrainDebugView::Off)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, design::warning());
            ImGui::TextUnformatted("a debug view is on, the viewport is not showing final shading");
            ImGui::PopStyleColor();

            if (ImGuiSp::button("Back To Final", ImVec2(-1.0f, 0.0f)))
            {
                terrain->SetDebugView(TerrainDebugView::Off);
            }
        }
    }
    card_end();

    card_begin("Baked Maps", "these come out of generate, without them the rules can only see slope and altitude");
    {
        const float size = ImGui::EditorUi::scaled(112.0f);

        draw_map("height", terrain->GetHeightMapFinal(), size, "the heightfield after smoothing, erosion and sculpt");
        ImGui::SameLine(0.0f, design::spacing_lg);
        draw_map("curvature, flow, occlusion", terrain->GetAnalysisMapA(), size, "red is curvature, green is flow accumulation, blue is sky occlusion, alpha is sediment deposition");

        draw_map("wear, sun, talus", terrain->GetAnalysisMapB(), size, "red is bedrock wear, green is insolation, blue is normalised height, alpha is talus scree");
        ImGui::SameLine(0.0f, design::spacing_lg);
        draw_map("grass, trees, rocks", terrain->GetPropMask(), size, "the biome mask, props only spawn where their channel is bright, alpha carries the dominant ground layer index");

        if (!terrain->GetAnalysisMapA())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, design::warning());
            ImGui::TextUnformatted("nothing baked yet, run generate");
            ImGui::PopStyleColor();
        }
    }
    card_end();

    card_begin("Materials", "the ground layers read their textures off disk, reload after adding or editing a folder");
    {
        if (ImGuiSp::button("Reload Layer Textures", ImVec2(-1.0f, 0.0f)))
        {
            terrain->RefreshLayers();
        }
        ImGuiSp::tooltip(
            "rescan project materials for each layer folder, a layer whose folder is missing is "
            "switched off and its weight goes to the layers that do exist"
        );

        if (ImGuiSp::button("Rebuild Biome Mask", ImVec2(-1.0f, 0.0f)))
        {
            terrain->RebuildPropMask();
        }
        ImGuiSp::tooltip("re-derive the grass, trees and rocks channels from the current ground rules without touching the props");
    }
    card_end();
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
    const int segments    = 48;
    const ImU32 color     = IM_COL32(80, 180, 255, 200);

    Vector2 prev_screen;
    bool has_prev = false;
    for (int i = 0; i <= segments; i++)
    {
        const float angle = (static_cast<float>(i) / static_cast<float>(segments)) * pi_2;
        const Vector3 world(
            hit.x + cosf(angle) * m_brush.radius,
            hit.y,
            hit.z + sinf(angle) * m_brush.radius
        );

        Vector2 screen;
        camera->WorldToScreenCoordinates(world, screen);

        const Vector2& viewport_position = Viewport::GetScreenPosition();
        const ImVec2 point(viewport_position.x + screen.x, viewport_position.y + screen.y);

        if (has_prev)
        {
            draw_list->AddLine(
                ImVec2(viewport_position.x + prev_screen.x, viewport_position.y + prev_screen.y),
                point,
                color,
                2.0f
            );
        }

        prev_screen = screen;
        has_prev    = true;
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
        const float dt      = static_cast<float>(Timer::GetDeltaTimeSec());
        stroke.strength     = m_strength_per_second * dt;
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

        // sculpted ground means the props no longer sit where the rules said they would
        MarkScatterDirty();
    }
}
