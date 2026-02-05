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
#include "Properties.h"
#include "Window.h"
#include "FileSelection.h"
#include "../ImGui/ImGui_Extension.h"
#include "../ImGui/Source/imgui_stdlib.h"
#include "../Widgets/ButtonColorPicker.h"
#include "Core/Engine.h"
#include "World/Entity.h"
#include "Rendering/Material.h"
#include "World/Components/Renderable.h"
#include "World/Components/Physics.h"
#include "World/Components/Light.h"
#include "World/Components/AudioSource.h"
#include "World/Components/Terrain.h"
#include "World/Components/Camera.h"
#include "World/Components/Volume.h"
#include "Rendering/Renderer.h"
//=======================================

//= NAMESPACES =========
using namespace std;
using namespace spartan;
using namespace math;
//======================

weak_ptr<Material> Properties::m_inspected_material;

namespace
{
    // color pickers
    std::unique_ptr<ButtonColorPicker> m_material_color_picker;
    std::unique_ptr<ButtonColorPicker> m_colorPicker_light;
    std::unique_ptr<ButtonColorPicker> m_colorPicker_camera;

    // context menu state
    string context_menu_id;
    Component* copied_component = nullptr;

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
        inline ImVec4 accent_renderable() { return ImVec4(0.60f, 0.50f, 0.70f, 1.0f); }
        inline ImVec4 accent_material()   { return ImVec4(0.70f, 0.55f, 0.50f, 1.0f); }
        inline ImVec4 accent_physics()    { return ImVec4(0.55f, 0.65f, 0.80f, 1.0f); }
        inline ImVec4 accent_audio()      { return ImVec4(0.70f, 0.45f, 0.55f, 1.0f); }
        inline ImVec4 accent_terrain()    { return ImVec4(0.50f, 0.70f, 0.45f, 1.0f); }
        inline ImVec4 accent_volume()     { return ImVec4(0.55f, 0.55f, 0.75f, 1.0f); }

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
            
