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
#include "pch.h"
#include "Properties.h"
#include "Window.h"
#include "FileDialog.h"
#include "../ImGui/ImGui_EditorUi.h"
#include "../ImGui/ImGui_Extension.h"
#include "../ImGui/Source/imgui_stdlib.h"
#include "../Widgets/ButtonColorPicker.h"
#include "Core/Engine.h"
#include "World/Entity.h"
#include "Rendering/Material.h"
#include "World/Components/Render.h"
#include "World/Components/Physics.h"
#include "World/Components/Light.h"
#include "World/Components/AudioSource.h"
#include "World/Components/Spline.h"
#include "World/Components/SplineFollower.h"
#include "World/Components/Terrain.h"
#include "World/WorldHelpers.h"
#include "Core/ThreadPool.h"
#include "World/Components/Camera.h"
#include "World/Components/Volume.h"
#include "Rendering/Renderer.h"
#include "Resource/IResource.h"
#include "RHI/RHI_Texture.h"
#include "World/Components/Script.h"
#include "World/Components/ParticleSystem.h"
#include "World/Components/Water.h"
#include "World/Components/SpawnPoint.h"
#include "World/Components/CarReset.h"
#include "World/Components/Text3D.h"
#include "World/Prefab.h"
#include "TerrainEditor.h"
#include "../Editor.h"
//==========================================

//= NAMESPACES =========
using namespace std;
using namespace spartan;
using namespace math;
//======================

namespace
{
    // the material currently pinned to the inspector, if any
    weak_ptr<Material> inspected_material;

    // click-to-browse, the inspector is the only place that needs a file dialog
    namespace file_selection
    {
        unique_ptr<FileDialog> dialog;
        bool visible = false;
        function<void(const string&)> callback;
        Editor* owner = nullptr;

        void initialize(Editor* editor)
        {
            owner = editor;
        }

        void open(const function<void(const string&)>& on_selected)
        {
            if (!dialog)
            {
                dialog = make_unique<FileDialog>(true, FileDialog_Type_FileSelection, FileDialog_Op_Load, FileDialog_Filter_All);
            }

            callback = on_selected;
            visible  = true;
        }

        void tick()
        {
            if (!visible || !owner)
            {
                return;
            }

            string selected_path;
            if (dialog->Show(&visible, owner, nullptr, &selected_path))
            {
                if (callback && !selected_path.empty())
                {
                    callback(selected_path);
                }

                visible  = false;
                callback = nullptr;
            }
        }

        // the "..." button that opens the dialog
        bool browse_button(const char* id)
        {
            ImGui::PushID(id);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
            bool clicked = ImGuiSp::button("...");
            ImGui::PopStyleVar();
            ImGui::PopID();

            return clicked;
        }
    }

    // color pickers
    std::unique_ptr<ButtonColorPicker> color_picker_material;
    std::unique_ptr<ButtonColorPicker> color_picker_light;
    std::unique_ptr<ButtonColorPicker> color_picker_camera;
    std::unique_ptr<ButtonColorPicker> color_picker_particle_start;
    std::unique_ptr<ButtonColorPicker> color_picker_particle_end;

    // context menu state
    string context_menu_id;
    Component* copied_component = nullptr;

    // deferred component removal - storing the id prevents a use-after-free
    // crash when the component is destroyed while its Show* function is still on the stack
    uint64_t pending_removal_id    = 0;
    Entity*  pending_removal_owner = nullptr;

    // component content tracking
    bool component_content_active = false;

    //----------------------------------------------------------
    // design system - consistent spacing, colors, and dimensions
    //----------------------------------------------------------

    namespace design
    {
        // spacing
        constexpr float spacing_xs     = 2.0f;
        constexpr float spacing_sm     = 4.0f;
        constexpr float spacing_md     = 8.0f;
        constexpr float spacing_lg     = 12.0f;
        constexpr float spacing_xl     = 16.0f;
        constexpr float spacing_xxl    = 24.0f;

        // layout
        constexpr float label_width    = 0.38f;  // percentage of available width
        constexpr float row_height     = 26.0f;
        constexpr float section_gap    = 6.0f;

        // component accent colors (subtle, professional)
        inline ImVec4 accent_entity()     { return ImVec4(0.45f, 0.55f, 0.70f, 1.0f); }
        inline ImVec4 accent_light()      { return ImVec4(0.85f, 0.75f, 0.35f, 1.0f); }
        inline ImVec4 accent_camera()     { return ImVec4(0.50f, 0.70f, 0.55f, 1.0f); }
        inline ImVec4 accent_render() { return ImVec4(0.60f, 0.50f, 0.70f, 1.0f); }
        inline ImVec4 accent_material()   { return ImVec4(0.70f, 0.55f, 0.50f, 1.0f); }
        inline ImVec4 accent_physics()    { return ImVec4(0.55f, 0.65f, 0.80f, 1.0f); }
        inline ImVec4 accent_audio()      { return ImVec4(0.70f, 0.45f, 0.55f, 1.0f); }
        inline ImVec4 accent_terrain()    { return ImVec4(0.50f, 0.70f, 0.45f, 1.0f); }
        inline ImVec4 accent_volume()     { return ImVec4(0.55f, 0.55f, 0.75f, 1.0f); }
        inline ImVec4 accent_spline()          { return ImVec4(0.30f, 0.75f, 0.70f, 1.0f); }
        inline ImVec4 accent_spline_follower() { return ImVec4(0.35f, 0.80f, 0.65f, 1.0f); }
        inline ImVec4 accent_script()          { return ImVec4(0.60f, 0.70f, 0.50f, 1.0f); }
        inline ImVec4 accent_particles() { return ImVec4(0.90f, 0.55f, 0.30f, 1.0f); }
        inline ImVec4 accent_water()     { return ImVec4(0.30f, 0.60f, 0.80f, 1.0f); }
        inline ImVec4 accent_text_3d()   { return ImVec4(0.75f, 0.55f, 0.85f, 1.0f); }

        // helper to get dimmed version for backgrounds
        inline ImVec4 dimmed(const ImVec4& color, float factor = 0.15f)
        {
            return ImVec4(color.x * factor, color.y * factor, color.z * factor, 0.4f);
        }
    }

    //----------------------------------------------------------
    // layout helpers - consistent property row rendering
    //----------------------------------------------------------

    namespace layout
    {
        // get label column width
        inline float label_width()
        {
            return ImGui::GetContentRegionAvail().x * design::label_width;
        }

        // get value column width
        inline float value_width()
        {
            return ImGui::GetContentRegionAvail().x * (1.0f - design::label_width) - design::spacing_sm;
        }

        // start a property row with label
        inline void begin_property(const char* label, const char* tooltip = nullptr)
        {
            ImGui::AlignTextToFramePadding();

            ImGui::PushStyleColor(
                ImGuiCol_Text,
                ImGui::Style::color_text_muted
            );
            ImGui::TextUnformatted(label);
            ImGui::PopStyleColor();

            if (tooltip && ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(300.0f);
                ImGui::TextUnformatted(tooltip);
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }

            ImGui::SameLine(label_width());
            ImGui::SetNextItemWidth(value_width());
        }

        // property row without label (for multi-value rows)
        inline void begin_value()
        {
            ImGui::SameLine(label_width());
            ImGui::SetNextItemWidth(value_width());
        }

        // position cursor at value column (alias for begin_value)
        inline void move_to_value_column()
        {
            ImGui::SameLine(label_width());
            ImGui::SetNextItemWidth(value_width());
        }

        // add vertical spacing between groups
        inline void group_spacing()
        {
            ImGui::Dummy(ImVec2(0, design::section_gap));
        }

        // draw a subtle horizontal separator
        inline void separator()
        {
            ImGui::Dummy(ImVec2(0, design::spacing_sm));
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddLine(
                ImVec2(p.x, p.y),
                ImVec2(p.x + ImGui::GetContentRegionAvail().x, p.y),
                ImGui::EditorUi::color(
                    ImGui::Style::color_border
                ),
                1.0f
            );
            ImGui::Dummy(ImVec2(0, design::spacing_md));
        }

        // section header within a component
        inline void section_header(const char* title)
        {
            ImGui::Dummy(ImVec2(0, design::spacing_sm));
            ImGui::PushStyleColor(
                ImGuiCol_Text,
                ImGui::Style::color_text
            );
            ImGui::PushFont(Editor::font_bold, 0.0f);
            ImGui::TextUnformatted(title);
            ImGui::PopFont();
            ImGui::PopStyleColor();
            ImGui::Dummy(ImVec2(0, design::spacing_xs));
        }
    }

    //----------------------------------------------------------
    // selection helpers
    //----------------------------------------------------------

    Entity* get_selected_entity()
    {
        if (Camera* camera = World::GetCamera())
        {
            return camera->GetSelectedEntity();
        }
        return nullptr;
    }

    uint32_t get_selected_entity_count()
    {
        if (Camera* camera = World::GetCamera())
        {
            return camera->GetSelectedEntityCount();
        }
        return 0;
    }

    const std::vector<Entity*>& get_selected_entities()
    {
        static std::vector<Entity*> empty;
        if (Camera* camera = World::GetCamera())
        {
            return camera->GetSelectedEntities();
        }
        return empty;
    }

    //----------------------------------------------------------
    // component context menu
    //----------------------------------------------------------

    void component_context_menu_options(const string& id, Component* component, const bool removable)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(design::spacing_md, design::spacing_md));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(design::spacing_md, design::spacing_sm));

        if (ImGui::BeginPopup(id.c_str()))
        {
            if (removable)
            {
                if (ImGui::MenuItem("Remove Component"))
                {
                    if (Entity* entity = get_selected_entity())
                    {
                        if (component)
                        {
                            // defer the removal so we don't destroy a component
                            // while its Show* function is still on the call stack
                            pending_removal_id    = component->GetObjectId();
                            pending_removal_owner = entity;
                        }
                    }
                }
            }

            if (ImGui::MenuItem("Copy Attributes"))
            {
                copied_component = component;
            }

            ImGui::BeginDisabled(!copied_component || (copied_component && copied_component->GetType() != component->GetType()));
            if (ImGui::MenuItem("Paste Attributes"))
            {
                if (copied_component && copied_component->GetType() == component->GetType())
                {
                    component->SetAttributes(copied_component->GetAttributes());
                }
            }
            ImGui::EndDisabled();

            ImGui::EndPopup();
        }

        ImGui::PopStyleVar(2);
    }

    //----------------------------------------------------------
    // component begin/end - styled component headers and content
    //----------------------------------------------------------

    bool component_begin(const char* name, const ImVec4& accent_color, Component* component_instance, bool options = true, const bool removable = true, bool default_open = false)
    {
        ImGui::PushID(name);

        // header styling
        ImVec4 header_bg = ImGui::Style::lerp(
            ImGui::Style::color_panel,
            accent_color,
            0.12f
        );
        ImVec4 header_hovered = ImGui::Style::lerp(
            ImGui::Style::color_surface_hover,
            accent_color,
            0.18f
        );
        ImVec4 header_active = ImGui::Style::lerp(
            ImGui::Style::color_surface_active,
            accent_color,
            0.20f
        );

        ImGui::PushStyleColor(ImGuiCol_Header, header_bg);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, header_hovered);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, header_active);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(design::spacing_md, design::spacing_md));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

        // draw collapsing header
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_AllowOverlap;
        if (default_open)
        {
            flags |= ImGuiTreeNodeFlags_DefaultOpen;
        }

        ImGui::PushFont(Editor::font_bold, 0.0f);
        const bool is_expanded = ImGuiSp::collapsing_header(name, flags);
        ImGui::PopFont();

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);

        // accent bar on the left of header
        ImVec2 header_min = ImGui::GetItemRectMin();
        ImVec2 header_max = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(header_min.x, header_min.y + 2.0f),
            ImVec2(header_min.x + 3.0f, header_max.y - 2.0f),
            ImGui::ColorConvertFloat4ToU32(accent_color),
            2.0f
        );

        // gear icon for context menu
        if (options)
        {
            // size based on header height
            const float header_height = header_max.y - header_min.y;
            const float v_padding     = 5.0f;
            const float r_padding     = 8.0f;  // small padding from right edge
            const float icon_size     = header_height - v_padding * 2.0f;

            // position: near right edge with small padding
            float icon_x = header_max.x - icon_size - r_padding;
            float icon_y = header_min.y + v_padding;

            ImGui::SetCursorScreenPos(ImVec2(icon_x, icon_y));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(
                ImGuiCol_ButtonHovered,
                ImGui::Style::color_surface_hover
            );
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            if (ImGuiSp::image_button(IconType::Gear, icon_size, false))
            {
                context_menu_id = name;
                ImGui::OpenPopup(context_menu_id.c_str());
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);

            if (component_instance && context_menu_id == name)
            {
                component_context_menu_options(context_menu_id, component_instance, removable);
            }
        }

        // wrap expanded content in styled child region
        if (is_expanded)
        {
            component_content_active = true;

            // content background
            const ImVec4 content_bg = ImGui::Style::lerp(
                ImGui::Style::color_canvas,
                ImGui::Style::color_panel,
                0.35f
            );

            ImGui::PushStyleColor(ImGuiCol_ChildBg, content_bg);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(design::spacing_lg, design::spacing_md));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(design::spacing_sm, design::spacing_sm));
            ImGui::BeginChild(("##content_" + string(name)).c_str(), ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
        }

        return is_expanded;
    }

    void component_end()
    {
        if (component_content_active)
        {
            ImGui::EndChild();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor();
            component_content_active = false;
        }
        ImGui::PopID();
        ImGui::Dummy(ImVec2(0, design::spacing_sm));
    }

    //----------------------------------------------------------
    // custom property widgets
    //----------------------------------------------------------

    // styled combo box
    bool property_combo(const char* label, const std::vector<std::string>& options, uint32_t* index, const char* tooltip = nullptr)
    {
        layout::begin_property(label, tooltip);
        return ImGuiSp::combo_box(("##" + string(label)).c_str(), options, index);
    }

    // styled float input with drag
    bool property_float(const char* label, float* value, float speed = 0.1f, float min = 0.0f, float max = 0.0f, const char* tooltip = nullptr, const char* format = "%.3f")
    {
        layout::begin_property(label, tooltip);
        return ImGuiSp::draw_float_wrap(("##" + string(label)).c_str(), value, speed, min, max, format);
    }

    // styled uint input with drag
    bool property_uint(
        const char* label,
        uint32_t* value,
        float speed = 1.0f,
        uint32_t min = 0,
        uint32_t max = 0,
        const char* tooltip = nullptr
    )
    {
        layout::begin_property(label, tooltip);
        int v = static_cast<int>(*value);
        const bool changed = ImGui::DragInt(
            ("##" + string(label)).c_str(),
            &v,
            speed,
            static_cast<int>(min),
            static_cast<int>(max)
        );
        if (changed)
        {
            *value = static_cast<uint32_t>(v < 0 ? 0 : v);
        }
        return changed;
    }

    // styled toggle switch
    bool property_toggle(const char* label, bool* value, const char* tooltip = nullptr)
    {
        layout::begin_property(label, tooltip);
        return ImGuiSp::toggle_switch(("##" + string(label)).c_str(), value);
    }

    // styled text input (read-only display)
    void property_text(const char* label, const std::string& text, const char* tooltip = nullptr)
    {
        layout::begin_property(label, tooltip);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.9f, 1.0f));
        ImGui::TextUnformatted(text.c_str());
        ImGui::PopStyleColor();
    }

    // styled text input field
    void property_input_text(const char* label, std::string* text, bool readonly = false, const char* tooltip = nullptr)
    {
        layout::begin_property(label, tooltip);
        ImGuiInputTextFlags flags = ImGuiInputTextFlags_AutoSelectAll;
        if (readonly)
        {
            flags |= ImGuiInputTextFlags_ReadOnly;
        }
        ImGui::InputText(("##" + string(label)).c_str(), text, flags);
    }

    // color picker property
    void property_color(const char* label, ButtonColorPicker* picker, const char* tooltip = nullptr)
    {
        layout::begin_property(label, tooltip);
        picker->Update();
    }

    // vector3 property with colored axis indicators - respects label/value columns
    void property_vector3(const char* label, Vector3& vec, const char* tooltip = nullptr)
    {
        ImGui::PushID(label);

        // label in left column
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            ImGui::Style::color_text_muted
        );
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::PopStyleColor();

        if (tooltip && ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(tooltip);
            ImGui::EndTooltip();
        }

        // move to value column
        ImGui::SameLine(layout::label_width());

        // use full remaining width for the 3 inputs
        float total_avail     = ImGui::GetContentRegionAvail().x;
        float axis_label_w    = 10.0f;
        float label_to_input  = 10.0f;  // space between X/Y/Z label and input box
        float between_groups  = 8.0f;   // space between groups
        float input_width     = (total_avail - axis_label_w * 3 - label_to_input * 3 - between_groups * 2) / 3.0f;

        const ImU32 colors[3] =
        {
            ImGui::EditorUi::color(ImGui::EditorUi::axis_color(0)),
            ImGui::EditorUi::color(ImGui::EditorUi::axis_color(1)),
            ImGui::EditorUi::color(ImGui::EditorUi::axis_color(2))
        };
        const char* axis[3] = { "X", "Y", "Z" };
        float* values[3] = { &vec.x, &vec.y, &vec.z };

        for (int i = 0; i < 3; ++i)
        {
            if (i > 0)
            {
                ImGui::SameLine(0, between_groups);
            }

            // axis label with color
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colors[i]));
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(axis[i]);
            ImGui::PopStyleColor();

            // SPACE between label and input
            ImGui::SameLine(0, label_to_input);

            // input field - wide
            ImGui::PushItemWidth(input_width);
            ImGui::PushID(i);
            ImGuiSp::draw_float_wrap("##v", values[i], 0.01f);
            ImGui::PopID();
            ImGui::PopItemWidth();
        }

        ImGui::PopID();
    }

    // transform widget with position, rotation, scale
    void property_transform(Entity* entity)
    {
        Vector3 position    = entity->GetPositionLocal();
        Quaternion rotation = entity->GetRotationLocal();
        Vector3 scale       = entity->GetScaleLocal();

        // per-entity tracking for continuous euler angles
        static std::unordered_map<uintptr_t, Vector3> display_euler_map;
        static std::unordered_map<uintptr_t, Quaternion> last_quat_map;
        uintptr_t entity_id = reinterpret_cast<uintptr_t>(entity);
        rotation.Normalize();

        // get or initialize display euler
        auto euler_it = display_euler_map.find(entity_id);
        auto quat_it = last_quat_map.find(entity_id);

        if (euler_it == display_euler_map.end())
        {
            display_euler_map[entity_id] = rotation.ToEulerAngles();
            last_quat_map[entity_id] = rotation;
        }
        else
        {
            // compute delta rotation from last frame
            Quaternion last_quat = quat_it->second;
            Quaternion delta_quat = rotation * last_quat.Inverse();
            delta_quat.Normalize();

            // convert delta to euler
            Vector3 delta_euler = delta_quat.ToEulerAngles();

            // only apply delta if rotation actually changed
            float dot_val = std::abs(rotation.Dot(last_quat));
            if (dot_val < 0.9999f)
            {
                display_euler_map[entity_id] += delta_euler;
                last_quat_map[entity_id] = rotation;
            }
        }

        Vector3& display_euler = display_euler_map[entity_id];
        Vector3 edit_euler = display_euler;

        // position
        property_vector3("Position", position, "local position in meters");

        // rotation
        property_vector3("Rotation", edit_euler, "local rotation in degrees");

        // scale
        property_vector3("Scale", scale, "local scale multiplier");

        // handle user editing euler angles directly
        if (edit_euler != display_euler)
        {
            display_euler = edit_euler;
            Quaternion new_rotation = Quaternion::FromEulerAngles(display_euler);
            new_rotation.Normalize();
            entity->SetRotationLocal(new_rotation);
            last_quat_map[entity_id] = new_rotation;
        }

        entity->SetPositionLocal(position);
        entity->SetScaleLocal(scale);
    }

    // file/resource selector with browse button
    bool property_resource(const char* label, std::string* name, const char* tooltip, const std::function<void(const std::string&)>& on_browse)
    {
        layout::begin_property(label, tooltip);

        float browse_width = 28.0f;
        float input_width  = layout::value_width() - browse_width - design::spacing_sm;

        ImGui::PushItemWidth(input_width);
        ImGui::InputText(("##" + string(label)).c_str(), name, ImGuiInputTextFlags_ReadOnly);
        ImGui::PopItemWidth();

        ImGui::SameLine(0, design::spacing_sm);

        if (file_selection::browse_button(("browse_" + string(label)).c_str()))
        {
            file_selection::open(on_browse);
            return true;
        }

        return false;
    }
}

Properties::Properties(Editor* editor) : Widget(editor)
{
    m_title          = "Properties";
    m_size_initial.x = 500;

    color_picker_light          = make_unique<ButtonColorPicker>("Light Color Picker");
    color_picker_material      = make_unique<ButtonColorPicker>("Material Color Picker");
    color_picker_camera         = make_unique<ButtonColorPicker>("Camera Color Picker");
    color_picker_particle_start = make_unique<ButtonColorPicker>("Particle Start Color");
    color_picker_particle_end   = make_unique<ButtonColorPicker>("Particle End Color");

    file_selection::initialize(editor);
}

void Properties::OnTickVisible()
{
    bool is_in_game_mode = spartan::Engine::IsFlagSet(spartan::EngineMode::Playing);
    ImGui::BeginDisabled(is_in_game_mode);
    {
        uint32_t selected_count = get_selected_entity_count();

        if (selected_count > 1)
        {
            // multiple entities selected - show summary
            ImGui::Dummy(ImVec2(0, design::spacing_md));

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.85f, 0.4f, 1.0f));
            ImGui::PushFont(Editor::font_bold, 0.0f);
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%d entities selected", selected_count);
            ImGui::TextUnformatted(buf);
            ImGui::PopFont();
            ImGui::PopStyleColor();

            layout::separator();

            // list selected entities
            const auto& selected = get_selected_entities();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
            for (Entity* entity : selected)
            {
                if (entity)
                {
                    ImGui::BulletText("%s", entity->GetObjectName().c_str());
                }
            }
            ImGui::PopStyleColor();

            layout::separator();
            layout::section_header("Transform");

            if (ImGuiSp::button("Snap Selected", ImVec2(-1, 0)))
            {
                Terrain::SnapEntitiesToTerrain(selected);
            }
            ImGuiSp::tooltip("drop onto the first surface below, building or prop, else terrain");

            if (ImGuiSp::button("Snap Selected Flat", ImVec2(-1, 0)))
            {
                Terrain::SnapEntitiesToFlatTerrain(selected);
            }
            ImGuiSp::tooltip("flatten the terrain inside the rectangle enclosing the selection, then drop onto it");
        }
        else if (Entity* entity = get_selected_entity())
        {
            // push entity id so each entity gets its own collapse state for components
            ImGui::PushID(static_cast<int>(entity->GetObjectId()));

            ShowEntity(entity);
            ShowScript(entity->GetComponent<Script>());
            ShowLight(entity->GetComponent<Light>());
            ShowCamera(entity->GetComponent<Camera>());
            ShowTerrain(entity->GetComponent<Terrain>());
            ShowSpline(entity->GetComponent<Spline>());
            ShowSplineFollower(entity->GetComponent<SplineFollower>());
            ShowAudioSource(entity->GetComponent<AudioSource>());
            ShowText3D(entity->GetComponent<Text3D>());

            // re-fetch after ShowSpline since clearing a road mesh removes the render component
            Render* render = entity->GetComponent<Render>();
            Material* material = render ? render->GetMaterial() : nullptr;
            ShowRender(render);
            ShowMaterial(material, render);
            ShowPhysics(entity->GetComponent<Physics>());
            ShowVolume(entity->GetComponent<Volume>());
            ShowParticleSystem(entity->GetComponent<ParticleSystem>());
            ShowWater(entity->GetComponent<Water>());
            ShowSpawnPoint(entity->GetComponent<SpawnPoint>());
            ShowCarReset(entity->GetComponent<CarReset>());

            ShowAddComponentButton();

            ImGui::PopID();

            // process deferred component removal now that all Show* calls are done
            if (pending_removal_owner && pending_removal_id != 0)
            {
                pending_removal_owner->RemoveComponentById(pending_removal_id);
                pending_removal_owner = nullptr;
                pending_removal_id    = 0;
            }
        }
        else if (!inspected_material.expired())
        {
            ShowMaterial(inspected_material.lock().get());
        }
        else
        {
            // empty state
            ImGui::Dummy(ImVec2(0, design::spacing_xxl));
            ImVec2 text_size = ImGui::CalcTextSize("Select an entity to view properties");
            ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - text_size.x) * 0.5f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
            ImGui::TextUnformatted("Select an entity to view properties");
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndDisabled();

    // handle file browser dialog
    file_selection::tick();
}

void Properties::InspectMaterial(const shared_ptr<Material> material)
{
    // clear entity selection so the material is shown instead
    if (Camera* camera = World::GetCamera())
    {
        camera->ClearSelection();
    }

    inspected_material = material;
}

void Properties::ClearMaterialInspection()
{
    // the inspector is the only place a material is edited, so persist before letting go of it
    if (!inspected_material.expired())
    {
        inspected_material.lock()->SaveToFile(inspected_material.lock()->GetResourceFilePath());
    }

    inspected_material.reset();
}

void Properties::ShowEntity(Entity* entity) const
{
    if (component_begin("Transform", design::accent_entity(), nullptr, true, false, true))
    {
        // entity name display
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        ImGui::PushFont(Editor::font_bold, 0.0f);
        ImGui::TextUnformatted(entity->GetObjectName().c_str());
        ImGui::PopFont();
        ImGui::PopStyleColor();

        // prefab indicator
        if (entity->HasPrefabData())
        {
            ImGui::SameLine();

            bool is_code = entity->IsCodePrefab();
            bool is_file = entity->IsFilePrefab();
            const ImVec4 badge_color = is_code
                ? design::accent_render()
                : ImGui::Style::color_ok;
            ImGui::EditorUi::draw_chip(
                is_code ? "code prefab" : "file prefab",
                ImGui::EditorUi::alpha(badge_color, 0.18f),
                badge_color
            );

            layout::group_spacing();

            // prefab type (for code prefabs)
            if (is_code)
            {
                property_text("Prefab Type", entity->GetPrefabType(), "registered code prefab type");
            }

            // prefab file path (for file prefabs)
            if (is_file)
            {
                property_text("Prefab File", entity->GetPrefabFilePath(), "path to the .prefab file");
            }

            // code prefab attributes (read-only)
            if (is_code && !entity->GetPrefabAttributes().empty())
            {
                layout::separator();
                layout::section_header("Prefab Attributes");

                for (const auto& [key, value] : entity->GetPrefabAttributes())
                {
                    property_text(key.c_str(), value, "prefab attribute (read-only)");
                }
            }

            // editing note, the base is rebuilt on load and user additions persist as overrides
            layout::separator();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
            if (is_code)
            {
                ImGui::TextWrapped("Defined in code. Transform edits plus components and children you add are saved as overrides and re-applied on load.");
            }
            else
            {
                ImGui::TextWrapped("Transform edits plus components and children you add are saved as overrides. Use Update Prefab to bake the current hierarchy into the .prefab file.");
            }
            ImGui::PopStyleColor();

            // prefab action buttons
            layout::separator();

            // update prefab (for file prefabs only, code prefabs have no file to write)
            if (is_file)
            {
                float button_width = ImGui::GetContentRegionAvail().x;
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.50f, 0.35f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.60f, 0.40f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.20f, 0.45f, 0.30f, 1.0f));
                if (ImGuiSp::button("Update Prefab", ImVec2(button_width, 0)))
                {
                    if (Prefab::SaveToFile(entity, entity->GetPrefabFilePath()))
                    {
                        // the current hierarchy is now the base, fold overrides back into it
                        entity->MarkPrefabBaseline();
                    }
                }
                ImGui::PopStyleColor(3);
            }

            // detach from prefab
            {
                float button_width = ImGui::GetContentRegionAvail().x;
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.35f, 0.30f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.65f, 0.40f, 0.35f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.50f, 0.30f, 0.25f, 1.0f));
                if (ImGuiSp::button("Detach from Prefab", ImVec2(button_width, 0)))
                {
                    entity->ClearPrefabData();
                }
                ImGui::PopStyleColor(3);
            }
        }

        layout::group_spacing();

        // active toggle
        bool is_active = entity->GetActive();
        if (property_toggle("Active", &is_active, "enable or disable this entity"))
        {
            entity->SetActive(is_active);
        }

        // tags, comma separated labels systems can query (e.g. wheel, wheel_front)
        {
            string tags = entity->GetTagsString();
            property_input_text("Tags", &tags, false, "comma separated labels, e.g. wheel, wheel_front");
            if (tags != entity->GetTagsString())
            {
                entity->SetTagsString(tags);
            }
        }

        layout::separator();

        // position, rotation, scale
        property_transform(entity);

        layout::separator();

        const float snap_button_width = ImGui::GetContentRegionAvail().x;
        if (ImGuiSp::button("Snap", ImVec2(snap_button_width, 0)))
        {
            Terrain::SnapEntityToTerrain(entity);
        }
        ImGuiSp::tooltip("snap mesh entities to the surface below; spline roads drop control points and conform to terrain");

        if (ImGuiSp::button("Snap Flat", ImVec2(snap_button_width, 0)))
        {
            Terrain::SnapEntityToFlatTerrain(entity);
        }
        ImGuiSp::tooltip("flatten the terrain inside the rectangle enclosing this entity and its children, then snap onto it");
    }
    component_end();
}

void Properties::ShowScript(spartan::Script* script) const
{
    if (!script)
    {
        return;
    }

    if (component_begin("Script", design::accent_script(), script))
    {
        // script file path with browse
        property_resource("Script File", &script->file_path, "lua script file", [script](const std::string& path)
        {
            if (FileSystem::IsEngineLuaFile(path))
            {
                script->LoadScriptFile(path);
            }
        });

        // drag-drop support for lua files
        if (auto* payload = ImGuiSp::receive_drag_drop_payload(ImGuiSp::DragPayloadType::Lua))
        {
            if (payload->path[0] != '\0')
            {
                script->LoadScriptFile(payload->path);
            }
        }

        // status
        bool is_loaded = script->script.valid();
        property_text("Status", is_loaded ? "Loaded" : "Not Loaded", "whether the script is loaded and valid");

        if (is_loaded)
        {
            if (ImGui::BeginTable("ScriptProperties", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                for (auto&& [K, V] : script->script)
                {
                    std::string key = K.as<std::string>();

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%s", key.c_str());

                    ImGui::TableSetColumnIndex(1);

                    if (V.is<bool>())
                    {
                        bool value = V.as<bool>();
                        if (ImGui::Checkbox(("##" + key).c_str(), &value))
                        {
                            script->script[K] = value;
                        }
                    }
                    else if (V.is<int>())
                    {
                        int value = V.as<int>();
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        if (ImGui::InputInt(("##" + key).c_str(), &value))
                        {
                            script->script[K] = value;
                        }
                    }
                    else if (V.is<float>() || V.is<double>())
                    {
                        float value = V.as<float>();
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        if (ImGui::InputFloat(("##" + key).c_str(), &value))
                        {
                            script->script[K] = value;
                        }
                    }
                    else if (V.is<std::string>())
                    {
                        std::string value = V.as<std::string>();
                        char buffer[256];
                        strncpy_s(buffer, value.c_str(), sizeof(buffer) - 1);
                        buffer[sizeof(buffer) - 1] = '\0';

                        ImGui::SetNextItemWidth(-FLT_MIN);
                        if (ImGui::InputText(("##" + key).c_str(), buffer, sizeof(buffer)))
                        {
                            script->script[K] = std::string(buffer);
                        }
                    }
                    else if (V.is<sol::table>())
                    {
                        ImGui::TextDisabled("[Table]");
                    }
                    else if (V.is<sol::function>())
                    {
                        ImGui::TextDisabled("[Function]");
                    }
                    else
                    {
                        ImGui::TextDisabled("[Unknown Type]");
                    }
                }

                ImGui::EndTable();
            }
        }

        layout::group_spacing();

        // reload button
        float button_width = 80.0f * spartan::Window::GetDpiScale();
        ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - button_width) * 0.5f + ImGui::GetCursorPosX());
        if (ImGuiSp::button("Reload", ImVec2(button_width, 0)))
        {
            script->LoadScriptFile(script->file_path);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        {
            ImGui::SetTooltip("reload the script file");
        }
    }
    component_end();
}

void Properties::ShowLight(spartan::Light* light) const
{
    if (!light)
    {
        return;
    }

    if (component_begin("Light", design::accent_light(), light))
    {
        //= REFLECT ==========================================================================
        static vector<string> types = { "Directional", "Point", "Spot", "Area" };
        float intensity             = light->GetIntensityPhotometric();
        float temperature_kelvin    = light->GetTemperature();
        float angle                 = light->GetAngle() * math::rad_to_deg * 2.0f;
        bool shadows                = light->GetFlag(spartan::LightFlags::Shadows);
        bool shadows_screen_space   = light->GetFlag(spartan::LightFlags::ShadowsScreenSpace);
        bool volumetric             = light->GetFlag(spartan::LightFlags::Volumetric);
        float range                 = light->GetRange();
        float area_width            = light->GetAreaWidth();
        float area_height           = light->GetAreaHeight();
        color_picker_light->SetColor(light->GetColor());
        //====================================================================================

        // type
        uint32_t selection_index = static_cast<uint32_t>(light->GetLightType());
        if (property_combo("Type", types, &selection_index))
        {
            light->SetLightType(static_cast<LightType>(selection_index));
        }

        const bool is_directional = static_cast<LightType>(selection_index) == LightType::Directional;

        // sky preset for directional lights, sets time of day, intensity and rotation in one click
        if (is_directional)
        {
            static vector<string> sky_presets = { "Dawn", "Day", "Dusk", "Night", "David Lynch", "Custom" };
            uint32_t preset_index             = static_cast<uint32_t>(light->GetPreset());
            if (property_combo("Sky Preset", sky_presets, &preset_index, "applies a full time of day setup, time, sun rotation and intensity, the atmosphere derives the sun color"))
            {
                light->SetPreset(static_cast<LightPreset>(preset_index));
                // refresh local copies so the map-back at the bottom does not revert the new values
                intensity          = light->GetIntensityPhotometric();
                temperature_kelvin = light->GetTemperature();
                color_picker_light->SetColor(light->GetColor());
            }
        }

        layout::separator();
        layout::section_header("Appearance");

        // color and temperature are derived from atmospheric transmittance for directional lights
        if (!is_directional)
        {
            property_color("Color", color_picker_light.get(), "light color");
            property_float("Temperature", &temperature_kelvin, 10.0f, 1000.0f, 40000.0f, "color temperature in kelvin", "%.0f K");
        }

        // intensity
        {
            static vector<string> intensity_types = {
                "Stadium",
                "500W Bulb",
                "150W Bulb",
                "100W Bulb",
                "60W Bulb",
                "25W Bulb",
                "Flashlight",
                "Black Hole",
                "Custom"
            };

            LightIntensityUnit intensity_unit = light->GetIntensityUnit();
            bool is_directional = intensity_unit == LightIntensityUnit::Lux;
            float intensity_max = is_directional ? 200000.0f : 1000000.0f;

            if (!is_directional)
            {
                uint32_t intensity_type_index = static_cast<uint32_t>(light->GetIntensity());
                if (property_combo("Preset", intensity_types, &intensity_type_index, "common light intensity presets"))
                {
                    light->SetIntensity(static_cast<LightIntensity>(intensity_type_index));
                    intensity = light->GetIntensityPhotometric();
                }
            }

            const char* unit_tooltip = "total emitted luminous flux in lumens";
            if (intensity_unit == LightIntensityUnit::Lux)
            {
                unit_tooltip = "top of atmosphere illuminance in lux, the atmosphere derives the ground level color and dimming";
            }
            else if (light->GetLightType() == LightType::Spot)
            {
                unit_tooltip = "total beam luminous flux in lumens, focused by the cone angle";
            }
            else if (light->GetLightType() == LightType::Area)
            {
                unit_tooltip = "total one-sided emitted luminous flux in lumens";
            }

            property_float("Intensity", &intensity, 10.0f, 0.0f, intensity_max, unit_tooltip, is_directional ? "%.0f lux" : "%.0f lm");
        }

        layout::separator();
        layout::section_header("Shadows");

        property_toggle("Enabled", &shadows, "cast shadows from this light");

        if (shadows)
        {
            if (is_directional)
            {
                property_toggle("Screen Space", &shadows_screen_space, "screen space contact shadows");
            }

            property_toggle("Volumetric", &volumetric, "volumetric light scattering");
        }

        // directional-specific options
        if (is_directional)
        {
            layout::separator();
            layout::section_header("Day/Night");

            bool day_night_cycle = light->GetFlag(spartan::LightFlags::DayNightCycle);
            bool real_time_cycle = light->GetFlag(spartan::LightFlags::RealTimeCycle);

            // time of day slider, scrubs the sun from midnight through noon and back
            {
                float time_of_day = World::GetTimeOfDay(day_night_cycle && real_time_cycle);
                char time_label[8];
                snprintf(time_label, sizeof(time_label), "%02d:%02d", static_cast<int>(time_of_day * 24.0f), static_cast<int>(time_of_day * 1440.0f) % 60);
                layout::begin_property("Time of Day", "drag the sun from night to day, the atmosphere derives the matching color and intensity");
                ImGui::BeginDisabled(day_night_cycle && real_time_cycle);
                if (ImGui::SliderFloat("##time_of_day", &time_of_day, 0.0f, 1.0f, time_label))
                {
                    light->SetTimeOfDay(time_of_day);
                }
                ImGui::EndDisabled();
            }

            if (property_toggle("Day/Night Cycle", &day_night_cycle, "automatic sun movement"))
            {
                light->SetFlag(spartan::LightFlags::DayNightCycle, day_night_cycle);
            }

            ImGui::BeginDisabled(!day_night_cycle);
            if (property_toggle("Real Time", &real_time_cycle, "sync with actual time"))
            {
                light->SetFlag(spartan::LightFlags::RealTimeCycle, real_time_cycle);
            }
            ImGui::EndDisabled();

            layout::separator();
            layout::section_header("Weather");

            float cloud_coverage = light->GetCloudCoverage();
            if (property_float(
                "Cloud Coverage",
                &cloud_coverage,
                0.005f,
                0.0f,
                1.0f,
                "how cloudy the day is, 0 = clear sky, 0.9 = default, 1 = overcast",
                "%.2f"))
            {
                light->SetCloudCoverage(cloud_coverage);
            }
        }

        // range (point/spot/area)
        if (light->GetLightType() != LightType::Directional)
        {
            layout::separator();
            layout::section_header("Attenuation");
            property_float("Range", &range, 0.1f, 0.0f, 1000.0f, "cutoff distance in meters. lighting stays inverse-square until this distance, then becomes zero", "%.1f m");

            layout::separator();
            layout::section_header("Performance");
            float draw_distance       = light->GetDrawDistance();
            float shadow_distance     = light->GetShadowDistance();
            float volumetric_distance = light->GetVolumetricDistance();
            if (property_float("Draw Distance",       &draw_distance,       1.0f, 0.0f, 10000.0f, "beyond this distance from the camera the light is fully culled",                  "%.0f m"))
            {
                light->SetDrawDistance(draw_distance);
            }
            if (property_float("Shadow Distance",     &shadow_distance,     1.0f, 0.0f, 10000.0f, "beyond this distance the shadow map stops being rendered for this light",        "%.0f m"))
            {
                light->SetShadowDistance(shadow_distance);
            }
            if (property_float("Volumetric Distance", &volumetric_distance, 1.0f, 0.0f, 10000.0f, "beyond this distance volumetric scattering stops being computed for this light", "%.0f m"))
            {
                light->SetVolumetricDistance(volumetric_distance);
            }
        }

        // spot angle
        if (light->GetLightType() == LightType::Spot)
        {
            property_float("Angle", &angle, 0.5f, 1.0f, 179.0f, "cone angle in degrees", "%.1f°");
        }

        // area dimensions
        if (light->GetLightType() == LightType::Area)
        {
            layout::separator();
            layout::section_header("Dimensions");
            property_float("Width", &area_width, 0.01f, 0.01f, 100.0f, "area light width", "%.2f m");
            property_float("Height", &area_height, 0.01f, 0.01f, 100.0f, "area light height", "%.2f m");
        }

        //= MAP ===================================================================================================
        if (intensity != light->GetIntensityPhotometric())
        {
            light->SetIntensity(intensity);
        }
        if (angle != light->GetAngle() * math::rad_to_deg * 2.0f)
        {
            light->SetAngle(angle * math::deg_to_rad * 0.5f);
        }
        if (range != light->GetRange())
        {
            light->SetRange(range);
        }
        if (area_width != light->GetAreaWidth())
        {
            light->SetAreaWidth(area_width);
        }
        if (area_height != light->GetAreaHeight())
        {
            light->SetAreaHeight(area_height);
        }
        if (!is_directional && color_picker_light->GetColor() != light->GetColor())
        {
            light->SetColor(color_picker_light->GetColor());
        }
        if (!is_directional && temperature_kelvin != light->GetTemperature())
        {
            light->SetTemperature(temperature_kelvin);
        }
        light->SetFlag(spartan::LightFlags::ShadowsScreenSpace, is_directional && shadows_screen_space);
        light->SetFlag(spartan::LightFlags::Volumetric, volumetric);
        light->SetFlag(spartan::LightFlags::Shadows, shadows);
        //=========================================================================================================
    }
    component_end();
}