            // subtle text color for labels
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.70f, 0.70f, 1.0f));
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
                IM_COL32(255, 255, 255, 20), 1.0f
            );
            ImGui::Dummy(ImVec2(0, design::spacing_md));
        }

        // section header within a component
        inline void section_header(const char* title)
        {
            ImGui::Dummy(ImVec2(0, design::spacing_sm));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
            ImGui::PushFont(Editor::font_bold);
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
                            entity->RemoveComponentById(component->GetObjectId());
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

    bool component_begin(const char* name, const ImVec4& accent_color, Component* component_instance, bool options = true, const bool removable = true)
    {
        ImGui::PushID(name);
        
        // header styling
        ImVec4 header_bg       = design::dimmed(accent_color, 0.25f);
        ImVec4 header_hovered  = design::dimmed(accent_color, 0.35f);
        ImVec4 header_active   = design::dimmed(accent_color, 0.30f);
        
        ImGui::PushStyleColor(ImGuiCol_Header, header_bg);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, header_hovered);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, header_active);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(design::spacing_md, design::spacing_md));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

        // draw collapsing header
        ImGui::PushFont(Editor::font_bold);
        const bool is_expanded = ImGuiSp::collapsing_header(name, ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_DefaultOpen);
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
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.1f));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            if (ImGuiSp::image_button(spartan::ResourceCache::GetIcon(IconType::Gear), icon_size, false))
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
            const ImVec4& bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
            ImVec4 content_bg = ImVec4(bg.x + 0.02f, bg.y + 0.02f, bg.z + 0.02f, 1.0f);

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
        if (readonly) flags |= ImGuiInputTextFlags_ReadOnly;
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
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.70f, 0.70f, 1.0f));
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

        const ImU32 colors[3] = {
            IM_COL32(200, 60, 60, 255),   // x - red
            IM_COL32(90, 160, 40, 255),   // y - green
            IM_COL32(50, 120, 200, 255)   // z - blue
        };
        const char* axis[3] = { "X", "Y", "Z" };
        float* values[3] = { &vec.x, &vec.y, &vec.z };

        for (int i = 0; i < 3; ++i)
        {
            if (i > 0) ImGui::SameLine(0, between_groups);

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

    m_colorPicker_light     = make_unique<ButtonColorPicker>("Light Color Picker");
    m_material_color_picker = make_unique<ButtonColorPicker>("Material Color Picker");
    m_colorPicker_camera    = make_unique<ButtonColorPicker>("Camera Color Picker");

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
            ImGui::PushFont(Editor::font_bold);
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
        }
        else if (Entity* entity = get_selected_entity())
        {
            Renderable* renderable = entity->GetComponent<Renderable>();
            Material* material     = renderable ? renderable->GetMaterial() : nullptr;

            ShowEntity(entity);
            ShowLight(entity->GetComponent<Light>());
            ShowCamera(entity->GetComponent<Camera>());
            ShowTerrain(entity->GetComponent<Terrain>());
            ShowAudioSource(entity->GetComponent<AudioSource>());
            ShowRenderable(renderable);
            ShowMaterial(material);
            ShowPhysics(entity->GetComponent<Physics>());
            ShowVolume(entity->GetComponent<Volume>());

            ShowAddComponentButton();
        }
        else if (!m_inspected_material.expired())
        {
            ShowMaterial(m_inspected_material.lock().get());
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

void Properties::Inspect(spartan::Entity* entity)
{
    // if we were previously inspecting a material, save changes
    if (!m_inspected_material.expired())
    {
        m_inspected_material.lock()->SaveToFile(m_inspected_material.lock()->GetResourceFilePath());
    }
    m_inspected_material.reset();
}

void Properties::Inspect(const shared_ptr<Material> material)
{
    m_inspected_material = material;
}

void Properties::ShowEntity(Entity* entity) const
{
    if (component_begin("Entity", design::accent_entity(), nullptr, true, false))
    {
        // entity name display
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        ImGui::PushFont(Editor::font_bold);
        ImGui::TextUnformatted(entity->GetObjectName().c_str());
        ImGui::PopFont();
        ImGui::PopStyleColor();
        
        layout::group_spacing();

        // active toggle
        bool is_active = entity->GetActive();
        if (property_toggle("Active", &is_active, "enable or disable this entity"))
        {
            entity->SetActive(is_active);
        }

        layout::separator();
        layout::section_header("Transform");

        // transform properties
        property_transform(entity);
    }
    component_end();
}

void Properties::ShowLight(spartan::Light* light) const
{
    if (!light)
        return;

    if (component_begin("Light", design::accent_light(), light))
    {
        //= REFLECT ==========================================================================
        static vector<string> types = { "Directional", "Point", "Spot", "Area" };
        float intensity             = light->GetIntensityLumens();
        float temperature_kelvin    = light->GetTemperature();
        float angle                 = light->GetAngle() * math::rad_to_deg * 2.0f;
        bool shadows                = light->GetFlag(spartan::LightFlags::Shadows);
        bool shadows_screen_space   = light->GetFlag(spartan::LightFlags::ShadowsScreenSpace);
        bool volumetric             = light->GetFlag(spartan::LightFlags::Volumetric);
        float range                 = light->GetRange();
        float area_width            = light->GetAreaWidth();
        float area_height           = light->GetAreaHeight();
        m_colorPicker_light->SetColor(light->GetColor());
        //====================================================================================

        // type
        uint32_t selection_index = static_cast<uint32_t>(light->GetLightType());
        if (property_combo("Type", types, &selection_index))
        {
            light->SetLightType(static_cast<LightType>(selection_index));
        }

        layout::separator();
        layout::section_header("Appearance");

        // color
        property_color("Color", m_colorPicker_light.get(), "light color");

        // temperature
        property_float("Temperature", &temperature_kelvin, 10.0f, 1000.0f, 40000.0f, "color temperature in kelvin", "%.0f K");

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

            bool is_directional = light->GetLightType() == LightType::Directional;

            if (!is_directional)
            {
                uint32_t intensity_type_index = static_cast<uint32_t>(light->GetIntensity());
                if (property_combo("Preset", intensity_types, &intensity_type_index, "common light intensity presets"))
                {
                    light->SetIntensity(static_cast<LightIntensity>(intensity_type_index));
                    intensity = light->GetIntensityLumens();
                }
            }

            const char* unit_tooltip = is_directional ? "intensity in lux" : "intensity in lumens";
            property_float("Intensity", &intensity, 10.0f, 0.0f, 120000.0f, unit_tooltip, is_directional ? "%.0f lux" : "%.0f lm");
        }

        layout::separator();
        layout::section_header("Shadows");

        property_toggle("Enabled", &shadows, "cast shadows from this light");

        if (shadows)
        {
            property_toggle("Screen Space", &shadows_screen_space, "screen space contact shadows");
            property_toggle("Volumetric", &volumetric, "volumetric light scattering");
        }

        // directional-specific options
        if (light->GetLightType() == LightType::Directional)
        {
            layout::separator();
            layout::section_header("Day/Night");

            bool day_night_cycle = light->GetFlag(spartan::LightFlags::DayNightCycle);
            if (property_toggle("Day/Night Cycle", &day_night_cycle, "automatic sun movement"))
            {
                light->SetFlag(spartan::LightFlags::DayNightCycle, day_night_cycle);
            }

            ImGui::BeginDisabled(!day_night_cycle);
            bool real_time_cycle = light->GetFlag(spartan::LightFlags::RealTimeCycle);
            if (property_toggle("Real Time", &real_time_cycle, "sync with actual time"))
            {
                light->SetFlag(spartan::LightFlags::RealTimeCycle, real_time_cycle);
            }
            ImGui::EndDisabled();
        }

        // range (point/spot/area)
        if (light->GetLightType() != LightType::Directional)
        {
            layout::separator();
            layout::section_header("Attenuation");
            property_float("Range", &range, 0.1f, 0.0f, 1000.0f, "light falloff distance in meters", "%.1f m");
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
        if (intensity != light->GetIntensityLumens())             light->SetIntensity(intensity);
        if (angle != light->GetAngle() * math::rad_to_deg * 0.5f) light->SetAngle(angle * math::deg_to_rad * 0.5f);
        if (range != light->GetRange())                           light->SetRange(range);
        if (area_width != light->GetAreaWidth())                  light->SetAreaWidth(area_width);
        if (area_height != light->GetAreaHeight())                light->SetAreaHeight(area_height);
        if (m_colorPicker_light->GetColor() != light->GetColor()) light->SetColor(m_colorPicker_light->GetColor());
        if (temperature_kelvin != light->GetTemperature())        light->SetTemperature(temperature_kelvin);
        light->SetFlag(spartan::LightFlags::ShadowsScreenSpace, shadows_screen_space);
        light->SetFlag(spartan::LightFlags::Volumetric, volumetric);
        light->SetFlag(spartan::LightFlags::Shadows, shadows);
        //=========================================================================================================
    }
    component_end();
}

void Properties::ShowRenderable(spartan::Renderable* renderable) const
{
    if (!renderable)
        return;

    if (component_begin("Renderable", design::accent_renderable(), renderable))
    {
        //= REFLECT ========================================================================================================
        string& name_mesh                 = const_cast<string&>(renderable->GetMeshName());
        Material* material                = renderable->GetMaterial();
        uint32_t instance_count           = renderable->GetInstanceCount();
        static string name_material_empty = "N/A";
        string& name_material             = material ? const_cast<string&>(material->GetObjectName()) : name_material_empty;
        bool cast_shadows                 = renderable->HasFlag(RenderableFlags::CastsShadows);
        bool is_visible                   = renderable->IsVisible();
        //==================================================================================================================

        // mesh info
        property_input_text("Mesh", &name_mesh, true);

        // lod information
        int lod_count = renderable->GetLodCount();
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
                    ImGui::Text("%d", renderable->GetVertexCount(i));
                }

                // indices row
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted("Indices");
                for (int i = 0; i < lod_count; ++i)
                {
                    ImGui::TableSetColumnIndex(i + 1);
                    ImGui::Text("%d", renderable->GetIndexCount(i));
                }

                ImGui::EndTable();
            }
            ImGui::PopStyleColor(5);
            ImGui::PopStyleVar();

            if (!renderable->HasInstancing())
            {
                char lod_buf[32];
                std::snprintf(lod_buf, sizeof(lod_buf), "%u", renderable->GetLodIndex());
                property_text("Current LOD", lod_buf);
            }
        }

        // instancing
        if (instance_count > 1 || renderable->HasInstancing())
        {
            layout::separator();
            layout::section_header("Instancing");

            char buf[32];
            std::snprintf(buf, sizeof(buf), "%u", instance_count);
            property_text("Instances", buf);

            if (renderable->HasInstancing() && ImGui::TreeNode("Instance Transforms"))
            {
                for (uint32_t i = 0; i < renderable->GetInstanceCount(); ++i)
                {
                    Matrix instance = renderable->GetInstance(i, true);

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
        float draw_distance = renderable->GetMaxRenderDistance();
        if (property_float("Draw Distance", &draw_distance, 1.0f, 0.0f, 10000.0f, "maximum render distance", "%.0f m"))
        {
            renderable->SetMaxRenderDistance(draw_distance);
        }

        // material
        property_resource("Material", &name_material, "assigned material", [renderable](const std::string& path) {
            if (FileSystem::IsEngineMaterialFile(path))
            {
                renderable->SetMaterial(path);
            }
        });

        // drag drop for material
        if (auto payload = ImGuiSp::receive_drag_drop_payload(ImGuiSp::DragPayloadType::Material))
        {
            renderable->SetMaterial(std::get<const char*>(payload->data));
        }

        layout::group_spacing();

        property_toggle("Cast Shadows", &cast_shadows, "whether this object casts shadows");
        property_text("Visible", is_visible ? "Yes" : "No", "current visibility state");

        //= MAP =========================================================
        renderable->SetFlag(RenderableFlags::CastsShadows, cast_shadows);
        //===============================================================
    }
    component_end();
}

void Properties::ShowPhysics(Physics* body) const
{
    if (!body)
        return;

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
            "Mesh", "Mesh (Convex)", "Controller", "Vehicle"
        };

        uint32_t body_type_index = static_cast<uint32_t>(body->GetBodyType());
        if (property_combo("Shape", body_types, &body_type_index, "collision shape type"))
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

        // map values back
        if (mass != body->GetMass())                        body->SetMass(mass);
        if (friction != body->GetFriction())                body->SetFriction(friction);
        if (friction_rolling != body->GetFrictionRolling()) body->SetFrictionRolling(friction_rolling);
        if (restitution != body->GetRestitution())          body->SetRestitution(restitution);

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

        if (center_of_mass != body->GetCenterOfMass()) body->SetCenterOfMass(center_of_mass);
        if (is_static != body->IsStatic())             body->SetStatic(is_static);
        if (is_kinematic != body->IsKinematic())       body->SetKinematic(is_kinematic);
    }
    component_end();
}