void Properties::ShowRender(spartan::Render* render) const
{
    if (!render)
    {
        return;
    }

    if (component_begin("Render", design::accent_render(), render))
    {
        //= REFLECT ========================================================================================================
        string& name_mesh                 = const_cast<string&>(render->GetMeshName());
        Material* material                = render->GetMaterial();
        uint32_t instance_count           = render->GetInstanceCount();
        static string name_material_empty = "N/A";
        string& name_material             = material ? const_cast<string&>(material->GetObjectName()) : name_material_empty;
        bool cast_shadows                 = render->HasFlag(RenderFlags::CastsShadows);
        bool is_visible                   = render->IsVisible();
        //==================================================================================================================

        // mesh info
        property_input_text("Mesh", &name_mesh, true);

        // lod information
        int lod_count = render->GetLodCount();
        if (lod_count > 0)
        {
            layout::separator();
            layout::section_header("Level of Detail");

            // styled lod table with visible cells
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(design::spacing_md, design::spacing_sm));
            ImGui::PushStyleColor(ImGuiCol_TableBorderLight, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, ImVec4(0.2f, 0.2f, 0.25f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_TableRowBg, ImVec4(0.12f, 0.12f, 0.14f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImVec4(0.16f, 0.16f, 0.18f, 1.0f));

            if (ImGui::BeginTable("##lod_table", lod_count + 1,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame))
            {
                // header row
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                for (int i = 0; i < lod_count; ++i)
                {
                    char col_name[16];
                    std::snprintf(col_name, sizeof(col_name), "LOD %d", i);
                    ImGui::TableSetupColumn(col_name);
                }

                ImGui::TableHeadersRow();

                // vertices row
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted("Vertices");
                for (int i = 0; i < lod_count; ++i)
                {
                    ImGui::TableSetColumnIndex(i + 1);
                    ImGui::Text("%d", render->GetVertexCount(i));
                }

                // indices row
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted("Indices");
                for (int i = 0; i < lod_count; ++i)
                {
                    ImGui::TableSetColumnIndex(i + 1);
                    ImGui::Text("%d", render->GetIndexCount(i));
                }

                ImGui::EndTable();
            }
            ImGui::PopStyleColor(5);
            ImGui::PopStyleVar();

            if (!render->HasInstancing())
            {
                char lod_buf[32];
                std::snprintf(lod_buf, sizeof(lod_buf), "%u", render->GetLodIndex());
                property_text("Current LOD", lod_buf);
            }
        }

        // instancing
        if (instance_count > 1 || render->HasInstancing())
        {
            layout::separator();
            layout::section_header("Instancing");

            char buf[32];
            std::snprintf(buf, sizeof(buf), "%u", instance_count);
            property_text("Instances", buf);

            if (render->HasInstancing() && ImGui::TreeNode("Instance Transforms"))
            {
                for (uint32_t i = 0; i < render->GetInstanceCount(); ++i)
                {
                    Matrix instance = render->GetInstance(i, true);

                    ImGui::PushID(static_cast<int>(i));

                    Vector3 pos, scale;
                    Quaternion rot;
                    instance.Decompose(scale, rot, pos);
                    Vector3 euler = rot.ToEulerAngles();

                    char instance_name[32];
                    std::snprintf(instance_name, sizeof(instance_name), "Instance %u", i);

                    if (ImGui::TreeNode(instance_name))
                    {
                        if (ImGui::DragFloat3("Position", &pos.x, 0.1f))
                        {
                            instance = Matrix::CreateScale(scale) * Matrix::CreateRotation(rot) * Matrix::CreateTranslation(pos);
                        }
                        if (ImGui::DragFloat3("Rotation", &euler.x, 0.5f))
                        {
                            rot = Quaternion::FromEulerAngles(euler.y, euler.x, euler.z);
                            instance = Matrix::CreateScale(scale) * Matrix::CreateRotation(rot) * Matrix::CreateTranslation(pos);
                        }
                        if (ImGui::DragFloat3("Scale", &scale.x, 0.1f))
                        {
                            instance = Matrix::CreateScale(scale) * Matrix::CreateRotation(rot) * Matrix::CreateTranslation(pos);
                        }
                        ImGui::TreePop();
                    }

                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
        }

        layout::separator();
        layout::section_header("Rendering");

        // draw distance
        float draw_distance = render->GetMaxRenderDistance();
        if (property_float("Draw Distance", &draw_distance, 1.0f, 0.0f, 10000.0f, "maximum render distance", "%.0f m"))
        {
            render->SetMaxRenderDistance(draw_distance);
        }

        // material
        {
            layout::begin_property("Material", "assigned material");

            float button_width = 28.0f;
            float input_width  = layout::value_width() - button_width * 2 - design::spacing_sm * 2;
            const ImVec2 drop_min = ImGui::GetCursorScreenPos();

            ImGui::PushItemWidth(input_width);
            ImGui::InputText("##Material", &name_material, ImGuiInputTextFlags_ReadOnly);
            ImGui::PopItemWidth();

            // browse
            ImGui::SameLine(0, design::spacing_sm);
            if (file_selection::browse_button("browse_Material"))
            {
                file_selection::open([render](const std::string& path) {
                    if (FileSystem::IsEngineMaterialFile(path))
                    {
                        render->SetMaterial(path);
                    }
                });
            }

            // clear - reset to default material
            ImGui::SameLine(0, design::spacing_sm);
            ImGui::PushID("clear_material");
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
            if (ImGuiSp::button("x"))
            {
                render->SetDefaultMaterial();
            }
            ImGui::PopStyleVar();
            ImGui::PopID();
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted("reset to default material");
                ImGui::EndTooltip();
            }

            // drop over the whole value row, not just the text field, and apply on mouse release
            // so imgui's two frame delivery cannot miss the assignment
            const ImVec2 drop_max = ImVec2(drop_min.x + layout::value_width(), drop_min.y + ImGui::GetFrameHeight());
            if (auto payload = ImGuiSp::receive_drag_drop_payload_rect(ImGuiSp::DragPayloadType::Material, drop_min, drop_max, ImGui::GetID("##material_drop")))
            {
                if (payload->path[0] != '\0' && FileSystem::IsEngineMaterialFile(payload->path))
                {
                    render->SetMaterial(payload->path);
                }
            }
        }

        layout::group_spacing();

        property_toggle("Cast Shadows", &cast_shadows, "whether this object casts shadows");
        property_text("Visible", is_visible ? "Yes" : "No", "current visibility state");

        //= MAP =========================================================
        render->SetFlag(RenderFlags::CastsShadows, cast_shadows);
        //===============================================================
    }
    component_end();
}

void Properties::ShowPhysics(Physics* body) const
{
    if (!body)
    {
        return;
    }

    if (component_begin("Physics", design::accent_physics(), body))
    {
        // reflect
        float mass             = body->GetMass();
        float friction         = body->GetFriction();
        float friction_rolling = body->GetFrictionRolling();
        float restitution      = body->GetRestitution();
        bool freeze_pos_x      = static_cast<bool>(body->GetPositionLock().x);
        bool freeze_pos_y      = static_cast<bool>(body->GetPositionLock().y);
        bool freeze_pos_z      = static_cast<bool>(body->GetPositionLock().z);
        bool freeze_rot_x      = static_cast<bool>(body->GetRotationLock().x);
        bool freeze_rot_y      = static_cast<bool>(body->GetRotationLock().y);
        bool freeze_rot_z      = static_cast<bool>(body->GetRotationLock().z);
        Vector3 center_of_mass = body->GetCenterOfMass();
        bool is_static         = body->IsStatic();
        bool is_kinematic      = body->IsKinematic();

        // body type
        static vector<string> body_types = {
            "Box", "Sphere", "Plane", "Capsule",
            "Mesh", "Mesh (Convex)", "Controller", "Vehicle", "Cloth", "Heightfield", "Unset"
        };

        uint32_t body_type_index = static_cast<uint32_t>(body->GetBodyType());
        if (property_combo("Type", body_types, &body_type_index, "body type"))
        {
            body->SetBodyType(static_cast<BodyType>(body_type_index));
        }

        layout::separator();
        layout::section_header("Physical Properties");

        property_float("Mass", &mass, 0.1f, 0.0f, 10000.0f, "mass in kilograms", "%.2f kg");
        property_float("Friction", &friction, 0.01f, 0.0f, 1.0f, "surface friction coefficient", "%.3f");
        property_float("Rolling Friction", &friction_rolling, 0.01f, 0.0f, 1.0f, "rolling friction coefficient", "%.3f");
        property_float("Restitution", &restitution, 0.01f, 0.0f, 1.0f, "bounciness", "%.3f");

        layout::separator();
        layout::section_header("Constraints");

        // freeze position with axis toggles
        {
            layout::begin_property("Freeze Position", "lock position on specific axes");

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.4f, 0.4f, 1.0f));
            ImGui::TextUnformatted("X");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGuiSp::toggle_switch("##freeze_pos_x", &freeze_pos_x);

            ImGui::SameLine(0, design::spacing_md);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 0.4f, 1.0f));
            ImGui::TextUnformatted("Y");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGuiSp::toggle_switch("##freeze_pos_y", &freeze_pos_y);

            ImGui::SameLine(0, design::spacing_md);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.6f, 0.9f, 1.0f));
            ImGui::TextUnformatted("Z");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGuiSp::toggle_switch("##freeze_pos_z", &freeze_pos_z);
        }

        // freeze rotation with axis toggles
        {
            layout::begin_property("Freeze Rotation", "lock rotation on specific axes");

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.4f, 0.4f, 1.0f));
            ImGui::TextUnformatted("X");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGuiSp::toggle_switch("##freeze_rot_x", &freeze_rot_x);

            ImGui::SameLine(0, design::spacing_md);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 0.4f, 1.0f));
            ImGui::TextUnformatted("Y");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGuiSp::toggle_switch("##freeze_rot_y", &freeze_rot_y);

            ImGui::SameLine(0, design::spacing_md);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.6f, 0.9f, 1.0f));
            ImGui::TextUnformatted("Z");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGuiSp::toggle_switch("##freeze_rot_z", &freeze_rot_z);
        }

        layout::separator();
        layout::section_header("Body Type");

        property_toggle("Static", &is_static, "immovable object");
        property_toggle("Kinematic", &is_kinematic, "script-controlled movement");

        layout::separator();
        layout::section_header("Center of Mass");

        property_vector3("Offset", center_of_mass, "center of mass offset");

        // cloth-specific properties
        if (body->GetBodyType() == BodyType::Cloth)
        {
            layout::separator();
            layout::section_header("Cloth Simulation");

            float cloth_stiffness  = body->GetClothStiffness();
            float cloth_damping    = body->GetClothDamping();
            float cloth_iterations = static_cast<float>(body->GetClothIterations());
            Vector3 cloth_pin_direction = body->GetClothPinDirection();

            property_float("Stiffness", &cloth_stiffness, 0.01f, 0.0f, 1.0f, "constraint stiffness per iteration", "%.2f");
            property_float("Damping", &cloth_damping, 0.001f, 0.0f, 1.0f, "velocity damping factor", "%.3f");
            property_float("Iterations", &cloth_iterations, 1.0f, 1.0f, 32.0f, "constraint solver iterations per step", "%.0f");
            property_vector3("Pin Direction", cloth_pin_direction, "direction toward the fixed edge");

            bool cloth_wind = body->GetClothWindEnabled();
            if (property_toggle("Wind", &cloth_wind, "allow wind to affect cloth simulation"))
            {
                body->SetClothWindEnabled(cloth_wind);
            }

            if (cloth_stiffness != body->GetClothStiffness())
            {
                body->SetClothStiffness(cloth_stiffness);
            }
            if (cloth_damping != body->GetClothDamping())
            {
                body->SetClothDamping(cloth_damping);
            }
            if (static_cast<uint32_t>(cloth_iterations) != body->GetClothIterations())
            {
                body->SetClothIterations(static_cast<uint32_t>(cloth_iterations));
            }
            if (cloth_pin_direction != body->GetClothPinDirection())
            {
                body->SetClothPinDirection(cloth_pin_direction);
            }
        }

        // map values back
        if (mass != body->GetMass())
        {
            body->SetMass(mass);
        }
        if (friction != body->GetFriction())
        {
            body->SetFriction(friction);
        }
        if (friction_rolling != body->GetFrictionRolling())
        {
            body->SetFrictionRolling(friction_rolling);
        }
        if (restitution != body->GetRestitution())
        {
            body->SetRestitution(restitution);
        }

        if (freeze_pos_x != static_cast<bool>(body->GetPositionLock().x) ||
            freeze_pos_y != static_cast<bool>(body->GetPositionLock().y) ||
            freeze_pos_z != static_cast<bool>(body->GetPositionLock().z))
        {
            body->SetPositionLock(Vector3(static_cast<float>(freeze_pos_x), static_cast<float>(freeze_pos_y), static_cast<float>(freeze_pos_z)));
        }

        if (freeze_rot_x != static_cast<bool>(body->GetRotationLock().x) ||
            freeze_rot_y != static_cast<bool>(body->GetRotationLock().y) ||
            freeze_rot_z != static_cast<bool>(body->GetRotationLock().z))
        {
            body->SetRotationLock(Vector3(static_cast<float>(freeze_rot_x), static_cast<float>(freeze_rot_y), static_cast<float>(freeze_rot_z)));
        }

        if (center_of_mass != body->GetCenterOfMass())
        {
            body->SetCenterOfMass(center_of_mass);
        }
        if (is_static != body->IsStatic())
        {
            body->SetStatic(is_static);
        }
        if (is_kinematic != body->IsKinematic())
        {
            body->SetKinematic(is_kinematic);
        }
    }
    component_end();
}

void Properties::ShowMaterial(Material* material, Render* render) const
{
    if (!material)
    {
        return;
    }

    const bool default_open = render == nullptr;
    if (component_begin("Material", design::accent_material(), nullptr, false, true, default_open))
    {
        // with a render component uv edits go to its override, standalone they modify the material defaults
        const bool uv_per_render = render != nullptr;

        //= REFLECT ================================================
        math::Vector2 tiling = uv_per_render
            ? Vector2(render->ResolveUvTilingX(), render->ResolveUvTilingY())
            : Vector2(material->GetProperty(MaterialProperty::TextureTilingX), material->GetProperty(MaterialProperty::TextureTilingY));

        math::Vector2 offset = uv_per_render
            ? Vector2(render->ResolveUvOffsetX(), render->ResolveUvOffsetY())
            : Vector2(material->GetProperty(MaterialProperty::TextureOffsetX), material->GetProperty(MaterialProperty::TextureOffsetY));

        color_picker_material->SetColor(Color(
            material->GetProperty(MaterialProperty::ColorR),
            material->GetProperty(MaterialProperty::ColorG),
            material->GetProperty(MaterialProperty::ColorB),
            material->GetProperty(MaterialProperty::ColorA)
        ));
        //==========================================================

        // material name
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        ImGui::PushFont(Editor::font_bold, 0.0f);
        ImGui::TextUnformatted(material->GetObjectName().c_str());
        ImGui::PopFont();
        ImGui::PopStyleColor();

        layout::separator();
        layout::section_header("Presets");

        auto refresh_material_color_picker = [&]()
        {
            tiling = uv_per_render
                ? Vector2(render->ResolveUvTilingX(), render->ResolveUvTilingY())
                : Vector2(material->GetProperty(MaterialProperty::TextureTilingX), material->GetProperty(MaterialProperty::TextureTilingY));

            offset = uv_per_render
                ? Vector2(render->ResolveUvOffsetX(), render->ResolveUvOffsetY())
                : Vector2(material->GetProperty(MaterialProperty::TextureOffsetX), material->GetProperty(MaterialProperty::TextureOffsetY));

            color_picker_material->SetColor(Color(
                material->GetProperty(MaterialProperty::ColorR),
                material->GetProperty(MaterialProperty::ColorG),
                material->GetProperty(MaterialProperty::ColorB),
                material->GetProperty(MaterialProperty::ColorA)
            ));
        };

        {
            static vector<string> paint_presets =
            {
                "Select...",
                "Gloss Solid",
                "Metallic",
                "Satin",
                "Matte",
                "Pearl",
                "Candy",
                "Chameleon"
            };

            uint32_t paint_preset_index = static_cast<uint32_t>(material->GetProperty(MaterialProperty::PaintPreset));
            if (paint_preset_index >= paint_presets.size())
            {
                paint_preset_index = 0;
            }

            if (property_combo("Paint", paint_presets, &paint_preset_index, "apply an automotive paint preset"))
            {
                if (paint_preset_index > 0)
                {
                    material->ApplyPaintPreset(
                        static_cast<MaterialPaintPreset>(paint_preset_index - 1),
                        color_picker_material->GetColor()
                    );
                    refresh_material_color_picker();
                }
                else
                {
                    material->SetProperty(MaterialProperty::PaintPreset, 0.0f);
                }
            }
        }

        {
            static vector<string> surface_presets =
            {
                "Select...",
                "Glass Clear",
                "Glass Tinted",
                "Headlight Lens",
                "Taillight Lens",
                "Rubber Tire",
                "Carbon Fiber",
                "Chrome",
                "Polished Metal",
                "Brake Disc",
                "Leather",
                "Black Plastic",
                "Emissive Red Light",
                "Emissive White Light"
            };

            uint32_t surface_preset_index = static_cast<uint32_t>(material->GetProperty(MaterialProperty::SurfacePreset));
            if (surface_preset_index >= surface_presets.size())
            {
                surface_preset_index = 0;
            }

            if (property_combo("Surface", surface_presets, &surface_preset_index, "apply a reusable surface preset"))
            {
                if (surface_preset_index > 0)
                {
                    material->ApplySurfacePreset(static_cast<MaterialSurfacePreset>(surface_preset_index - 1));
                    refresh_material_color_picker();
                }
                else
                {
                    material->SetProperty(MaterialProperty::SurfacePreset, 0.0f);
                }
            }
        }

        layout::separator();
        layout::section_header("Surface");

        // texture slot helper lambda
        const auto show_property = [this, &material](const char* name, const char* tooltip, const MaterialTextureType mat_tex, const MaterialProperty mat_property)
        {
            bool show_texture  = mat_tex      != MaterialTextureType::Max;
            bool show_modifier = mat_property != MaterialProperty::Max;

            ImGui::PushID(name);

            // property label
            if (name)
            {
                layout::begin_property(name, tooltip);
            }

            // texture slot
            if (show_texture)
            {
                for (uint32_t slot = 0; slot < material->GetUsedSlotCount(); ++slot)
                {
                    MaterialTextureType texture_type = static_cast<MaterialTextureType>(mat_tex);

                    auto setter = [material, texture_type, slot](spartan::RHI_Texture* texture) {
                        material->SetTexture(texture_type, texture, slot);
                    };

                    if (slot > 0)
                    {
                        ImGui::SameLine();
                    }

                    // push unique id for each slot to avoid id collisions in image_slot
                    ImGui::PushID(static_cast<int>(slot));
                    spartan::RHI_Texture* texture = material->GetTexture(texture_type, slot);
                    if (ImGuiSp::image_slot(texture, setter))
                    {
                        file_selection::open([setter](const std::string& path) {
                            if (FileSystem::IsSupportedImageFile(path))
                            {
                                if (const auto tex = ResourceCache::Load<RHI_Texture>(path).get())
                                {
                                    // load only produces a cpu texture, prepare it for the gpu so the slot and material can display it
                                    tex->PrepareForGpu();
                                    setter(tex);
                                }
                            }
                        });
                    }
                    ImGui::PopID();
                }

                if (show_modifier)
                {
                    ImGui::SameLine();
                }
            }

            // modifier/multiplier
            if (show_modifier)
            {
                // constrain width to available space
                float available_width = ImGui::GetContentRegionAvail().x;
                float slider_width    = ImMin(available_width, 120.0f);

                if (mat_property == MaterialProperty::ColorA)
                {
                    color_picker_material->Update();
                }
                else if (mat_property == MaterialProperty::Metalness)
                {
                    bool is_metallic = material->GetProperty(mat_property) != 0.0f;
                    if (ImGuiSp::toggle_switch("##metalness", &is_metallic))
                    {
                        material->SetProperty(mat_property, is_metallic ? 1.0f : 0.0f);
                    }
                }
                else
                {
                    ImGui::PushItemWidth(slider_width);
                    float value = material->GetProperty(mat_property);
                    if (ImGuiSp::draw_float_wrap("##val", &value, 0.004f, 0.0f, 1.0f))
                    {
                        material->SetProperty(mat_property, value);
                    }
                    ImGui::PopItemWidth();
                }
            }

            ImGui::PopID();
        };

        // properties with textures
        show_property("Color",                "Surface color",                                                                     MaterialTextureType::Color,     MaterialProperty::ColorA);
        show_property("Roughness",            "Specifies microfacet roughness of the surface for diffuse and specular reflection", MaterialTextureType::Roughness, MaterialProperty::Roughness);
        show_property("Metalness",            "Blends between a non-metallic and metallic material model",                         MaterialTextureType::Metalness, MaterialProperty::Metalness);
        show_property("Normal",               "Controls the normals of the base layers",                                           MaterialTextureType::Normal,    MaterialProperty::Normal);
        show_property("Height",               "Perceived depth for parallax mapping",                                              MaterialTextureType::Height,    MaterialProperty::Height);
        show_property("Occlusion",            "Amount of light loss, can be complementary to SSAO",                                MaterialTextureType::Occlusion, MaterialProperty::Max);
        show_property("Emission",             "Light emission from the surface, works nice with bloom",                            MaterialTextureType::Emission,  MaterialProperty::Max);
        show_property("Alpha mask",           "Discards pixels",                                                                   MaterialTextureType::AlphaMask, MaterialProperty::Max);
        show_property("Clearcoat",            "Extra white specular layer on top of others",                                       MaterialTextureType::Max,       MaterialProperty::Clearcoat);
        show_property("Clearcoat roughness",  "Roughness of clearcoat specular",                                                   MaterialTextureType::Max,       MaterialProperty::Clearcoat_Roughness);
        show_property("Anisotropic",          "Amount of anisotropy for specular reflection",                                      MaterialTextureType::Max,       MaterialProperty::Anisotropic);
        show_property("Anisotropic rotation", "Rotates the direction of anisotropy, with 1.0 going full circle",                   MaterialTextureType::Max,       MaterialProperty::AnisotropicRotation);
        show_property("Sheen",                "Amount of soft velvet like reflection near edges",                                  MaterialTextureType::Max,       MaterialProperty::Sheen);
        show_property("Subsurface scattering","Amount of translucency",                                                            MaterialTextureType::Max,       MaterialProperty::SubsurfaceScattering);

        layout::separator();
        layout::section_header("Automotive");

        float flake_strength = material->GetProperty(MaterialProperty::FlakeStrength);
        if (property_float("Flake strength", &flake_strength, 0.004f, 0.0f, 1.0f, "procedural metallic sparkle strength", "%.3f"))
        {
            material->SetProperty(MaterialProperty::FlakeStrength, flake_strength);
        }

        float flake_scale = material->GetProperty(MaterialProperty::FlakeScale);
        if (property_float("Flake scale", &flake_scale, 1.0f, 1.0f, 512.0f, "procedural metallic sparkle density", "%.0f"))
        {
            material->SetProperty(MaterialProperty::FlakeScale, flake_scale);
        }

        float pearl_strength = material->GetProperty(MaterialProperty::PearlStrength);
        if (property_float("Pearl strength", &pearl_strength, 0.004f, 0.0f, 1.0f, "view angle color shift strength", "%.3f"))
        {
            material->SetProperty(MaterialProperty::PearlStrength, pearl_strength);
        }

        {
            float pearl_color[3] =
            {
                material->GetProperty(MaterialProperty::PearlColorR),
                material->GetProperty(MaterialProperty::PearlColorG),
                material->GetProperty(MaterialProperty::PearlColorB)
            };

            layout::begin_property("Pearl color", "view angle color shift tint");
            if (ImGui::ColorEdit3("##pearl_color", pearl_color, ImGuiColorEditFlags_NoInputs))
            {
                material->SetProperty(MaterialProperty::PearlColorR, pearl_color[0]);
                material->SetProperty(MaterialProperty::PearlColorG, pearl_color[1]);
                material->SetProperty(MaterialProperty::PearlColorB, pearl_color[2]);
            }
        }

        float coat_tint_strength = material->GetProperty(MaterialProperty::CoatTintStrength);
        if (property_float("Coat tint", &coat_tint_strength, 0.004f, 0.0f, 1.0f, "colored clearcoat and candy absorption strength", "%.3f"))
        {
            material->SetProperty(MaterialProperty::CoatTintStrength, coat_tint_strength);
        }

        {
            float coat_tint[3] =
            {
                material->GetProperty(MaterialProperty::CoatTintR),
                material->GetProperty(MaterialProperty::CoatTintG),
                material->GetProperty(MaterialProperty::CoatTintB)
            };

            layout::begin_property("Coat color", "colored clearcoat and candy absorption tint");
            if (ImGui::ColorEdit3("##coat_color", coat_tint, ImGuiColorEditFlags_NoInputs))
            {
                material->SetProperty(MaterialProperty::CoatTintR, coat_tint[0]);
                material->SetProperty(MaterialProperty::CoatTintG, coat_tint[1]);
                material->SetProperty(MaterialProperty::CoatTintB, coat_tint[2]);
            }
        }

        float ior = material->GetProperty(MaterialProperty::Ior);
        if (property_float("IOR", &ior, 0.01f, 1.0f, 2.6f, "index of refraction for transparent materials", "%.2f"))
        {
            material->SetProperty(MaterialProperty::Ior, ior);
        }

        float absorption = material->GetProperty(MaterialProperty::Absorption);
        if (property_float("Absorption", &absorption, 0.01f, 0.0f, 8.0f, "beer lambert dye density for glass, independent of alpha", "%.2f"))
        {
            material->SetProperty(MaterialProperty::Absorption, absorption);
        }

        float thickness = material->GetProperty(MaterialProperty::Thickness);
        if (property_float("Thickness", &thickness, 0.001f, 0.0f, 0.1f, "glass shell thickness in meters for parallax and optical path", "%.3f m"))
        {
            material->SetProperty(MaterialProperty::Thickness, thickness);
        }

        layout::separator();
        layout::section_header("UV Mapping");

        // tiling
        {
            layout::begin_property("Tiling", "texture repeat");

            float w = (layout::value_width() - design::spacing_md - 24.0f) * 0.5f;

            ImGui::PushItemWidth(w);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.5f, 0.5f, 1.0f));
            ImGui::TextUnformatted("X");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::InputFloat("##tileX", &tiling.x, 0.0f, 0.0f, "%.2f");
            ImGui::PopItemWidth();

            ImGui::SameLine();
            ImGui::PushItemWidth(w);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.9f, 0.5f, 1.0f));
            ImGui::TextUnformatted("Y");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::InputFloat("##tileY", &tiling.y, 0.0f, 0.0f, "%.2f");
            ImGui::PopItemWidth();
        }

        // offset
        {
            layout::begin_property("Offset", "texture offset");

            float w = (layout::value_width() - design::spacing_md - 24.0f) * 0.5f;

            ImGui::PushItemWidth(w);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.5f, 0.5f, 1.0f));
            ImGui::TextUnformatted("X");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::InputFloat("##offsetX", &offset.x, 0.0f, 0.0f, "%.2f");
            ImGui::PopItemWidth();

            ImGui::SameLine();
            ImGui::PushItemWidth(w);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.9f, 0.5f, 1.0f));
            ImGui::TextUnformatted("Y");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::InputFloat("##offsetY", &offset.y, 0.0f, 0.0f, "%.2f");
            ImGui::PopItemWidth();
        }

        // inversion
        bool invert_x = uv_per_render
            ? render->ResolveUvInvertX() > 0.5f
            : material->GetProperty(MaterialProperty::TextureInvertX) > 0.5f;
        bool invert_y = uv_per_render
            ? render->ResolveUvInvertY() > 0.5f
            : material->GetProperty(MaterialProperty::TextureInvertY) > 0.5f;
        {
            layout::begin_property("Invert", "flip texture axes");

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.5f, 0.5f, 1.0f));
            ImGui::TextUnformatted("X");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGuiSp::toggle_switch("##invertX", &invert_x);

            ImGui::SameLine(0, design::spacing_md);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.9f, 0.5f, 1.0f));
            ImGui::TextUnformatted("Y");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGuiSp::toggle_switch("##invertY", &invert_y);
        }

        // rotation
        static vector<string> rotation_options = { "0", "90", "180", "270" };
        const float rotation_source = uv_per_render
            ? render->ResolveUvRotation()
            : material->GetProperty(MaterialProperty::TextureRotation);
        uint32_t rotation_index = static_cast<uint32_t>(rotation_source);
        if (property_combo("Rotation", rotation_options, &rotation_index, "rotate texture in 90 degree increments"))
        {
            if (uv_per_render)
            {
                render->GetMaterialOverrideMutable().uv_rotation = static_cast<float>(rotation_index);
            }
            else
            {
                material->SetProperty(MaterialProperty::TextureRotation, static_cast<float>(rotation_index));
            }
        }

        layout::separator();
        layout::section_header("Rendering Options");

        // cull mode
        static vector<string> cull_modes = { "Back", "Front", "None" };
        uint32_t cull_mode_index = static_cast<uint32_t>(material->GetProperty(MaterialProperty::CullMode));
        if (property_combo("Culling", cull_modes, &cull_mode_index, "face culling mode"))
        {
            material->SetProperty(MaterialProperty::CullMode, static_cast<float>(cull_mode_index));
        }

        // feature toggles
        bool tessellation = material->GetProperty(MaterialProperty::Tessellation) != 0.0f;
        if (property_toggle("Tessellation", &tessellation, "hardware tessellation"))
        {
            material->SetProperty(MaterialProperty::Tessellation, tessellation ? 1.0f : 0.0f);
        }

        bool wind_animation = material->GetProperty(MaterialProperty::WindAnimation) != 0.0f;
        if (property_toggle("Wind Animation", &wind_animation, "vertex animation from wind"))
        {
            material->SetProperty(MaterialProperty::WindAnimation, wind_animation ? 1.0f : 0.0f);
        }

        bool motion_blur_radial = material->GetProperty(MaterialProperty::MotionBlurRadial) != 0.0f;
        if (property_toggle("Radial Motion Blur", &motion_blur_radial, "rotational blur for spinning wheels"))
        {
            material->SetProperty(MaterialProperty::MotionBlurRadial, motion_blur_radial ? 1.0f : 0.0f);
        }

        bool emissive_from_albedo = material->GetProperty(MaterialProperty::EmissiveFromAlbedo) != 0.0f;
        if (property_toggle("Emissive from Albedo", &emissive_from_albedo, "use albedo as emission"))
        {
            material->SetProperty(MaterialProperty::EmissiveFromAlbedo, emissive_from_albedo ? 1.0f : 0.0f);
        }

        bool world_space_uv = uv_per_render
            ? render->ResolveUvWorldSpace() != 0.0f
            : material->GetProperty(MaterialProperty::WorldSpaceUv) != 0.0f;
        if (property_toggle("World Space UV", &world_space_uv, "world-space texture coordinates"))
        {
            if (uv_per_render)
            {
                render->GetMaterialOverrideMutable().uv_world_space = world_space_uv ? 1.0f : 0.0f;
            }
            else
            {
                material->SetProperty(MaterialProperty::WorldSpaceUv, world_space_uv ? 1.0f : 0.0f);
            }
        }

        //= MAP ===============================================================================
        // an edit lights up only the fields that changed, untouched ones keep inheriting through the nan sentinel
        auto write_override = [](float& target, float new_value, float resolved_value)
        {
            if (new_value != resolved_value)
            {
                target = new_value;
            }
        };
        if (uv_per_render)
        {
            MaterialOverride& ovr = render->GetMaterialOverrideMutable();
            write_override(ovr.uv_tiling_x, tiling.x,                    render->ResolveUvTilingX());
            write_override(ovr.uv_tiling_y, tiling.y,                    render->ResolveUvTilingY());
            write_override(ovr.uv_offset_x, offset.x,                    render->ResolveUvOffsetX());
            write_override(ovr.uv_offset_y, offset.y,                    render->ResolveUvOffsetY());
            write_override(ovr.uv_invert_x, invert_x ? 1.0f : 0.0f,      render->ResolveUvInvertX());
            write_override(ovr.uv_invert_y, invert_y ? 1.0f : 0.0f,      render->ResolveUvInvertY());
        }
        else
        {
            material->SetProperty(MaterialProperty::TextureTilingX, tiling.x);
            material->SetProperty(MaterialProperty::TextureTilingY, tiling.y);
            material->SetProperty(MaterialProperty::TextureOffsetX, offset.x);
            material->SetProperty(MaterialProperty::TextureOffsetY, offset.y);
            material->SetProperty(MaterialProperty::TextureInvertX, invert_x ? 1.0f : 0.0f);
            material->SetProperty(MaterialProperty::TextureInvertY, invert_y ? 1.0f : 0.0f);
        }
        material->SetProperty(MaterialProperty::ColorR, color_picker_material->GetColor().r);
        material->SetProperty(MaterialProperty::ColorG, color_picker_material->GetColor().g);
        material->SetProperty(MaterialProperty::ColorB, color_picker_material->GetColor().b);
        material->SetProperty(MaterialProperty::ColorA, color_picker_material->GetColor().a);
        //=====================================================================================
    }

    component_end();
}