void Properties::ShowMaterial(Material* material) const
{
    if (!material)
        return;

    if (component_begin("Material", design::accent_material(), nullptr, false))
    {
        //= REFLECT ================================================
        math::Vector2 tiling = Vector2(
            material->GetProperty(MaterialProperty::TextureTilingX),
            material->GetProperty(MaterialProperty::TextureTilingY)
        );

        math::Vector2 offset = Vector2(
            material->GetProperty(MaterialProperty::TextureOffsetX),
            material->GetProperty(MaterialProperty::TextureOffsetY)
        );

        m_material_color_picker->SetColor(Color(
            material->GetProperty(MaterialProperty::ColorR),
            material->GetProperty(MaterialProperty::ColorG),
            material->GetProperty(MaterialProperty::ColorB),
            material->GetProperty(MaterialProperty::ColorA)
        ));
        //==========================================================

        // material name
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        ImGui::PushFont(Editor::font_bold);
        ImGui::TextUnformatted(material->GetObjectName().c_str());
        ImGui::PopFont();
        ImGui::PopStyleColor();

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

                    if (slot > 0) ImGui::SameLine();

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
                                    setter(tex);
                                }
                            }
                        });
                    }
                    ImGui::PopID();
                }

                if (show_modifier) ImGui::SameLine();
            }

            // modifier/multiplier
            if (show_modifier)
            {
                // constrain width to available space
                float available_width = ImGui::GetContentRegionAvail().x;
                float slider_width    = ImMin(available_width, 120.0f);
                
                if (mat_property == MaterialProperty::ColorA)
                {
                    m_material_color_picker->Update();
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

        // primary surface properties
        show_property("Color",     "surface base color",     MaterialTextureType::Color,     MaterialProperty::ColorA);
        show_property("Roughness", "microfacet roughness",   MaterialTextureType::Roughness, MaterialProperty::Roughness);
        show_property("Metalness", "metallic vs dielectric", MaterialTextureType::Metalness, MaterialProperty::Metalness);
        show_property("Normal",    "surface normal detail",  MaterialTextureType::Normal,    MaterialProperty::Normal);

        layout::separator();
        layout::section_header("Detail");

        show_property("Height",     "parallax/displacement",    MaterialTextureType::Height,    MaterialProperty::Height);
        show_property("Occlusion",  "ambient occlusion",        MaterialTextureType::Occlusion, MaterialProperty::Max);
        show_property("Emission",   "light emission",           MaterialTextureType::Emission,  MaterialProperty::Max);
        show_property("Alpha Mask", "transparency cutout",      MaterialTextureType::AlphaMask, MaterialProperty::Max);

        layout::separator();
        layout::section_header("Advanced");

        show_property("Clearcoat",            "extra specular layer",         MaterialTextureType::Max, MaterialProperty::Clearcoat);
        show_property("Clearcoat Roughness",  "clearcoat roughness",          MaterialTextureType::Max, MaterialProperty::Clearcoat_Roughness);
        show_property("Anisotropic",          "anisotropic reflection",       MaterialTextureType::Max, MaterialProperty::Anisotropic);
        show_property("Anisotropic Rotation", "anisotropy direction",         MaterialTextureType::Max, MaterialProperty::AnisotropicRotation);
        show_property("Sheen",                "soft velvet reflection",       MaterialTextureType::Max, MaterialProperty::Sheen);
        show_property("Subsurface",           "subsurface scattering",        MaterialTextureType::Max, MaterialProperty::SubsurfaceScattering);

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
        bool invert_x = material->GetProperty(MaterialProperty::TextureInvertX) > 0.5f;
        bool invert_y = material->GetProperty(MaterialProperty::TextureInvertY) > 0.5f;
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

        bool emissive_from_albedo = material->GetProperty(MaterialProperty::EmissiveFromAlbedo) != 0.0f;
        if (property_toggle("Emissive from Albedo", &emissive_from_albedo, "use albedo as emission"))
        {
            material->SetProperty(MaterialProperty::EmissiveFromAlbedo, emissive_from_albedo ? 1.0f : 0.0f);
        }

        bool world_space_uv = material->GetProperty(MaterialProperty::WorldSpaceUv) != 0.0f;
        if (property_toggle("World Space UV", &world_space_uv, "world-space texture coordinates"))
        {
            material->SetProperty(MaterialProperty::WorldSpaceUv, world_space_uv ? 1.0f : 0.0f);
        }

        //= MAP ===============================================================================
        material->SetProperty(MaterialProperty::TextureTilingX, tiling.x);
        material->SetProperty(MaterialProperty::TextureTilingY, tiling.y);
        material->SetProperty(MaterialProperty::TextureOffsetX, offset.x);
        material->SetProperty(MaterialProperty::TextureOffsetY, offset.y);
        material->SetProperty(MaterialProperty::TextureInvertX, invert_x ? 1.0f : 0.0f);
        material->SetProperty(MaterialProperty::TextureInvertY, invert_y ? 1.0f : 0.0f);
        material->SetProperty(MaterialProperty::ColorR, m_material_color_picker->GetColor().r);
        material->SetProperty(MaterialProperty::ColorG, m_material_color_picker->GetColor().g);
        material->SetProperty(MaterialProperty::ColorB, m_material_color_picker->GetColor().b);
        material->SetProperty(MaterialProperty::ColorA, m_material_color_picker->GetColor().a);
        //=====================================================================================
    }

    component_end();
}

void Properties::ShowCamera(Camera* camera) const
{
    if (!camera)
        return;

    if (component_begin("Camera", design::accent_camera(), camera))
    {
        //= REFLECT ======================================================================
        static vector<string> projection_types = { "Perspective", "Orthographic" };
        float aperture                         = camera->GetAperture();
        float shutter_speed                    = camera->GetShutterSpeed();
        float iso                              = camera->GetIso();
        float fov                              = camera->GetFovHorizontalDeg();
        bool first_person_control_enabled      = camera->GetFlag(CameraFlags::CanBeControlled);
        //================================================================================

        // background
        property_color("Background", m_colorPicker_camera.get(), "clear color");

        // projection
        uint32_t proj_index = static_cast<uint32_t>(camera->GetProjectionType());
        if (property_combo("Projection", projection_types, &proj_index, "camera projection type"))
        {
            camera->SetProjection(static_cast<ProjectionType>(proj_index));
        }

        property_float("Field of View", &fov, 0.5f, 1.0f, 179.0f, "horizontal field of view", "%.1f°");

        layout::separator();
        layout::section_header("Exposure");

        property_float("Aperture", &aperture, 0.1f, 0.01f, 150.0f, "f-stop (affects DoF and brightness)", "f/%.1f");
        property_float("Shutter Speed", &shutter_speed, 0.0001f, 0.0f, 1.0f, "exposure time in seconds (affects motion blur)", "%.4f s");
        property_float("ISO", &iso, 10.0f, 0.0f, 2000.0f, "sensor sensitivity (affects noise)", "%.0f");

        layout::separator();
        layout::section_header("Controls");

        property_toggle("First Person Control", &first_person_control_enabled, "enable WASD + mouse control");

        //= MAP =======================================================================================================================================================
        if (aperture != camera->GetAperture())          camera->SetAperture(aperture);
        if (shutter_speed != camera->GetShutterSpeed()) camera->SetShutterSpeed(shutter_speed);
        if (iso != camera->GetIso())                    camera->SetIso(iso);
        if (fov != camera->GetFovHorizontalDeg())       camera->SetFovHorizontalDeg(fov);
        if (first_person_control_enabled != camera->GetFlag(CameraFlags::CanBeControlled)) camera->SetFlag(CameraFlags::CanBeControlled, first_person_control_enabled);
        //=============================================================================================================================================================
    }
    component_end();
}