void Properties::ShowCamera(Camera* camera) const
{
    if (!camera)
    {
        return;
    }

    if (component_begin("Camera", design::accent_camera(), camera))
    {
        //= REFLECT ======================================================================
        static vector<string> projection_types = { "Perspective", "Orthographic" };
        static vector<string> camera_presets =
        {
            "Custom",
            "Daylight",
            "Overcast",
            "Golden Hour",
            "Interior",
            "Night",
            "Cinematic"
        };
        static vector<string> exposure_modes   = { "Manual", "Automatic" };
        float aperture                         = camera->GetAperture();
        float shutter_speed                    = camera->GetShutterSpeed();
        float iso                              = camera->GetIso();
        float adaptation_speed                 = camera->GetAutoExposureAdaptationSpeed();
        float exposure_compensation            = camera->GetAutoExposureCompensation();
        float fov                              = camera->GetFovHorizontalDeg();
        uint32_t preset_index                  = static_cast<uint32_t>(camera->GetPreset());
        uint32_t exposure_mode_index            = static_cast<uint32_t>(camera->GetExposureMode());
        bool first_person_control_enabled      = camera->GetFlag(CameraFlags::CanBeControlled);
        //================================================================================

        // background
        property_color("Background", color_picker_camera.get(), "clear color");

        // projection
        uint32_t proj_index = static_cast<uint32_t>(camera->GetProjectionType());
        if (property_combo("Projection", projection_types, &proj_index, "camera projection type"))
        {
            camera->SetProjection(static_cast<ProjectionType>(proj_index));
        }

        property_float("Field of View", &fov, 0.5f, 1.0f, 179.0f, "horizontal field of view", "%.1f°");

        layout::separator();
        layout::section_header("Exposure");

        property_combo(
            "Mode",
            exposure_modes,
            &exposure_mode_index,
            "manual uses the physical camera, automatic meters scene luminance"
        );

        bool manual_exposure =
            exposure_mode_index !=
            static_cast<uint32_t>(CameraExposureMode::automatic);
        ImGui::BeginDisabled(manual_exposure);
        property_float(
            "Adaptation Speed",
            &adaptation_speed,
            0.1f,
            0.0f,
            10.0f,
            "how quickly automatic exposure responds, zero adapts immediately",
            "%.2f"
        );
        property_float(
            "Compensation",
            &exposure_compensation,
            0.1f,
            -10.0f,
            10.0f,
            "automatic exposure bias in ev stops, positive brightens",
            "%.1f EV"
        );
        ImGui::EndDisabled();

        property_float(
            "Aperture",
            &aperture,
            0.1f,
            0.01f,
            150.0f,
            "controls manual exposure, depth of field and chromatic aberration",
            "f/%.1f"
        );
        property_float(
            "Shutter Speed",
            &shutter_speed,
            0.0001f,
            0.0001f,
            1.0f,
            "controls manual exposure and motion blur",
            "%.4f s"
        );
        property_float(
            "ISO",
            &iso,
            10.0f,
            1.0f,
            2000.0f,
            "controls manual exposure and film grain",
            "%.0f"
        );

        float aperture_clamped  = std::max(aperture, 0.01f);
        float shutter_clamped   = std::max(shutter_speed, 0.0001f);
        float iso_clamped       = std::max(iso, 1.0f);
        float ev100 = std::log2(
            (aperture_clamped * aperture_clamped) /
            shutter_clamped *
            (100.0f / iso_clamped)
        );
        float exposure_scale    = 1.0f / (1.2f * std::exp2(ev100));

        char exposure_value_text[32];
        snprintf(exposure_value_text, sizeof(exposure_value_text), "%.2f", ev100);
        property_text(
            "Manual EV100",
            exposure_value_text,
            "derived from aperture, shutter speed, and iso"
        );

        char exposure_scale_text[32];
        snprintf(exposure_scale_text, sizeof(exposure_scale_text), "%.6f", exposure_scale);
        property_text(
            "Manual Exposure Scale",
            exposure_scale_text,
            "physical camera multiplier used in manual mode"
        );

        if (property_combo(
            "Physical Preset",
            camera_presets,
            &preset_index,
            "applies aperture, shutter speed and iso"
        ))
        {
            camera->SetPreset(static_cast<CameraPreset>(preset_index));
            // refresh locals so the map-back below does not revert the preset to custom
            aperture      = camera->GetAperture();
            shutter_speed = camera->GetShutterSpeed();
            iso           = camera->GetIso();
        }

        layout::separator();
        layout::section_header("Controls");

        property_toggle("First Person Control", &first_person_control_enabled, "enable WASD + mouse control");

        //= MAP =======================================================================================================================================================
        if (aperture != camera->GetAperture())
        {
            camera->SetAperture(aperture);
        }
        if (shutter_speed != camera->GetShutterSpeed())
        {
            camera->SetShutterSpeed(shutter_speed);
        }
        if (iso != camera->GetIso())
        {
            camera->SetIso(iso);
        }
        if (exposure_mode_index != static_cast<uint32_t>(camera->GetExposureMode()))
        {
            camera->SetExposureMode(static_cast<CameraExposureMode>(exposure_mode_index));
        }
        if (adaptation_speed != camera->GetAutoExposureAdaptationSpeed())
        {
            camera->SetAutoExposureAdaptationSpeed(adaptation_speed);
        }
        if (exposure_compensation != camera->GetAutoExposureCompensation())
        {
            camera->SetAutoExposureCompensation(exposure_compensation);
        }
        if (fov != camera->GetFovHorizontalDeg())
        {
            camera->SetFovHorizontalDeg(fov);
        }
        if (first_person_control_enabled != camera->GetFlag(CameraFlags::CanBeControlled))
        {
            camera->SetFlag(CameraFlags::CanBeControlled, first_person_control_enabled);
        }
        //=============================================================================================================================================================
    }
    component_end();
}

void Properties::ShowTerrain(Terrain* terrain) const
{
    if (!terrain)
    {
        return;
    }

    if (component_begin("Terrain", design::accent_terrain(), terrain))
    {
        float min_y         = terrain->GetMinY();
        float max_y         = terrain->GetMaxY();
        float sea_level     = terrain->GetSeaLevel();
        float shore_width   = terrain->GetShoreWidth();
        uint32_t density    = terrain->GetDensity();
        uint32_t scale      = terrain->GetScale();
        uint32_t tiles      = terrain->GetTileCountAxis();
        uint32_t smoothing  = terrain->GetSmoothingPasses();
        bool create_border  = terrain->GetCreateBorder();
        const bool has_field = terrain->HasHeightfield();
        const bool can_generate =
            terrain->GetHeightMapSeed() != nullptr ||
            (terrain->GetWidth() > 1 && terrain->GetHeight() > 1);

        layout::section_header("Height Map");

        ImGui::BeginGroup();
        {
            auto height_map_setter = [&terrain](spartan::RHI_Texture* texture)
            {
                terrain->SetHeightMapSeed(texture);
            };

            ImGui::TextUnformatted("Source");
            ImGuiSp::tooltip(
                "input heightmap image. use 8-bit grayscale. "
                "black becomes min height, white becomes max height. "
                "click to browse or drag a texture from the asset viewer"
            );
            if (ImGuiSp::image_slot(terrain->GetHeightMapSeed(), height_map_setter))
            {
                file_selection::open([terrain](const std::string& path)
                {
                    if (FileSystem::IsSupportedImageFile(path))
                    {
                        if (const auto tex = ResourceCache::Load<RHI_Texture>(path).get())
                        {
                            terrain->SetHeightMapSeed(tex);
                        }
                    }
                });
            }
            ImGuiSp::tooltip(
                "input heightmap image. use 8-bit grayscale. "
                "black becomes min height, white becomes max height. "
                "click to browse or drag a texture from the asset viewer"
            );
        }
        ImGui::EndGroup();

        ImGui::SameLine(0, design::spacing_xl);

        ImGui::BeginGroup();
        {
            ImGui::TextUnformatted("Preview");
            ImGuiSp::tooltip(
                "grayscale preview of the generated heightfield after generate. "
                "empty until you run generate at least once"
            );
            RHI_Texture* baked = terrain->GetHeightMapFinal();
            if (baked && baked->GetResourceState() == ResourceState::PreparedForGpu)
            {
                ImGuiSp::image(baked, ImVec2(80, 80));
            }
            else
            {
                ImGui::Dummy(ImVec2(80, 80));
                ImGui::TextDisabled("empty");
            }
            ImGuiSp::tooltip(
                "grayscale preview of the generated heightfield after generate. "
                "empty until you run generate at least once"
            );
        }
        ImGui::EndGroup();

        layout::group_spacing();
        layout::section_header("Heights");

        property_float(
            "Min Height",
            &min_y,
            0.1f,
            -1000.0f,
            1000.0f,
            "world y for black heightmap pixels. must differ from max height or the terrain will be flat",
            "%.1f m"
        );
        property_float(
            "Max Height",
            &max_y,
            0.1f,
            -1000.0f,
            1000.0f,
            "world y for white heightmap pixels. zakynthos peak is about 755 m",
            "%.1f m"
        );
        if (abs(max_y - min_y) < 0.001f)
        {
            ImGui::TextColored(
                ImVec4(1.0f, 0.55f, 0.2f, 1.0f),
                "min and max are equal, generate will be flat"
            );
            ImGuiSp::tooltip(
                "set max height higher than min height before generate. "
                "equal values collapse every pixel to one height"
            );
        }
        property_float(
            "Sea Level",
            &sea_level,
            0.1f,
            -1000.0f,
            1000.0f,
            "world y of the ocean. used by erosion, border, and make island so shores meet the water",
            "%.1f m"
        );

        layout::group_spacing();
        layout::section_header("Mesh");

        if (property_uint(
            "Density",
            &density,
            1.0f,
            1,
            16,
            "extra samples between each heightmap pixel. 1 is 1:1 with the image. "
            "higher is smoother but heavier. applied on generate"
        ))
        {
            terrain->SetDensity(max(density, 1u));
        }
        if (property_uint(
            "Scale",
            &scale,
            1.0f,
            1,
            1000,
            "meters between heightmap samples. world width is about (samples_x - 1) * scale. "
            "25 fits zakynthos into roughly 36 km. applied on generate"
        ))
        {
            terrain->SetScale(max(scale, 1u));
        }
        if (property_uint(
            "Tiles",
            &tiles,
            1.0f,
            1,
            64,
            "splits the mesh into an n by n grid of tile_* children for culling and streaming. "
            "applied on generate"
        ))
        {
            terrain->SetTileCountAxis(max(tiles, 1u));
        }
        if (property_uint(
            "Smoothing",
            &smoothing,
            1.0f,
            0,
            32,
            "blur passes on the heightmap before meshing. softens hard dem edges. 0 keeps raw data"
        ))
        {
            terrain->SetSmoothingPasses(smoothing);
        }
        if (property_toggle(
            "Border",
            &create_border,
            "during generate, raise or seal map edges so the player cannot walk off the heightfield. "
            "usually off for islands, use make island instead"
        ))
        {
            terrain->SetCreateBorder(create_border);
        }

        bool spawn_biome_props = terrain->GetSpawnBiomeProps();
        if (property_toggle(
            "Spawn Biome Props",
            &spawn_biome_props,
            "spawn gpu grass, trees, rocks and flowers from the terrain biome mask. "
            "off clears them. on regenerates after analysis maps exist"
        ))
        {
            terrain->SetSpawnBiomeProps(spawn_biome_props);
            spartan::ThreadPool::AddTask([terrain]()
            {
                spartan::WorldHelpers::PopulateTerrainBiomeProps(terrain);
            });
        }

        if (spawn_biome_props)
        {
            const char* density_tooltip =
                "multiplier on the authored density for this prop. 1 is the default look, "
                "press respawn props to apply. placement is still gated by the biome mask, "
                "so a prop cannot spread onto ground its layer does not own";

            float density_tree   = terrain->GetPropDensityTree();
            float density_rock   = terrain->GetPropDensityRock();
            float density_flower = terrain->GetPropDensityFlower();

            if (property_float(
                "Tree Density",
                &density_tree,
                0.1f,
                0.0f,
                spartan::Terrain::prop_density_max,
                density_tooltip,
                "%.2f"
            ))
            {
                terrain->SetPropDensityTree(density_tree);
            }

            if (property_float(
                "Rock Density",
                &density_rock,
                0.1f,
                0.0f,
                spartan::Terrain::prop_density_max,
                density_tooltip,
                "%.2f"
            ))
            {
                terrain->SetPropDensityRock(density_rock);
            }

            if (property_float(
                "Flower Density",
                &density_flower,
                0.1f,
                0.0f,
                spartan::Terrain::prop_density_max,
                density_tooltip,
                "%.2f"
            ))
            {
                terrain->SetPropDensityFlower(density_flower);
            }

            if (ImGuiSp::button("Respawn Props", ImVec2(-1, 0)))
            {
                spartan::ThreadPool::AddTask([terrain]()
                {
                    spartan::WorldHelpers::PopulateTerrainBiomeProps(terrain);
                });
            }
            ImGuiSp::tooltip(
                "rescatter trees, rocks and flowers with the densities above. "
                "does not touch the terrain surface, so it is far cheaper than a full generate"
            );
        }

        layout::group_spacing();
        layout::section_header("Actions");

        ImGui::BeginDisabled(!can_generate);
        if (ImGuiSp::button("Generate", ImVec2(-1, 0)))
        {
            spartan::ThreadPool::AddTask([terrain]()
            {
                // clears cache and rebuilds from heightmap or flat params
                terrain->Regenerate();
            });
        }
        ImGui::EndDisabled();
        ImGuiSp::tooltip(
            can_generate
                ? "build or rebuild the mesh from the source heightmap. "
                  "clears sculpt edits and the terrain cache. click again anytime to regenerate"
                : "assign a heightmap in source first, or create a flat terrain from the sculpt window"
        );

        layout::group_spacing();
        layout::section_header("Surface Layers");

        {
            uint32_t quality = terrain->GetLayerQuality();
            float snow       = terrain->GetSnowAmount();
            float wetness    = terrain->GetWetness();

            if (property_uint(
                "Quality",
                &quality,
                1.0f,
                1,
                4,
                "how many of the highest weighted layers get sampled per pixel. "
                "3 is the sweet spot, 2 reads as a two tone surface, 4 costs more than it shows"
            ))
            {
                terrain->SetLayerQuality(quality);
            }

            if (property_float(
                "Snow Amount",
                &snow,
                0.01f,
                0.0f,
                1.0f,
                "global multiplier on the snow layer. 0 removes snow entirely regardless of altitude",
                "%.2f"
            ))
            {
                terrain->SetSnowAmount(snow);
                terrain->PushToRenderer();
            }

            if (property_float(
                "Wetness",
                &wetness,
                0.01f,
                0.0f,
                1.0f,
                "global wetness floor added on top of the flow driven amount. rain and storms drive this",
                "%.2f"
            ))
            {
                terrain->SetWetness(wetness);
                terrain->PushToRenderer();
            }

            static const std::vector<std::string> debug_views =
            {
                "Off", "Layer Weights", "Dominant Layer", "Curvature", "Flow",
                "Occlusion", "Insolation", "Deposition", "Wear", "Talus", "Wetness"
            };

            uint32_t debug_view = static_cast<uint32_t>(terrain->GetDebugView());
            if (property_combo(
                "Debug View",
                debug_views,
                &debug_view,
                "paint the terrain with what the rule system is actually reading. "
                "layer weights shows the top three picks as rgb, the rest show one analysis channel"
            ))
            {
                terrain->SetDebugView(static_cast<spartan::TerrainDebugView>(debug_view));
            }

            if (ImGuiSp::button("Reload Layer Textures", ImVec2(-1, 0)))
            {
                terrain->RefreshLayers();
            }
            ImGuiSp::tooltip(
                "rescan project/materials for each layer folder. a layer whose folder is missing is "
                "disabled and its weight goes to the layers that do exist"
            );

            if (ImGuiSp::button("Remove Props", ImVec2(-1, 0)))
            {
                spartan::WorldHelpers::RemoveTerrainProps();
            }
            ImGuiSp::tooltip(
                "delete every tree, rock and flower in the world, wherever it sits in the hierarchy, "
                "and switch off gpu grass. use this to clear props left behind by an older build, "
                "generate respawns them"
            );

            // analysis maps, without these the rules fall back to slope and altitude alone
            ImGui::BeginGroup();
            {
                auto preview = [](const char* label, spartan::RHI_Texture* texture, const char* tooltip)
                {
                    ImGui::BeginGroup();
                    ImGui::TextUnformatted(label);
                    if (texture && texture->GetResourceState() == ResourceState::PreparedForGpu)
                    {
                        ImGuiSp::image(texture, ImVec2(80, 80));
                    }
                    else
                    {
                        ImGui::Dummy(ImVec2(80, 80));
                        ImGui::TextDisabled("empty");
                    }
                    ImGuiSp::tooltip(tooltip);
                    ImGui::EndGroup();
                };

                preview(
                    "Curv/Flow/AO",
                    terrain->GetAnalysisMapA(),
                    "baked heightfield analysis. red is curvature, green is flow accumulation, "
                    "blue is sky occlusion, alpha is sediment deposition"
                );
                ImGui::SameLine(0, design::spacing_xl);
                preview(
                    "Wear/Sun/Talus",
                    terrain->GetAnalysisMapB(),
                    "baked heightfield analysis. red is bedrock wear, green is insolation, "
                    "blue is normalized height, alpha is talus scree"
                );
            }
            ImGui::EndGroup();

            if (!terrain->GetAnalysisMapA())
            {
                ImGui::TextColored(
                    ImVec4(1.0f, 0.55f, 0.2f, 1.0f),
                    "no analysis maps, run generate"
                );
                ImGuiSp::tooltip(
                    "without the analysis bake the layer rules can only see slope and altitude, "
                    "which is what the old three layer system did"
                );
            }
        }

        layout::group_spacing();
        layout::section_header("Layer Rules");

        {
            std::array<spartan::TerrainLayerRule, spartan::terrain_layer_max>& rules = terrain->GetLayerRules();
            bool rules_changed = false;

            for (uint32_t i = 0; i < spartan::terrain_layer_max; i++)
            {
                spartan::TerrainLayerRule& rule = rules[i];
                const bool enabled              = terrain->IsLayerEnabled(i);

                char header[128];
                std::snprintf(
                    header,
                    sizeof(header),
                    "%u  %s%s###terrain_layer_%u",
                    i,
                    rule.name.empty() ? "(unused)" : rule.name.c_str(),
                    enabled ? "" : "  [missing]",
                    i
                );

                ImGui::PushStyleColor(ImGuiCol_Text, enabled ? ImVec4(0.9f, 0.9f, 0.9f, 1.0f) : ImVec4(0.55f, 0.55f, 0.55f, 1.0f));
                const bool open = ImGui::TreeNode(header);
                ImGui::PopStyleColor();

                if (!open)
                {
                    continue;
                }

                ImGui::PushID(static_cast<int>(i));

                rules_changed |= property_float("Slope Min",   &rule.slope_min,   0.5f, 0.0f, 90.0f, "lower edge of the slope band this layer covers", "%.0f deg");
                rules_changed |= property_float("Slope Max",   &rule.slope_max,   0.5f, 0.0f, 90.0f, "upper edge of the slope band this layer covers", "%.0f deg");
                rules_changed |= property_float("Height Min",  &rule.height_min,  1.0f, -2000.0f, 2000.0f, "lower edge of the altitude band, measured against sea level for shore layers", "%.0f m");
                rules_changed |= property_float("Height Max",  &rule.height_max,  1.0f, -2000.0f, 2000.0f, "upper edge of the altitude band", "%.0f m");

                rules_changed |= property_float("Curvature",   &rule.curvature_influence,  0.01f, -1.0f, 1.0f, "positive favours concave gullies, negative favours convex ridges", "%.2f");
                rules_changed |= property_float("Flow",        &rule.flow_influence,       0.01f, -1.0f, 1.0f, "positive favours water channels", "%.2f");
                rules_changed |= property_float("Occlusion",   &rule.occlusion_influence,  0.01f, -1.0f, 1.0f, "positive favours crevices and valley floors", "%.2f");
                rules_changed |= property_float("Insolation",  &rule.insolation_influence, 0.01f, -1.0f, 1.0f, "positive favours sun facing slopes, negative favours shaded ones", "%.2f");
                rules_changed |= property_float("Wear",        &rule.wear_influence,       0.01f, -1.0f, 1.0f, "positive favours scoured bedrock", "%.2f");
                rules_changed |= property_float("Deposition",  &rule.deposition_influence, 0.01f, -1.0f, 1.0f, "positive favours accumulated sediment", "%.2f");
                rules_changed |= property_float("Talus",       &rule.talus_influence,      0.01f, -1.0f, 1.0f, "positive favours scree fans below cliffs", "%.2f");

                rules_changed |= property_float("Tiling",      &rule.tiling_scale,   0.01f, 0.05f, 8.0f,  "multiplies the terrain uv, higher is finer texel density", "%.2f");
                rules_changed |= property_float("Blend",       &rule.blend_contrast, 0.01f, 0.01f, 1.0f,  "height blend band width, smaller is a sharper material interface", "%.2f");
                rules_changed |= property_float("Porosity",    &rule.porosity,       0.01f, 0.0f,  1.0f,  "how much the layer darkens when wet, sand is high, rock is low", "%.2f");
                rules_changed |= property_float("Macro",       &rule.macro_strength, 0.01f, 0.0f,  1.0f,  "large scale colour breakup amount", "%.2f");
                rules_changed |= property_float("Priority",    &rule.weight_bias,    0.01f, 0.0f,  4.0f,  "overall priority against the other layers, 0 disables the layer", "%.2f");

                auto flag_toggle = [&rule, &rules_changed](const char* label, uint32_t bit, const char* tooltip)
                {
                    bool value = (rule.flags & bit) != 0;
                    if (property_toggle(label, &value, tooltip))
                    {
                        rule.flags     = value ? (rule.flags | bit) : (rule.flags & ~bit);
                        rules_changed  = true;
                    }
                };

                flag_toggle("Biplanar",  spartan::TerrainLayerFlags_Biplanar, "project on the two dominant axes, this is what stops cliff faces from smearing");
                flag_toggle("Parallax",  spartan::TerrainLayerFlags_Pom,      "march the height map when this layer dominates up close");
                flag_toggle("Snow",      spartan::TerrainLayerFlags_Snow,     "weight comes from the snow accumulation model instead of the slope and altitude bands");
                flag_toggle("Below Sea", spartan::TerrainLayerFlags_BelowSea, "the height band is measured against sea level rather than absolute world y");

                ImGui::PopID();
                ImGui::TreePop();
            }

            if (rules_changed)
            {
                terrain->PushToRenderer();
            }
        }

        layout::group_spacing();
        layout::section_header("Island");

        property_float(
            "Shore Width",
            &shore_width,
            1.0f,
            1.0f,
            50000.0f,
            "how far inland make island bends the rim down to sea level. wider is a gentler beach",
            "%.0f m"
        );

        ImGui::BeginDisabled(!has_field);
        if (ImGuiSp::button("Make Island", ImVec2(-1, 0)))
        {
            terrain->MakeIslandShore();
        }
        ImGui::EndDisabled();
        ImGuiSp::tooltip(
            has_field
                ? "bend the map borders down to sea level so the ocean clips the shore cleanly. "
                  "run generate first"
                : "generate a heightfield first, then use make island"
        );

        if (ImGuiSp::button("Sculpt", ImVec2(-1, 0)))
        {
            if (TerrainEditor* sculpt = m_editor->GetWidget<TerrainEditor>())
            {
                sculpt->SetVisible(true);
            }
        }
        ImGuiSp::tooltip(
            "open the terrain sculpt window. raise, lower, smooth, or flatten with a brush in the viewport"
        );

        layout::separator();
        layout::section_header("Statistics");

        char stat_buf[128];
        std::snprintf(stat_buf, sizeof(stat_buf), "%.1f km²", terrain->GetArea());
        property_text("Area", stat_buf, "surface area of the generated mesh in square kilometers");

        std::snprintf(stat_buf, sizeof(stat_buf), "%u x %u", terrain->GetWidth(), terrain->GetHeight());
        property_text("Samples", stat_buf, "base heightmap resolution in samples before density densify");

        std::snprintf(stat_buf, sizeof(stat_buf), "%llu", static_cast<unsigned long long>(terrain->GetVertexCount()));
        property_text("Vertices", stat_buf, "vertex count of the generated terrain mesh");

        std::snprintf(stat_buf, sizeof(stat_buf), "%llu", static_cast<unsigned long long>(terrain->GetIndexCount()));
        property_text("Indices", stat_buf, "index count of the generated terrain mesh");

        std::snprintf(stat_buf, sizeof(stat_buf), "%u x %u", tiles, tiles);
        property_text("Tile Grid", stat_buf, "current tile split, number of tile_* child entities is n times n");

        if (min_y != terrain->GetMinY())
        {
            terrain->SetMinY(min_y);
        }
        if (max_y != terrain->GetMaxY())
        {
            terrain->SetMaxY(max_y);
        }
        if (sea_level != terrain->GetSeaLevel())
        {
            terrain->SetSeaLevel(sea_level);
        }
        if (shore_width != terrain->GetShoreWidth())
        {
            terrain->SetShoreWidth(shore_width);
        }
    }
    component_end();
}