void Properties::ShowTerrain(Terrain* terrain) const
{
    if (!terrain)
        return;

    if (component_begin("Terrain", design::accent_terrain(), terrain))
    {
        //= REFLECT =====================
        float min_y = terrain->GetMinY();
        float max_y = terrain->GetMaxY();
        //===============================

        layout::section_header("Height Map");

        // height map texture slot and preview
        ImGui::BeginGroup();
        {
            auto height_map_setter = [&terrain](spartan::RHI_Texture* texture) {
                terrain->SetHeightMapSeed(texture);
            };

            // source height map
            ImGui::TextUnformatted("Source");
            if (ImGuiSp::image_slot(terrain->GetHeightMapSeed(), height_map_setter))
            {
                file_selection::open([terrain](const std::string& path) {
                    if (FileSystem::IsSupportedImageFile(path))
                    {
                        if (const auto tex = ResourceCache::Load<RHI_Texture>(path).get())
                        {
                            terrain->SetHeightMapSeed(tex);
                        }
                    }
                });
            }
        }
        ImGui::EndGroup();

        ImGui::SameLine(0, design::spacing_xl);

        // generated preview
        ImGui::BeginGroup();
        {
            ImGui::TextUnformatted("Generated");
            ImGuiSp::image(terrain->GetHeightMapFinal(), ImVec2(80, 80));
        }
        ImGui::EndGroup();

        layout::group_spacing();

        // height range
        property_float("Min Height", &min_y, 0.1f, -1000.0f, 1000.0f, "minimum terrain height", "%.1f m");
        property_float("Max Height", &max_y, 0.1f, -1000.0f, 1000.0f, "maximum terrain height", "%.1f m");

        layout::group_spacing();

        // generate button
        float button_width = 120.0f * spartan::Window::GetDpiScale();
        ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - button_width) * 0.5f + ImGui::GetCursorPosX());
        if (ImGuiSp::button("Generate Terrain", ImVec2(button_width, 0)))
        {
            spartan::ThreadPool::AddTask([terrain]() {
                terrain->Generate();
            });
        }

        layout::separator();
        layout::section_header("Statistics");

        // stats in a compact format
        char stat_buf[128];
        std::snprintf(stat_buf, sizeof(stat_buf), "%.1f km²", terrain->GetArea());
        property_text("Area", stat_buf);

        std::snprintf(stat_buf, sizeof(stat_buf), "%llu", static_cast<unsigned long long>(terrain->GetHeightSampleCount()));
        property_text("Height Samples", stat_buf);

        std::snprintf(stat_buf, sizeof(stat_buf), "%llu", static_cast<unsigned long long>(terrain->GetVertexCount()));
        property_text("Vertices", stat_buf);

        std::snprintf(stat_buf, sizeof(stat_buf), "%llu", static_cast<unsigned long long>(terrain->GetIndexCount()));
        property_text("Indices", stat_buf);

        //= MAP =================================================
        if (min_y != terrain->GetMinY()) terrain->SetMinY(min_y);
        if (max_y != terrain->GetMaxY()) terrain->SetMaxY(max_y);
        //=======================================================
    }
    component_end();
}