void Properties::ShowWater(spartan::Water* water) const
{
    if (!water)
    {
        return;
    }

    if (component_begin("Water", design::accent_water(), water))
    {
        float amplitude          = water->GetAmplitude();
        float choppiness         = water->GetChoppiness();
        float displacement_scale = water->GetDisplacementScale();
        float normal_strength    = water->GetNormalStrength();
        float sea_level          = water->GetSeaLevel();
        float turbidity          = water->GetTurbidity();
        float caustics_intensity = water->GetCausticsIntensity();

        uint32_t cascade_index = water->GetCascadeCount() - 1;
        if (property_combo("Detail Cascades", { "1", "2", "3", "4" }, &cascade_index, "number of band-limited scales, more cascades adds finer microwaves"))
        {
            water->SetCascadeCount(cascade_index + 1);
        }

        property_float("Amplitude",          &amplitude,          0.01f, 0.0f,    10.0f,    "linear wave height multiplier, 1 is the physical sea for the current wind");
        property_float("Choppiness",         &choppiness,         0.01f, 0.0f,    4.0f,     "horizontal sharpening of the crests, past ~2 the surface folds and foams heavily");
        property_float("Displacement Scale", &displacement_scale, 0.01f, 0.0f,    4.0f,     "scales the simulated displacement");
        property_float("Normal Strength",    &normal_strength,    0.01f, 0.0f,    4.0f,     "steepens the surface normals so waves catch more light");
        property_float("Sea Level",          &sea_level,          0.1f,  -1000.0f, 1000.0f, "world height of the water surface", "%.1f m");
        property_float("Turbidity",          &turbidity,          0.01f, 0.0f,    4.0f,     "suspended particle density, higher makes the underwater light shafts more vivid");
        property_float("Caustics Intensity", &caustics_intensity, 0.01f, 0.0f,    4.0f,     "brightness of the sun caustics dancing on submerged geometry");

        if (amplitude != water->GetAmplitude())                  { water->SetAmplitude(amplitude); }
        if (choppiness != water->GetChoppiness())                { water->SetChoppiness(choppiness); }
        if (displacement_scale != water->GetDisplacementScale()) { water->SetDisplacementScale(displacement_scale); }
        if (normal_strength != water->GetNormalStrength())       { water->SetNormalStrength(normal_strength); }
        if (sea_level != water->GetSeaLevel())                   { water->SetSeaLevel(sea_level); }
        if (turbidity != water->GetTurbidity())                  { water->SetTurbidity(turbidity); }
        if (caustics_intensity != water->GetCausticsIntensity()) { water->SetCausticsIntensity(caustics_intensity); }
    }
    component_end();
}

void Properties::ShowText3D(spartan::Text3D* text_3d) const
{
    if (!text_3d)
    {
        return;
    }

    if (component_begin("3D Text", design::accent_text_3d(), text_3d))
    {
        string text          = text_3d->GetText();
        string font_path     = text_3d->GetFontPath();
        float size           = text_3d->GetSize();
        float depth          = text_3d->GetDepth();
        float weight         = text_3d->GetWeight();
        float letter_spacing = text_3d->GetLetterSpacing();
        float line_spacing   = text_3d->GetLineSpacing();
        float resolution     =
            static_cast<float>(text_3d->GetResolution());
        uint32_t alignment   =
            static_cast<uint32_t>(text_3d->GetAlignment());

        layout::section_header("Content");

        property_input_text(
            "Text",
            &text,
            false,
            "utf 8 text rendered as geometry"
        );
        if (text != text_3d->GetText())
        {
            text_3d->SetText(text);
        }

        const uint64_t entity_id =
            text_3d->GetEntity()->GetObjectId();
        const uint64_t component_id = text_3d->GetObjectId();

        property_resource(
            "Font",
            &font_path,
            "truetype or opentype font",
            [entity_id, component_id](const string& path)
            {
                Entity* entity = World::GetEntityById(entity_id);
                Text3D* component =
                    entity
                    ? entity->GetComponent<Text3D>()
                    : nullptr;

                if (
                    component &&
                    component->GetObjectId() == component_id &&
                    FileSystem::IsSupportedFontFile(path)
                )
                {
                    component->SetFontPath(path);
                }
            }
        );

        layout::separator();
        layout::section_header("Geometry");

        if (
            property_float(
                "Size",
                &size,
                0.01f,
                0.01f,
                1000.0f,
                "text height in meters",
                "%.2f m"
            )
        )
        {
            text_3d->SetSize(size);
        }

        if (
            property_float(
                "Depth",
                &depth,
                0.01f,
                0.001f,
                1000.0f,
                "extrusion depth",
                "%.3f m"
            )
        )
        {
            text_3d->SetDepth(depth);
        }

        if (
            property_float(
                "Weight",
                &weight,
                0.001f,
                0.0f,
                1.0f,
                "extra glyph thickness in meters",
                "%.3f m"
            )
        )
        {
            text_3d->SetWeight(weight);
        }

        if (
            property_float(
                "Resolution",
                &resolution,
                1.0f,
                32.0f,
                512.0f,
                "surface detail per character",
                "%.0f"
            )
        )
        {
            text_3d->SetResolution(
                static_cast<uint32_t>(resolution)
            );
        }

        layout::separator();
        layout::section_header("Layout");

        static vector<string> alignment_names =
        {
            "Left",
            "Center",
            "Right"
        };
        if (
            property_combo(
                "Alignment",
                alignment_names,
                &alignment,
                "horizontal origin for each line"
            )
        )
        {
            text_3d->SetAlignment(
                static_cast<Text3DAlignment>(alignment)
            );
        }

        if (
            property_float(
                "Letter Spacing",
                &letter_spacing,
                0.01f,
                -10.0f,
                100.0f,
                "additional spacing between glyphs",
                "%.3f m"
            )
        )
        {
            text_3d->SetLetterSpacing(letter_spacing);
        }

        if (
            property_float(
                "Line Spacing",
                &line_spacing,
                0.01f,
                0.1f,
                10.0f,
                "line height multiplier",
                "%.2f"
            )
        )
        {
            text_3d->SetLineSpacing(line_spacing);
        }

        property_text(
            "Mesh",
            text_3d->HasMesh() ? "generated" : "pending"
        );
    }
    component_end();
}

void Properties::ShowSpline(spartan::Spline* spline) const
{
    if (!spline)
    {
        return;
    }

    if (component_begin("Spline", design::accent_spline(), spline))
    {
        //= REFLECT ===============================================
        bool closed_loop                  = spline->GetClosedLoop();
        uint32_t resolution               = spline->GetResolution();
        uint32_t point_count              = spline->GetControlPointCount();
        float road_width                  = spline->GetRoadWidth();
        float road_width_end              = spline->GetRoadWidthEnd();
        uint32_t profile                  = static_cast<uint32_t>(spline->GetProfile());
        float height                      = spline->GetHeight();
        float thickness                   = spline->GetThickness();
        uint32_t tube_sides               = spline->GetTubeSides();
        float uv_tiling_u                 = spline->GetUvTilingU();
        float uv_tiling_v                 = spline->GetUvTilingV();
        bool sidewalk_enabled             = spline->GetSidewalkEnabled();
        float sidewalk_width              = spline->GetSidewalkWidth();
        float curb_height                 = spline->GetCurbHeight();
        bool conform_to_terrain           = spline->GetConformToTerrain();
        float terrain_offset              = spline->GetTerrainOffset();
        bool mesh_enabled                 = spline->GetMeshEnabled();
        float inst_spacing                = spline->GetInstanceSpacing();
        bool inst_align                   = spline->GetAlignInstancesToSpline();
        uint64_t inst_template_id         = spline->GetInstanceTemplateId();
        float inst_lateral_offset         = spline->GetInstanceLateralOffset();
        bool inst_mirror                  = spline->GetInstanceMirror();
        bool inst_face_inward             = spline->GetInstanceFaceInward();
        float inst_random_offset          = spline->GetInstanceRandomOffset();
        float inst_random_scale_min       = spline->GetInstanceRandomScaleMin();
        float inst_random_scale_max       = spline->GetInstanceRandomScaleMax();
        float inst_random_yaw             = spline->GetInstanceRandomYaw();
        uint32_t attach_mode              = static_cast<uint32_t>(spline->GetAttachMode());
        uint64_t source_spline_id         = spline->GetSourceSplineEntityId();
        float attach_lateral_offset       = spline->GetAttachLateralOffset();
        float attach_vertical_offset      = spline->GetAttachVerticalOffset();
        bool attach_inherit_closed_loop   = spline->GetAttachInheritClosedLoop();
        uint32_t attach_sample_count      = spline->GetAttachSampleCount();
        //=========================================================

        layout::section_header("Spline");

        bool is_attached_loop_inherited = attach_mode != 0 && attach_inherit_closed_loop && source_spline_id != 0;
        ImGui::BeginDisabled(is_attached_loop_inherited);
        if (property_toggle("Closed Loop", &closed_loop, "connect the last point back to the first"))
        {
            spline->SetClosedLoop(closed_loop);
        }
        ImGui::EndDisabled();

        float resolution_f = static_cast<float>(resolution);
        if (property_float("Resolution", &resolution_f, 1.0f, 2.0f, 100.0f, "line segments per span", "%.0f"))
        {
            spline->SetResolution(static_cast<uint32_t>(resolution_f));
        }

        layout::separator();
        layout::section_header("Attachment");

        // build a list of entities that have a spline component (excluding self)
        const vector<Entity*>& all_entities_attach = World::GetEntities();
        vector<string> attach_names;
        vector<uint64_t> attach_ids;
        uint32_t attach_selected_index = 0;

        attach_names.push_back("(none)");
        attach_ids.push_back(0);

        for (Entity* candidate : all_entities_attach)
        {
            if (!candidate || candidate == spline->GetEntity())
            {
                continue;
            }

            if (candidate->GetComponent<spartan::Spline>())
            {
                attach_ids.push_back(candidate->GetObjectId());
                attach_names.push_back(candidate->GetObjectName());

                if (candidate->GetObjectId() == source_spline_id)
                {
                    attach_selected_index = static_cast<uint32_t>(attach_names.size() - 1);
                }
            }
        }

        if (property_combo("Source Spline", attach_names, &attach_selected_index, "attach this spline to another spline so it follows it"))
        {
            spline->SetSourceSplineEntityId(attach_ids[attach_selected_index]);
        }

        static vector<string> attach_mode_names = { "None", "Centerline", "Left Edge", "Right Edge", "Left Outer", "Right Outer" };
        ImGui::BeginDisabled(source_spline_id == 0);
        if (property_combo("Attach Mode", attach_mode_names, &attach_mode, "where on the source spline to snap"))
        {
            spline->SetAttachMode(static_cast<spartan::SplineAttachMode>(attach_mode));
        }
        ImGui::EndDisabled();

        bool attachment_active = source_spline_id != 0 && attach_mode != 0;
        if (attachment_active)
        {
            if (property_float("Lateral Offset", &attach_lateral_offset, 0.05f, -100.0f, 100.0f, "extra inward or outward push from the chosen edge", "%.2f m"))
            {
                spline->SetAttachLateralOffset(attach_lateral_offset);
            }
            if (property_float("Vertical Offset", &attach_vertical_offset, 0.05f, -100.0f, 100.0f, "extra height above the source path", "%.2f m"))
            {
                spline->SetAttachVerticalOffset(attach_vertical_offset);
            }
            if (property_toggle("Inherit Closed Loop", &attach_inherit_closed_loop, "match the source closed loop state automatically"))
            {
                spline->SetAttachInheritClosedLoop(attach_inherit_closed_loop);
            }
            float sample_count_f = static_cast<float>(attach_sample_count);
            if (property_float("Sample Count", &sample_count_f, 1.0f, 0.0f, 4096.0f, "0 means use the source resolution", "%.0f"))
            {
                spline->SetAttachSampleCount(static_cast<uint32_t>(sample_count_f));
            }
        }

        // attached splines derive their path from the source so own control points are hidden
        if (!attachment_active)
        {
            layout::separator();
            layout::section_header("Control Points");

            char stat_buf[64];
            std::snprintf(stat_buf, sizeof(stat_buf), "%u", point_count);
            property_text("Count", stat_buf);

            if (point_count >= 2)
            {
                std::snprintf(stat_buf, sizeof(stat_buf), "%.2f m", spline->GetLength());
                property_text("Length", stat_buf);
            }

            layout::group_spacing();

            float button_width = 100.0f * spartan::Window::GetDpiScale();
            float total_width  = button_width * 2.0f + design::spacing_md;
            ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - total_width) * 0.5f + ImGui::GetCursorPosX());

            if (ImGuiSp::button("+ Add Point", ImVec2(button_width, 0)))
            {
                math::Vector3 position = math::Vector3::Zero;
                if (point_count > 0)
                {
                    spartan::Entity* parent = spline->GetEntity();
                    for (uint32_t i = parent->GetChildrenCount(); i > 0; i--)
                    {
                        if (spartan::Entity* child = parent->GetChildByIndex(i - 1))
                        {
                            if (child->GetObjectName().find("spline_point_") == 0)
                            {
                                position = child->GetPositionLocal() + math::Vector3(5.0f, 0.0f, 0.0f);
                                break;
                            }
                        }
                    }
                }
                spline->AddControlPoint(position);
            }

            ImGui::SameLine(0, design::spacing_md);

            ImGui::BeginDisabled(point_count == 0);
            if (ImGuiSp::button("- Remove Last", ImVec2(button_width, 0)))
            {
                spline->RemoveLastControlPoint();
            }
            ImGui::EndDisabled();
        }
        else
        {
            char attached_length_buf[64];
            std::snprintf(attached_length_buf, sizeof(attached_length_buf), "%.2f m", spline->GetLength());
            property_text("Length", attached_length_buf);
        }

        layout::separator();
        layout::section_header("Mesh Generation");

        if (property_toggle("Enabled", &mesh_enabled, "automatically generate a mesh along the spline"))
        {
            spline->SetMeshEnabled(mesh_enabled);
            if (!mesh_enabled)
            {
                spline->ClearRoadMesh();
            }
        }

        ImGui::BeginDisabled(!mesh_enabled);

        // profile type
        static vector<string> profile_names = { "Road", "Wall", "Tube", "Fence", "Channel" };
        if (property_combo("Profile", profile_names, &profile, "cross-section shape extruded along the spline"))
        {
            spline->SetProfile(static_cast<spartan::SplineProfile>(profile));
        }

        // width (start)
        if (property_float("Width (Start)", &road_width, 0.1f, 0.5f, 100.0f, "width at the start of the spline", "%.1f m"))
        {
            spline->SetRoadWidth(road_width);
        }

        // width (end)
        if (property_float("Width (End)", &road_width_end, 0.1f, 0.5f, 100.0f, "width at the end of the spline", "%.1f m"))
        {
            spline->SetRoadWidthEnd(road_width_end);
        }

        // profile-specific properties
        spartan::SplineProfile current_profile = static_cast<spartan::SplineProfile>(profile);
        bool needs_height = current_profile == spartan::SplineProfile::Wall ||
                            current_profile == spartan::SplineProfile::Fence ||
                            current_profile == spartan::SplineProfile::Channel;
        if (needs_height)
        {
            if (property_float("Height", &height, 0.1f, 0.1f, 100.0f, "height in meters", "%.1f m"))
            {
                spline->SetHeight(height);
            }
        }

        bool needs_thickness = current_profile == spartan::SplineProfile::Wall ||
                               current_profile == spartan::SplineProfile::Fence;
        if (needs_thickness)
        {
            if (property_float("Thickness", &thickness, 0.01f, 0.01f, 10.0f, "thickness in meters", "%.2f m"))
            {
                spline->SetThickness(thickness);
            }
        }

        if (current_profile == spartan::SplineProfile::Tube)
        {
            float tube_sides_f = static_cast<float>(tube_sides);
            if (property_float("Sides", &tube_sides_f, 1.0f, 3.0f, 64.0f, "tube cross-section subdivisions", "%.0f"))
            {
                spline->SetTubeSides(static_cast<uint32_t>(tube_sides_f));
            }
        }

        // uv tiling
        if (property_float("UV Tiling U", &uv_tiling_u, 0.01f, 0.01f, 100.0f, "texture tiling across the profile", "%.2f"))
        {
            spline->SetUvTilingU(uv_tiling_u);
        }
        if (property_float("UV Tiling V", &uv_tiling_v, 0.01f, 0.01f, 100.0f, "texture tiling along the spline", "%.2f"))
        {
            spline->SetUvTilingV(uv_tiling_v);
        }

        // sidewalk/curb (road profile only)
        if (current_profile == spartan::SplineProfile::Road)
        {
            if (property_toggle("Sidewalks", &sidewalk_enabled, "add raised sidewalks on both sides of the road"))
            {
                spline->SetSidewalkEnabled(sidewalk_enabled);
            }

            if (sidewalk_enabled)
            {
                if (property_float("Sidewalk Width", &sidewalk_width, 0.1f, 0.1f, 20.0f, "width of each sidewalk", "%.1f m"))
                {
                    spline->SetSidewalkWidth(sidewalk_width);
                }
                if (property_float("Curb Height", &curb_height, 0.01f, 0.01f, 2.0f, "height of the curb above the road", "%.2f m"))
                {
                    spline->SetCurbHeight(curb_height);
                }
            }
        }

        // terrain conforming
        if (property_toggle("Conform to Terrain", &conform_to_terrain, "snap the mesh to the terrain surface"))
        {
            spline->SetConformToTerrain(conform_to_terrain);
        }
        if (conform_to_terrain)
        {
            if (property_float("Terrain Offset", &terrain_offset, 0.001f, 0.0f, 10.0f, "vertical offset above the terrain", "%.3f m"))
            {
                spline->SetTerrainOffset(terrain_offset);
            }
        }

        ImGui::EndDisabled();

        layout::separator();
        layout::section_header("Instancing");

        // template entity picker (any entity in the world that is not the spline itself)
        const vector<Entity*>& all_entities = World::GetEntities();
        vector<string> template_names;
        vector<uint64_t> template_ids;
        uint32_t template_selected_index = 0;

        template_names.push_back("(default cylinder)");
        template_ids.push_back(0);

        for (Entity* candidate : all_entities)
        {
            if (!candidate || candidate == spline->GetEntity())
            {
                continue;
            }

            template_ids.push_back(candidate->GetObjectId());
            template_names.push_back(candidate->GetObjectName());

            if (candidate->GetObjectId() == inst_template_id)
            {
                template_selected_index = static_cast<uint32_t>(template_names.size() - 1);
            }
        }

        if (property_combo("Template", template_names, &template_selected_index, "entity hierarchy to clone for each instance"))
        {
            spline->SetInstanceTemplateId(template_ids[template_selected_index]);
        }

        if (property_float("Spacing", &inst_spacing, 0.1f, 0.5f, 100.0f, "distance between instances in meters", "%.1f m"))
        {
            spline->SetInstanceSpacing(inst_spacing);
        }

        if (property_float("Lateral Offset", &inst_lateral_offset, 0.1f, 0.0f, 100.0f, "perpendicular distance from the spline centerline", "%.2f m"))
        {
            spline->SetInstanceLateralOffset(inst_lateral_offset);
        }

        if (property_toggle("Mirror", &inst_mirror, "also spawn a mirrored instance on the opposite side"))
        {
            spline->SetInstanceMirror(inst_mirror);
        }

        if (property_toggle("Face Inward", &inst_face_inward, "orient instances so their local +z faces the spline centerline"))
        {
            spline->SetInstanceFaceInward(inst_face_inward);
        }

        if (property_toggle("Align to Spline", &inst_align, "rotate instances to follow the spline direction (ignored when face inward is on)"))
        {
            spline->SetAlignInstancesToSpline(inst_align);
        }

        // procedural placement randomization
        if (property_float("Random Offset", &inst_random_offset, 0.1f, 0.0f, 50.0f, "random lateral jitter in addition to the lateral offset", "%.1f m"))
        {
            spline->SetInstanceRandomOffset(inst_random_offset);
        }
        if (property_float("Random Scale Min", &inst_random_scale_min, 0.01f, 0.01f, 10.0f, "minimum random scale", "%.2f"))
        {
            spline->SetInstanceRandomScaleMin(inst_random_scale_min);
        }
        if (property_float("Random Scale Max", &inst_random_scale_max, 0.01f, 0.01f, 10.0f, "maximum random scale", "%.2f"))
        {
            spline->SetInstanceRandomScaleMax(inst_random_scale_max);
        }
        if (property_float("Random Yaw", &inst_random_yaw, 1.0f, 0.0f, 360.0f, "random rotation around the up axis in degrees", "%.0f\xc2\xb0"))
        {
            spline->SetInstanceRandomYaw(inst_random_yaw);
        }

        layout::group_spacing();

        // spawn / clear instance buttons
        float inst_button_width = 120.0f * spartan::Window::GetDpiScale();
        ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - inst_button_width) * 0.5f + ImGui::GetCursorPosX());

        ImGui::BeginDisabled(point_count < 2);
        if (ImGuiSp::button("Spawn", ImVec2(inst_button_width, 0)))
        {
            spline->SpawnInstances();
        }
        ImGui::EndDisabled();

        ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - inst_button_width) * 0.5f + ImGui::GetCursorPosX());
        if (ImGuiSp::button("Clear Instances", ImVec2(inst_button_width, 0)))
        {
            spline->ClearInstances();
        }
    }
    component_end();
}