void Properties::ShowAudioSource(spartan::AudioSource* audio_source) const
{
    if (!audio_source)
        return;

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
            audio_source->SetAudioClip(std::get<const char*>(payload->data));
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
        if (mute != audio_source->GetMute())                       audio_source->SetMute(mute);
        if (play_on_start != audio_source->GetPlayOnStart())       audio_source->SetPlayOnStart(play_on_start);
        if (loop != audio_source->GetLoop())                       audio_source->SetLoop(loop);
        if (is_3d != audio_source->GetIs3d())                      audio_source->SetIs3d(is_3d);
        if (volume != audio_source->GetVolume())                   audio_source->SetVolume(volume);
        if (pitch != audio_source->GetPitch())                     audio_source->SetPitch(pitch);
        if (reverb_enabled != audio_source->GetReverbEnabled())    audio_source->SetReverbEnabled(reverb_enabled);
        if (reverb_room_size != audio_source->GetReverbRoomSize()) audio_source->SetReverbRoomSize(reverb_room_size);
        if (reverb_decay != audio_source->GetReverbDecay())        audio_source->SetReverbDecay(reverb_decay);
        if (reverb_wet != audio_source->GetReverbWet())            audio_source->SetReverbWet(reverb_wet);
        //===============================================================================================
    }
    component_end();
}

void Properties::ShowVolume(spartan::Volume* volume) const
{
    if (!volume)
        return;

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
                    continue;

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
}

void Properties::ComponentContextMenu_Add() const
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(design::spacing_md, design::spacing_md));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(design::spacing_md, design::spacing_sm));

    if (ImGui::BeginPopup("##ComponentContextMenu_Add"))
    {
        if (Entity* entity = get_selected_entity())
        {
            // rendering
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
            ImGui::TextUnformatted("RENDERING");
            ImGui::PopStyleColor();
            ImGui::Separator();

            if (ImGui::MenuItem("Camera"))
            {
                entity->AddComponent<Camera>();
            }

            if (ImGui::MenuItem("Renderable"))
            {
                entity->AddComponent<Renderable>();
            }

            if (ImGui::MenuItem("Terrain"))
            {
                entity->AddComponent<Terrain>();
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