void Properties::ShowSplineFollower(spartan::SplineFollower* follower) const
{
    if (!follower)
    {
        return;
    }

    if (component_begin("Spline Follower", design::accent_spline_follower(), follower))
    {
        //= REFLECT ========================================
        float speed         = follower->GetSpeed();
        uint32_t mode       = static_cast<uint32_t>(follower->GetFollowMode());
        bool align          = follower->GetAlignToSpline();
        bool flip           = follower->GetFlipForward();
        float progress      = follower->GetProgress();
        uint64_t spline_id  = follower->GetSplineEntityId();
        Entity* spline_ent  = follower->GetSplineEntity();
        bool animate_wheels = follower->GetAnimateWheels();
        float wheel_radius  = follower->GetWheelRadius();
        float steer_angle   = follower->GetMaxSteerAngle();
        //==================================================

        layout::section_header("Spline Reference");

        // build a list of entities that have a spline component
        const vector<Entity*>& all_entities = World::GetEntities();
        vector<string> spline_names;
        vector<uint64_t> spline_ids;
        uint32_t selected_index = 0;

        // first entry is "none"
        spline_names.push_back("(none)");
        spline_ids.push_back(0);

        for (Entity* entity : all_entities)
        {
            if (entity && entity->GetComponent<Spline>())
            {
                spline_ids.push_back(entity->GetObjectId());
                spline_names.push_back(entity->GetObjectName());

                if (entity->GetObjectId() == spline_id)
                {
                    selected_index = static_cast<uint32_t>(spline_names.size() - 1);
                }
            }
        }

        if (property_combo("Spline", spline_names, &selected_index, "the spline entity to follow"))
        {
            follower->SetSplineEntityId(spline_ids[selected_index]);
        }

        layout::separator();
        layout::section_header("Movement");

        // speed
        if (property_float("Speed", &speed, 0.1f, 0.0f, 1000.0f, "movement speed in world units per second", "%.1f"))
        {
            follower->SetSpeed(speed);
        }

        // follow mode
        static vector<string> mode_names = { "Clamp", "Loop", "Ping Pong" };
        if (property_combo("Mode", mode_names, &mode, "behavior when reaching the end of the spline"))
        {
            follower->SetFollowMode(static_cast<spartan::SplineFollowMode>(mode));
        }

        // align to spline
        if (property_toggle("Align To Spline", &align, "orient the entity along the spline tangent"))
        {
            follower->SetAlignToSpline(align);
        }

        // flip forward
        if (property_toggle("Flip Forward", &flip, "rotate 180 degrees for meshes whose forward axis points backwards"))
        {
            follower->SetFlipForward(flip);
        }

        // progress (read-only)
        char progress_buf[32];
        snprintf(progress_buf, sizeof(progress_buf), "%.1f%%", progress * 100.0f);
        property_text("Progress", progress_buf, "current position along the spline");

        layout::separator();
        layout::section_header("Wheels");

        // animate wheels, auto finds child entities named tire or wheel
        if (property_toggle("Animate Wheels", &animate_wheels, "roll every wheel with speed and steer the front wheels into turns, wheels are auto detected from child entities named tire or wheel"))
        {
            follower->SetAnimateWheels(animate_wheels);
        }

        // wheel radius, 0 auto estimates from the mesh
        if (property_float("Wheel Radius", &wheel_radius, 0.01f, 0.0f, 5.0f, "wheel radius in meters, 0 auto estimates it from the wheel mesh", "%.2f"))
        {
            follower->SetWheelRadius(wheel_radius);
        }

        // max steering angle for the front wheels
        if (property_float("Max Steer Angle", &steer_angle, 0.5f, 0.0f, 90.0f, "maximum steering angle of the front wheels in degrees", "%.0f"))
        {
            follower->SetMaxSteerAngle(steer_angle);
        }
    }
    component_end();
}

void Properties::ShowSpawnPoint(spartan::SpawnPoint* spawn_point) const
{
    if (!spawn_point)
    {
        return;
    }

    if (
        component_begin(
            "Spawn Point",
            design::accent_entity(),
            spawn_point
        )
    )
    {
        ImGui::TextWrapped(
            "The entity transform defines the exact spawn pose."
        );
    }
    component_end();
}

void Properties::ShowCarReset(spartan::CarReset* car_reset) const
{
    if (!car_reset)
    {
        return;
    }

    if (
        component_begin(
            "Car Reset",
            design::accent_entity(),
            car_reset
        )
    )
    {
        const uint64_t spawn_point_id =
            car_reset->GetSpawnPointEntityId();
        const vector<Entity*>& entities = World::GetEntities();
        vector<string> names = { "(none)" };
        vector<uint64_t> ids = { 0 };
        uint32_t selected_index = 0;

        for (Entity* entity : entities)
        {
            if (
                !entity ||
                !entity->GetComponent<SpawnPoint>()
            )
            {
                continue;
            }

            ids.push_back(entity->GetObjectId());
            names.push_back(entity->GetObjectName());

            if (entity->GetObjectId() == spawn_point_id)
            {
                selected_index =
                    static_cast<uint32_t>(names.size() - 1);
            }
        }

        if (
            property_combo(
                "Spawn Point",
                names,
                &selected_index,
                "spawn point used when play starts and the car resets"
            )
        )
        {
            car_reset->SetSpawnPointEntityId(ids[selected_index]);
        }
    }
    component_end();
}

void Properties::ShowAudioSource(spartan::AudioSource* audio_source) const
{
    if (!audio_source)
    {
        return;
    }

    if (component_begin("Audio Source", design::accent_audio(), audio_source))
    {
        //= REFLECT ==============================================
        string audio_clip_name  = audio_source->GetAudioClipName();
        bool mute               = audio_source->GetMute();
        bool play_on_start      = audio_source->GetPlayOnStart();
        bool loop               = audio_source->GetLoop();
        bool is_3d              = audio_source->GetIs3d();
        float volume            = audio_source->GetVolume();
        float pitch             = audio_source->GetPitch();
        bool reverb_enabled     = audio_source->GetReverbEnabled();
        float reverb_room_size  = audio_source->GetReverbRoomSize();
        float reverb_decay      = audio_source->GetReverbDecay();
        float reverb_wet        = audio_source->GetReverbWet();
        //========================================================

        // audio clip resource
        property_resource("Audio Clip", &audio_clip_name, "audio file", [audio_source](const std::string& path) {
            if (FileSystem::IsSupportedAudioFile(path))
            {
                audio_source->SetAudioClip(path);
            }
        });

        if (auto payload = ImGuiSp::receive_drag_drop_payload(ImGuiSp::DragPayloadType::Audio))
        {
            if (payload->path[0] != '\0')
            {
                audio_source->SetAudioClip(payload->path);
            }
        }

        layout::separator();
        layout::section_header("Playback");

        property_toggle("Play on Start", &play_on_start, "auto-play when scene starts");
        property_toggle("Loop", &loop, "repeat playback");
        property_toggle("Mute", &mute, "silence output");

        layout::group_spacing();

        // volume slider
        {
            layout::begin_property("Volume", "output volume");
            ImGui::SliderFloat("##volume", &volume, 0.0f, 1.0f, "%.0f%%");
        }

        // pitch slider
        {
            layout::begin_property("Pitch", "playback speed");
            ImGui::SliderFloat("##pitch", &pitch, 0.01f, 5.0f, "%.2fx");
        }

        layout::separator();
        layout::section_header("Spatialization");

        property_toggle("3D Sound", &is_3d, "position-based audio");

        layout::separator();
        layout::section_header("Progress");

        // progress bar
        {
            layout::begin_property("", nullptr);
            float progress = audio_source->GetProgress();
            ImGui::ProgressBar(progress, ImVec2(-1, 0), "");
        }

        layout::separator();
        layout::section_header("Reverb");

        property_toggle("Enabled", &reverb_enabled, "apply reverb effect");

        ImGui::BeginDisabled(!reverb_enabled);
        {
            layout::begin_property("Room Size", "reverb room size");
            ImGui::SliderFloat("##room_size", &reverb_room_size, 0.0f, 1.0f);

            layout::begin_property("Decay", "reverb decay time");
            ImGui::SliderFloat("##decay", &reverb_decay, 0.0f, 0.99f);

            layout::begin_property("Wet Mix", "reverb blend amount");
            ImGui::SliderFloat("##wet", &reverb_wet, 0.0f, 1.0f);
        }
        ImGui::EndDisabled();

        //= MAP =========================================================================================
        if (mute != audio_source->GetMute())
        {
            audio_source->SetMute(mute);
        }
        if (play_on_start != audio_source->GetPlayOnStart())
        {
            audio_source->SetPlayOnStart(play_on_start);
        }
        if (loop != audio_source->GetLoop())
        {
            audio_source->SetLoop(loop);
        }
        if (is_3d != audio_source->GetIs3d())
        {
            audio_source->SetIs3d(is_3d);
        }
        if (volume != audio_source->GetVolume())
        {
            audio_source->SetVolume(volume);
        }
        if (pitch != audio_source->GetPitch())
        {
            audio_source->SetPitch(pitch);
        }
        if (reverb_enabled != audio_source->GetReverbEnabled())
        {
            audio_source->SetReverbEnabled(reverb_enabled);
        }
        if (reverb_room_size != audio_source->GetReverbRoomSize())
        {
            audio_source->SetReverbRoomSize(reverb_room_size);
        }
        if (reverb_decay != audio_source->GetReverbDecay())
        {
            audio_source->SetReverbDecay(reverb_decay);
        }
        if (reverb_wet != audio_source->GetReverbWet())
        {
            audio_source->SetReverbWet(reverb_wet);
        }
        //===============================================================================================
    }
    component_end();
}

void Properties::ShowVolume(spartan::Volume* volume) const
{
    if (!volume)
    {
        return;
    }

    if (component_begin("Volume", design::accent_volume(), volume))
    {
        // reflect
        const math::BoundingBox& bounding_box = volume->GetBoundingBox();
        Vector3 min = bounding_box.GetMin();
        Vector3 max = bounding_box.GetMax();

        layout::section_header("Bounds");

        property_vector3("Min", min, "minimum corner");
        property_vector3("Max", max, "maximum corner");

        // map
        if (min != bounding_box.GetMin() || max != bounding_box.GetMax())
        {
            volume->SetBoundingBox(math::BoundingBox(min, max));
        }

        layout::separator();
        layout::section_header("Render Overrides");

        // scrollable area of render options
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.2f));

        if (ImGui::BeginChild("##vol_overrides", ImVec2(0, 220.0f), true))
        {
            int id_counter = 0;
            for (const auto& [cvar_name, cvar] : ConsoleRegistry::Get().GetAll())
            {
                // only include renderer options
                if (cvar_name.size() < 2 || cvar_name[0] != 'r' || cvar_name[1] != '.')
                {
                    continue;
                }

                string name(cvar_name);
                float global_value = get<float>(*cvar.m_value_ptr);

                ImGui::PushID(id_counter++);

                bool is_active = volume->GetOptions().find(name) != volume->GetOptions().end();

                // format display name (remove "r." prefix)
                string display_name = name.substr(2);

                // toggle for override
                if (ImGuiSp::toggle_switch(display_name.c_str(), &is_active))
                {
                    if (is_active)
                    {
                        volume->SetOption(name.c_str(), global_value);
                    }
                    else
                    {
                        volume->RemoveOption(name.c_str());
                    }
                }

                // value editor when active
                if (is_active)
                {
                    ImGui::SameLine();
                    ImGui::PushItemWidth(-FLT_MIN);

                    float value = volume->GetOption(name.c_str());
                    if (ImGuiSp::draw_float_wrap("##v", &value, 0.1f))
                    {
                        volume->SetOption(name.c_str(), value);
                    }

                    ImGui::PopItemWidth();
                }

                ImGui::PopID();
            }
        }
        ImGui::EndChild();

        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        layout::separator();
        layout::section_header("Audio Reverb");

        bool reverb_enabled = volume->GetReverbEnabled();
        property_toggle("Enabled", &reverb_enabled, "apply reverb to audio sources inside this volume (derived from volume size)");

        if (reverb_enabled != volume->GetReverbEnabled())
        {
            volume->SetReverbEnabled(reverb_enabled);
        }
    }
    component_end();
}

void Properties::ShowParticleSystem(spartan::ParticleSystem* particle_system) const
{
    if (!particle_system)
    {
        return;
    }

    if (component_begin("Particle System", design::accent_particles(), particle_system))
    {
        //= REFLECT =====================================================
        uint32_t max_particles   = particle_system->GetMaxParticles();
        float emission_rate      = particle_system->GetEmissionRate();
        float lifetime           = particle_system->GetLifetime();
        float start_speed        = particle_system->GetStartSpeed();
        float start_size         = particle_system->GetStartSize();
        float end_size           = particle_system->GetEndSize();
        float gravity_modifier   = particle_system->GetGravityModifier();
        float emission_radius    = particle_system->GetEmissionRadius();
        Vector3 emission_direction = particle_system->GetEmissionDirection();
        float emission_cone_angle  = particle_system->GetEmissionConeAngle();
        float directional_blend    = particle_system->GetDirectionalBlend();
        float emissive_strength    = particle_system->GetEmissiveStrength();
        float soft_depth_scale     = particle_system->GetSoftDepthScale();
        float volume_density       = particle_system->GetVolumeDensity();
        float volume_anisotropy    = particle_system->GetVolumeAnisotropy();
        float volume_shadowing     = particle_system->GetVolumeShadowing();
        float drag                 = particle_system->GetDrag();
        float turbulence_strength  = particle_system->GetTurbulenceStrength();
        float wind_influence       = particle_system->GetWindInfluence();
        float velocity_inheritance = particle_system->GetVelocityInheritance();
        float velocity_stretch     = particle_system->GetVelocityStretch();
        float spawn_burst          = particle_system->GetSpawnBurst();
        float flipbook_rows        = static_cast<float>(particle_system->GetFlipbookRows());
        float flipbook_columns     = static_cast<float>(particle_system->GetFlipbookColumns());
        float flipbook_fps         = particle_system->GetFlipbookFps();
        color_picker_particle_start->SetColor(particle_system->GetStartColor());
        color_picker_particle_end->SetColor(particle_system->GetEndColor());
        //===============================================================

        // preset selector
        static vector<string> preset_names =
        {
            "Custom", "Fire", "Smoke", "Steam", "Sparks", "Dust", "Snow",
            "Rain", "Confetti", "Fireflies", "Blood", "Magic", "Explosion",
            "Waterfall", "Embers", "Tire Smoke", "Exhaust"
        };
        uint32_t preset_index = static_cast<uint32_t>(particle_system->GetPreset());
        if (property_combo("Preset", preset_names, &preset_index, "apply a preset to quickly configure the particle system"))
        {
            particle_system->ApplyPreset(static_cast<spartan::ParticlePreset>(preset_index));

            // refresh local copies after preset application
            max_particles    = particle_system->GetMaxParticles();
            emission_rate    = particle_system->GetEmissionRate();
            lifetime         = particle_system->GetLifetime();
            start_speed      = particle_system->GetStartSpeed();
            start_size       = particle_system->GetStartSize();
            end_size         = particle_system->GetEndSize();
            gravity_modifier = particle_system->GetGravityModifier();
            emission_radius  = particle_system->GetEmissionRadius();
            emission_direction = particle_system->GetEmissionDirection();
            emission_cone_angle = particle_system->GetEmissionConeAngle();
            directional_blend = particle_system->GetDirectionalBlend();
            emissive_strength = particle_system->GetEmissiveStrength();
            soft_depth_scale = particle_system->GetSoftDepthScale();
            volume_density = particle_system->GetVolumeDensity();
            volume_anisotropy = particle_system->GetVolumeAnisotropy();
            volume_shadowing = particle_system->GetVolumeShadowing();
            drag = particle_system->GetDrag();
            turbulence_strength = particle_system->GetTurbulenceStrength();
            wind_influence = particle_system->GetWindInfluence();
            velocity_inheritance = particle_system->GetVelocityInheritance();
            velocity_stretch = particle_system->GetVelocityStretch();
            color_picker_particle_start->SetColor(particle_system->GetStartColor());
            color_picker_particle_end->SetColor(particle_system->GetEndColor());
        }

        layout::separator();
        layout::section_header("Effect Asset");

        string effect_path = particle_system->GetEffectPath().empty() ? "inline custom" : particle_system->GetEffectPath();
        ImGui::TextWrapped("%s", effect_path.c_str());
        if (ImGuiSp::button("Load Effect"))
        {
            file_selection::open([particle_system](const std::string& path)
            {
                if (FileSystem::GetExtensionFromFilePath(path) == ".particle")
                {
                    particle_system->LoadEffect(path);
                }
            });
        }
        ImGui::SameLine();
        if (ImGuiSp::button("Save Effect"))
        {
            const string path = particle_system->GetEffectPath().empty() ? "worlds/custom.particle" : particle_system->GetEffectPath();
            particle_system->SaveEffect(path);
            particle_system->SetEffectPath(path);
        }

        layout::separator();
        layout::section_header("Emission");

        // max particles
        float max_p_float = static_cast<float>(max_particles);
        if (property_float("Max Particles", &max_p_float, 100.0f, 100.0f, 100000.0f, "maximum number of particles alive at once", "%.0f"))
        {
            particle_system->SetMaxParticles(static_cast<uint32_t>(max_p_float));
        }

        // emission rate
        if (property_float("Rate", &emission_rate, 1.0f, 0.0f, 10000.0f, "particles emitted per second", "%.0f /s"))
        {
            particle_system->SetEmissionRate(emission_rate);
        }

        // emission radius
        if (property_float("Radius", &emission_radius, 0.01f, 0.0f, 100.0f, "sphere emission radius in meters", "%.2f m"))
        {
            particle_system->SetEmissionRadius(emission_radius);
        }

        property_vector3("Direction", emission_direction, "world-space emission direction");
        if (emission_direction.x != particle_system->GetEmissionDirection().x ||
            emission_direction.y != particle_system->GetEmissionDirection().y ||
            emission_direction.z != particle_system->GetEmissionDirection().z)
        {
            particle_system->SetEmissionDirection(emission_direction);
        }

        if (property_float("Cone", &emission_cone_angle, 0.01f, 0.0f, math::pi, "directional cone angle in radians", "%.2f rad"))
        {
            particle_system->SetEmissionConeAngle(emission_cone_angle);
        }

        if (property_float("Direction Blend", &directional_blend, 0.01f, 0.0f, 1.0f, "0 is random upward smoke, 1 follows direction", "%.2f"))
        {
            particle_system->SetDirectionalBlend(directional_blend);
        }

        if (property_float("Spawn Burst", &spawn_burst, 1.0f, 0.0f, 100000.0f, "one-shot particles emitted when the effect is loaded or triggered", "%.0f"))
        {
            particle_system->SetSpawnBurst(spawn_burst);
        }

        layout::separator();
        layout::section_header("Lifetime & Motion");

        // lifetime
        if (property_float("Lifetime", &lifetime, 0.1f, 0.01f, 60.0f, "particle lifetime in seconds", "%.1f s"))
        {
            particle_system->SetLifetime(lifetime);
        }

        // start speed
        if (property_float("Start Speed", &start_speed, 0.1f, 0.0f, 100.0f, "initial speed in meters per second", "%.1f m/s"))
        {
            particle_system->SetStartSpeed(start_speed);
        }

        // gravity modifier
        if (property_float("Gravity", &gravity_modifier, 0.1f, -20.0f, 20.0f, "gravity multiplier (negative = downward)", "%.1f"))
        {
            particle_system->SetGravityModifier(gravity_modifier);
        }

        if (property_float("Drag", &drag, 0.01f, 0.0f, 10.0f, "air resistance, higher values make smoke settle faster", "%.2f"))
        {
            particle_system->SetDrag(drag);
        }

        if (property_float("Turbulence", &turbulence_strength, 0.01f, 0.0f, 10.0f, "curl-like procedural motion strength", "%.2f"))
        {
            particle_system->SetTurbulenceStrength(turbulence_strength);
        }

        if (property_float("Wind", &wind_influence, 0.01f, 0.0f, 5.0f, "amount of world wind inherited during simulation", "%.2f"))
        {
            particle_system->SetWindInfluence(wind_influence);
        }

        if (property_float("Inherit Velocity", &velocity_inheritance, 0.01f, 0.0f, 2.0f, "amount of emitter velocity inherited at birth", "%.2f"))
        {
            particle_system->SetVelocityInheritance(velocity_inheritance);
        }

        if (property_float("Stretch", &velocity_stretch, 0.01f, 0.0f, 5.0f, "screen-space stretch along particle velocity", "%.2f"))
        {
            particle_system->SetVelocityStretch(velocity_stretch);
        }

        layout::separator();
        layout::section_header("Appearance");

        // start size
        if (property_float("Start Size", &start_size, 0.01f, 0.001f, 10.0f, "particle size at birth in meters", "%.3f m"))
        {
            particle_system->SetStartSize(start_size);
        }

        // end size
        if (property_float("End Size", &end_size, 0.01f, 0.0f, 10.0f, "particle size at death in meters", "%.3f m"))
        {
            particle_system->SetEndSize(end_size);
        }

        static vector<string> blend_modes = { "Alpha", "Premultiplied", "Additive" };
        uint32_t blend_mode = static_cast<uint32_t>(particle_system->GetBlendMode());
        if (property_combo("Blend", blend_modes, &blend_mode, "how particles composite into the hdr frame"))
        {
            particle_system->SetBlendMode(static_cast<spartan::ParticleBlendMode>(blend_mode));
        }

        static vector<string> lighting_modes = { "Lit", "Unlit", "Emissive" };
        uint32_t lighting_mode = static_cast<uint32_t>(particle_system->GetLightingMode());
        if (property_combo("Lighting", lighting_modes, &lighting_mode, "lit smoke, unlit vapor, or hdr emissive fire"))
        {
            particle_system->SetLightingMode(static_cast<spartan::ParticleLightingMode>(lighting_mode));
        }

        static vector<string> render_modes = { "Billboard", "Volumetric" };
        uint32_t render_mode = static_cast<uint32_t>(particle_system->GetRenderMode());
        if (property_combo("Render Mode", render_modes, &render_mode, "billboard quads or froxel raymarched density"))
        {
            particle_system->SetRenderMode(static_cast<spartan::ParticleRenderMode>(render_mode));
        }

        if (property_float("Emissive", &emissive_strength, 0.1f, 0.0f, 100.0f, "hdr multiplier for emissive particles", "%.1f"))
        {
            particle_system->SetEmissiveStrength(emissive_strength);
        }

        if (property_float("Soft Depth", &soft_depth_scale, 0.1f, 0.0f, 100.0f, "higher values make depth intersections tighter", "%.1f"))
        {
            particle_system->SetSoftDepthScale(soft_depth_scale);
        }

        if (property_float("Volume Density", &volume_density, 0.01f, 0.0f, 25.0f, "density scale for volumetric render mode", "%.2f"))
        {
            particle_system->SetVolumeDensity(volume_density);
        }

        if (property_float("Volume Anisotropy", &volume_anisotropy, 0.01f, -0.9f, 0.9f, "phase bias for volumetric forward scattering", "%.2f"))
        {
            particle_system->SetVolumeAnisotropy(volume_anisotropy);
        }

        if (property_float("Volume Shadowing", &volume_shadowing, 0.01f, 0.0f, 1.0f, "self shadowing strength for dense smoke", "%.2f"))
        {
            particle_system->SetVolumeShadowing(volume_shadowing);
        }

        layout::group_spacing();

        // texture, used as the billboard mask, falls back to a procedural circle when unset
        {
            auto texture_setter = [particle_system](spartan::RHI_Texture* texture)
            {
                particle_system->SetTexture(texture);
            };

            spartan::RHI_Texture* texture = particle_system->GetTexture();
            ImGui::TextUnformatted("Texture");
            ImGui::SameLine();
            if (ImGuiSp::image_slot(texture, texture_setter))
            {
                file_selection::open([particle_system](const std::string& path)
                {
                    if (FileSystem::IsSupportedImageFile(path))
                    {
                        particle_system->SetTexture(path);
                    }
                });
            }
        }

        if (property_float("Flipbook Rows", &flipbook_rows, 1.0f, 1.0f, 32.0f, "texture atlas rows", "%.0f"))
        {
            particle_system->SetFlipbookRows(static_cast<uint32_t>(flipbook_rows));
        }

        if (property_float("Flipbook Columns", &flipbook_columns, 1.0f, 1.0f, 32.0f, "texture atlas columns", "%.0f"))
        {
            particle_system->SetFlipbookColumns(static_cast<uint32_t>(flipbook_columns));
        }

        if (property_float("Flipbook FPS", &flipbook_fps, 1.0f, 0.0f, 120.0f, "atlas playback rate, 0 spreads frames over lifetime", "%.0f"))
        {
            particle_system->SetFlipbookFps(flipbook_fps);
        }

        layout::group_spacing();

        // start color
        ImGui::PushID("particle_start_color");
        property_color("Start Color", color_picker_particle_start.get(), "particle color at birth");
        ImGui::PopID();

        // end color
        ImGui::PushID("particle_end_color");
        property_color("End Color", color_picker_particle_end.get(), "particle color at death");
        ImGui::PopID();

        //= MAP ==========================================================
        if (color_picker_particle_start->GetColor() != particle_system->GetStartColor())
        {
            particle_system->SetStartColor(color_picker_particle_start->GetColor());
        }
        if (color_picker_particle_end->GetColor() != particle_system->GetEndColor())
        {
            particle_system->SetEndColor(color_picker_particle_end->GetColor());
        }
        //=================================================================
    }
    component_end();
}

void Properties::ShowAddComponentButton() const
{
    ImGui::Dummy(ImVec2(0, design::spacing_lg));

    // centered add button
    float button_width = 140.0f * spartan::Window::GetDpiScale();
    ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - button_width) * 0.5f + ImGui::GetCursorPosX());

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(design::spacing_lg, design::spacing_md));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.4f, 0.55f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.5f, 0.65f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.35f, 0.5f, 1.0f));

    if (ImGuiSp::button("+ Add Component", ImVec2(button_width, 0)))
    {
        ImGui::OpenPopup("##ComponentContextMenu_Add");
    }

    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);

    ComponentContextMenu_Add();

    // save as prefab button, hidden for code prefabs since baking one to a file loses its code behavior
    if (Entity* entity = get_selected_entity())
    {
        if (!entity->IsCodePrefab())
        {
            ImGui::Dummy(ImVec2(0, design::spacing_sm));

            float save_button_width = 160.0f * spartan::Window::GetDpiScale();
            ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - save_button_width) * 0.5f + ImGui::GetCursorPosX());

            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(design::spacing_lg, design::spacing_md));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.50f, 0.35f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.60f, 0.40f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.25f, 0.45f, 0.30f, 1.0f));

            if (ImGuiSp::button("Save as Prefab...", ImVec2(save_button_width, 0)))
            {
                ImGui::OpenPopup("##SaveAsPrefab");
            }

            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(2);

            // save-as-prefab popup
            ShowSaveAsPrefabPopup(entity);
        }
    }
}

void Properties::ShowSaveAsPrefabPopup(spartan::Entity* entity) const
{
    static char prefab_name[256]  = "";
    static bool needs_init        = true;

    // detect when the popup is about to open (was closed, now opening)
    bool is_open = ImGui::IsPopupOpen("##SaveAsPrefab");
    if (is_open && needs_init)
    {
        strncpy_s(prefab_name, sizeof(prefab_name), entity->GetObjectName().c_str(), _TRUNCATE);
        needs_init = false;
    }
    else if (!is_open)
    {
        needs_init = true;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(design::spacing_xl, design::spacing_lg));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(design::spacing_md, design::spacing_md));

    if (ImGui::BeginPopup("##SaveAsPrefab"))
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
        ImGui::PushFont(Editor::font_bold, 0.0f);
        ImGui::TextUnformatted("Save as Prefab");
        ImGui::PopFont();
        ImGui::PopStyleColor();

        ImGui::Dummy(ImVec2(0, design::spacing_sm));

        ImGui::TextUnformatted("Name:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputText("##prefab_name_input", prefab_name, sizeof(prefab_name));

        // show the path that will be used
        string preview_path = string("prefabs/") + prefab_name + ".prefab";
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
        ImGui::Text("file: %s", preview_path.c_str());
        ImGui::PopStyleColor();

        ImGui::Dummy(ImVec2(0, design::spacing_sm));

        // save button
        bool name_valid = strlen(prefab_name) > 0;
        ImGui::BeginDisabled(!name_valid);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.50f, 0.35f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.60f, 0.40f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.20f, 0.45f, 0.30f, 1.0f));

        if (ImGuiSp::button("Save", ImVec2(80.0f, 0)))
        {
            string file_path = string(ResourceCache::GetProjectDirectory()) + "/prefabs/" + prefab_name + ".prefab";
            if (Prefab::SaveToFile(entity, file_path))
            {
                // tag the entity as a file prefab so future world saves reference the file
                entity->SetPrefabFilePath(file_path);

                // the saved hierarchy is now the base, so it is not re-emitted as overrides
                entity->MarkPrefabBaseline();
            }
            ImGui::CloseCurrentPopup();
        }

        ImGui::PopStyleColor(3);
        ImGui::EndDisabled();

        ImGui::SameLine();

        if (ImGuiSp::button("Cancel", ImVec2(80.0f, 0)))
        {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    ImGui::PopStyleVar(2);
}

void Properties::ComponentContextMenu_Add() const
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(design::spacing_md, design::spacing_md));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(design::spacing_md, design::spacing_sm));

    if (ImGui::BeginPopup("##ComponentContextMenu_Add"))
    {
        if (Entity* entity = get_selected_entity())
        {
            // scripting (Lua support)
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
            ImGui::TextUnformatted("SCRIPTING");
            ImGui::PopStyleColor();
            ImGui::Separator();

            if (ImGui::MenuItem("Script"))
            {
                entity->AddComponent<Script>();
            }

            ImGui::Dummy(ImVec2(0, design::spacing_sm));

            ImGui::PushStyleColor(
                ImGuiCol_Text,
                ImVec4(0.6f, 0.6f, 0.6f, 1.0f)
            );
            ImGui::TextUnformatted("GAMEPLAY");
            ImGui::PopStyleColor();
            ImGui::Separator();

            if (ImGui::MenuItem("Spawn Point"))
            {
                entity->AddComponent<SpawnPoint>();
            }

            if (ImGui::MenuItem("Car Reset"))
            {
                entity->AddComponent<CarReset>();
            }

            ImGui::Dummy(ImVec2(0, design::spacing_sm));

            // rendering
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
            ImGui::TextUnformatted("RENDERING");
            ImGui::PopStyleColor();
            ImGui::Separator();

            if (ImGui::MenuItem("Camera"))
            {
                entity->AddComponent<Camera>();
            }

            if (ImGui::MenuItem("Render"))
            {
                entity->AddComponent<Render>();
            }

            if (ImGui::MenuItem("3D Text"))
            {
                entity->AddComponent<Text3D>();
            }

            if (ImGui::MenuItem("Terrain"))
            {
                entity->AddComponent<Terrain>();
            }

            if (ImGui::MenuItem("Spline"))
            {
                entity->AddComponent<Spline>();
            }

            if (ImGui::MenuItem("Spline Follower"))
            {
                entity->AddComponent<SplineFollower>();
            }

            ImGui::Dummy(ImVec2(0, design::spacing_sm));

            // lighting
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
            ImGui::TextUnformatted("LIGHTING");
            ImGui::PopStyleColor();
            ImGui::Separator();

            if (ImGui::BeginMenu("Light"))
            {
                if (ImGui::MenuItem("Directional"))
                {
                    entity->AddComponent<Light>()->SetLightType(LightType::Directional);
                }
                if (ImGui::MenuItem("Point"))
                {
                    entity->AddComponent<Light>()->SetLightType(LightType::Point);
                }
                if (ImGui::MenuItem("Spot"))
                {
                    entity->AddComponent<Light>()->SetLightType(LightType::Spot);
                }
                if (ImGui::MenuItem("Area"))
                {
                    entity->AddComponent<Light>()->SetLightType(LightType::Area);
                }
                ImGui::EndMenu();
            }

            if (ImGui::MenuItem("Volume"))
            {
                entity->AddComponent<Volume>();
            }

            ImGui::Dummy(ImVec2(0, design::spacing_sm));

            // effects
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
            ImGui::TextUnformatted("EFFECTS");
            ImGui::PopStyleColor();
            ImGui::Separator();

            if (ImGui::MenuItem("Particle System"))
            {
                entity->AddComponent<ParticleSystem>();
            }

            if (ImGui::MenuItem("Water"))
            {
                entity->AddComponent<Water>();
            }

            ImGui::Dummy(ImVec2(0, design::spacing_sm));

            // physics & audio
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
            ImGui::TextUnformatted("PHYSICS & AUDIO");
            ImGui::PopStyleColor();
            ImGui::Separator();

            if (ImGui::MenuItem("Physics"))
            {
                entity->AddComponent<Physics>();
            }

            if (ImGui::BeginMenu("Audio"))
            {
                if (ImGui::MenuItem("Audio Source"))
                {
                    entity->AddComponent<AudioSource>();
                }
                ImGui::EndMenu();
            }

        }

        ImGui::EndPopup();
    }

    ImGui::PopStyleVar(2);
}
