/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ===============================
#include "pch.h"
#include "Properties.h"
#include "commands/CommandStack.h"
#include "../EditorHistory.h"
#include "Window.h"
#include "FileDialog.h"
#include "../imgui/ImGui_EditorUi.h"
#include "../imgui/ImGui_Extension.h"
#include "../imgui/ImGui_Properties.h"
#include "../imgui/source/imgui_stdlib.h"
#include "../widgets/ButtonColorPicker.h"
#include "core/Engine.h"
#include "world/Entity.h"
#include "rendering/Material.h"
#include "world/components/Render.h"
#include "world/components/Physics.h"
#include "world/components/Light.h"
#include "world/components/AudioSource.h"
#include "world/components/Spline.h"
#include "world/components/SplineFollower.h"
#include "world/components/Pedestrians.h"
#include "world/components/Navigation.h"
#include "world/components/Terrain.h"
#include "world/WorldHelpers.h"
#include "world/Weather.h"
#include "core/ThreadPool.h"
#include "world/components/Camera.h"
#include "world/components/Volume.h"
#include "rendering/Renderer.h"
#include "resource/IResource.h"
#include "rhi/RHI_Texture.h"
#include "world/components/Script.h"
#include "world/components/ParticleSystem.h"
#include "world/components/Water.h"
#include "world/components/SpawnPoint.h"
#include "world/components/CarReset.h"
#include "world/components/Text3D.h"
#include "world/Prefab.h"
#include "TerrainEditor.h"
#include "WorldEnvironment.h"
#include "../Editor.h"
#include "../../../data/shaders/shared_lighting.h"
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

            Entity* target = World::GetCamera() ? World::GetCamera()->GetSelectedEntity() : nullptr;
            const uint64_t id = target ? target->GetObjectId() : 0;
            const uint64_t epoch = CommandStack::Epoch();
            auto material = inspected_material.lock();
            if (!material && target)
                if (auto render = target->GetComponent<Render>(); render && render->GetMaterial())
                    material = std::static_pointer_cast<Material>(render->GetMaterial()->shared_from_this());
            callback = [on_selected, id, epoch, material](const string& path)
            {
                if (epoch != CommandStack::Epoch()) return;
                auto entity = World::GetEntityById(id);
                if (id && !entity) return;
                std::unique_ptr<editor_history::EntityScope> entity_history;
                std::unique_ptr<editor_history::MaterialScope> material_history;
                if (entity) entity_history = std::make_unique<editor_history::EntityScope>(entity, true);
                if (material) material_history = std::make_unique<editor_history::MaterialScope>(material.get());
                on_selected(path);
            };
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
            const float height = ImGui::GetFrameHeight();
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
            bool clicked = ImGuiSp::button("...", ImVec2(0.0f, height));
            ImGui::PopStyleVar();
            ImGui::PopID();

            return clicked;
        }
    }

    // color pickers
    std::unique_ptr<ButtonColorPicker> color_picker_material;
    std::unique_ptr<ButtonColorPicker> color_picker_light;
    std::unique_ptr<ButtonColorPicker> color_picker_particle_start;
    std::unique_ptr<ButtonColorPicker> color_picker_particle_end;

    // context menu state
    string context_menu_id;
    std::string copied_component;
    ComponentType copied_component_type = ComponentType::Max;

    // deferred component removal - storing the id prevents a use-after-free
    // crash when the component is destroyed while its Show* function is still on the stack
    uint64_t pending_removal_id    = 0;
    Entity*  pending_removal_owner = nullptr;

    // component content tracking
    bool component_content_active = false;

    // one line of state drawn on the next component header, so a closed component still reports itself
    string next_component_summary;
    ImVec4 next_component_summary_tint = ImVec4(0, 0, 0, 0);

    void component_summary(const string& summary, const ImVec4& tint = ImVec4(0, 0, 0, 0))
    {
        next_component_summary      = summary;
        next_component_summary_tint = tint;
    }

    // expand requests from the editor bridge, consumed by the next inspector frame
    string expand_request;
    bool expand_request_open     = true;
    bool expand_request_consumed = false;
    bool expand_forcing_folds    = false;

    // spacing, accent colors, property rows and the section header all come from the shared kit so
    // the inspector and the terrain window cannot drift apart
    using namespace editor_ui;

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
                pugi::xml_document document;
                auto node = document.append_child("component");
                component->Save(node);
                copied_component = editor_history::Xml(node);
                copied_component_type = component->GetType();
            }

            ImGui::BeginDisabled(copied_component.empty() || copied_component_type != component->GetType());
            if (ImGui::MenuItem("Paste Attributes"))
            {
                if (!copied_component.empty() && copied_component_type == component->GetType())
                {
                    pugi::xml_document document;
                    if (document.load_string(copied_component.c_str()))
                    {
                        auto node = document.child("component");
                        if (component->GetType() == ComponentType::Terrain) static_cast<Terrain*>(component)->LoadEditorState(node);
                        else component->Load(node);
                    }
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

        // imgui's own label and arrow stay invisible, the header is drawn below: chevron, the component's color tab, its name
        const float dpi = spartan::Window::GetDpiScale();
        ImGui::PushStyleColor(ImGuiCol_Header, ImGui::EditorUi::alpha(ImGui::Style::color_text, 0.055f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImGui::EditorUi::alpha(ImGui::Style::color_text, 0.085f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImGui::EditorUi::alpha(ImGui::Style::color_text, 0.11f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f * dpi, 7.0f * dpi));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f * dpi);

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_AllowOverlap;
        if (default_open)
        {
            flags |= ImGuiTreeNodeFlags_DefaultOpen;
        }

        const bool expand_all  = expand_request == "all";
        const bool expand_this = !expand_request.empty() && (expand_all || _stricmp(expand_request.c_str(), name) == 0);
        if (expand_this)
        {
            ImGui::SetNextItemOpen(expand_request_open, ImGuiCond_Always);
            expand_request_consumed = true;
            layout::fold_force      = expand_request_open ? 1 : 0;
            expand_forcing_folds    = true;
        }

        const bool is_expanded = ImGuiSp::collapsing_header(name, flags);
        if (expand_this && !expand_all)
        {
            ImGui::SetScrollHereY(0.0f);
        }

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(4);

        const string summary        = next_component_summary;
        const ImVec4 summary_tint   = next_component_summary_tint;
        next_component_summary.clear();
        next_component_summary_tint = ImVec4(0, 0, 0, 0);

        ImVec2 header_min     = ImGui::GetItemRectMin();
        ImVec2 header_max     = ImGui::GetItemRectMax();
        {
            ImDrawList* draw_list  = ImGui::GetWindowDrawList();
            const float center_y   = IM_ROUND((header_min.y + header_max.y) * 0.5f);
            const float font_size  = ImGui::GetFontSize();
            const float chevron    = font_size * 0.55f;
            const bool hovered     = ImGui::IsItemHovered();
            const float open       = ImGui::EditorUi::animate(ImGui::GetID("##chevron"), is_expanded ? 1.0f : 0.0f, 16.0f);
            const ImVec2 arrow_pos = ImVec2(IM_ROUND(header_min.x + 10.0f * dpi), IM_ROUND(center_y - chevron * 0.5f));
            const ImVec4 arrow_tint = hovered || is_expanded ? ImGui::Style::color_text : ImGui::Style::color_text_muted;
            ImGui::EditorUi::draw_chevron(draw_list, ImVec2(arrow_pos.x + chevron * 0.5f, center_y), chevron, open, arrow_tint);

            // the color tab is what makes a component recognisable at a glance, it lights up while the component is open
            const float tab_x = IM_ROUND(arrow_pos.x + chevron + 12.0f * dpi);
            const float tab_w = IM_ROUND(3.0f * dpi);
            const float tab_h = IM_ROUND(font_size * 0.95f);
            const ImVec2 tab_min(tab_x, center_y - tab_h * 0.5f);
            const ImVec2 tab_max(tab_x + tab_w, center_y + tab_h * 0.5f);
            if (open > 0.01f)
            {
                ImGui::EditorUi::draw_glow(draw_list, tab_min, tab_max, accent_color, tab_w, 6.0f * dpi, 0.9f * open);
            }
            draw_list->AddRectFilled(tab_min, tab_max, ImGui::EditorUi::color(ImGui::Style::lerp(ImGui::EditorUi::alpha(accent_color, 0.75f), ImGui::Style::lerp(accent_color, ImVec4(1, 1, 1, 1), 0.2f), open)), tab_w);

            ImFont* font           = Editor::font_bold ? Editor::font_bold : ImGui::GetFont();
            const ImVec4 name_tint = is_expanded ? ImGui::Style::color_text : ImGui::EditorUi::alpha(ImGui::Style::color_text, 0.82f);
            const float name_x = IM_ROUND(tab_x + tab_w + 10.0f * dpi);
            draw_list->AddText(font, font_size, ImVec2(name_x, IM_ROUND(center_y - font_size * 0.5f)), ImGui::EditorUi::color(name_tint), name);

            // the summary sits right aligned before the gear and gives way to the name when space runs out
            if (!summary.empty())
            {
                const float header_height = header_max.y - header_min.y;
                const float gear_space    = options ? header_height + 6.0f * dpi : 10.0f * dpi;
                const float right         = header_max.x - gear_space;
                const float left          = name_x + font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, name).x + 14.0f * dpi;
                const ImVec2 size         = ImGui::CalcTextSize(summary.c_str());
                if (right - left > 24.0f * dpi)
                {
                    const float x     = ImMax(left, right - size.x);
                    const float y     = IM_ROUND(center_y - size.y * 0.5f);
                    const ImVec4 tint = summary_tint.w > 0.0f ? summary_tint : ImGui::EditorUi::alpha(ImGui::Style::color_text_muted, 0.9f);
                    ImGui::PushStyleColor(ImGuiCol_Text, tint);
                    ImGui::RenderTextEllipsis(draw_list, ImVec2(x, y), ImVec2(right, y + size.y), right, summary.c_str(), nullptr, &size);
                    ImGui::PopStyleColor();
                }
            }
        }

        // gear icon for context menu
        if (options)
        {
            // a quiet glyph that only brightens under the pointer, the header's name stays the loudest thing on it
            const float header_height = header_max.y - header_min.y;
            const float icon_size     = IM_ROUND(header_height * 0.52f);
            const float pad           = IM_ROUND((header_height - icon_size) * 0.5f);
            const float r_padding     = 6.0f * dpi;
            const float icon_x        = header_max.x - icon_size - pad * 2.0f - r_padding;
            const float icon_y        = header_min.y;
            const bool gear_hovered   = ImGui::IsMouseHoveringRect(ImVec2(icon_x, icon_y), ImVec2(icon_x + icon_size + pad * 2.0f, header_max.y));
            const ImVec4 gear_tint    = gear_hovered ? ImGui::Style::color_text : ImGui::EditorUi::alpha(ImGui::Style::color_text_muted, 0.8f);

            ImGui::SetCursorScreenPos(ImVec2(icon_x, icon_y));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::EditorUi::alpha(ImGui::Style::color_text, 0.06f));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(pad, pad));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f * dpi);
            if (ImGuiSp::image_button(IconType::Gear, icon_size, false, gear_tint))
            {
                context_menu_id = name;
                ImGui::OpenPopup(context_menu_id.c_str());
            }
            ImGui::PopStyleVar(2);
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

            // content sits on the panel itself, so the value wells are the darkest thing in it
            const ImVec4 content_bg = ImVec4(0, 0, 0, 0);

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
        if (expand_forcing_folds)
        {
            layout::fold_force   = -1;
            expand_forcing_folds = false;
        }
        ImGui::PopID();
        ImGui::Dummy(ImVec2(0, design::spacing_sm));
    }

    //----------------------------------------------------------
    // custom property widgets, the rest of the kit lives in ImGui_Properties.h
    //----------------------------------------------------------

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

        // three fields sharing the value column, each carries its axis as a colored cap on its left end
        const float dpi         = spartan::Window::GetDpiScale();
        const float between     = 6.0f * dpi;
        const float input_width = (ImGui::GetContentRegionAvail().x - between * 2.0f) / 3.0f;
        const char* axis[3]     = { "X", "Y", "Z" };
        float* values[3]        = { &vec.x, &vec.y, &vec.z };

        for (int i = 0; i < 3; ++i)
        {
            if (i > 0)
            {
                ImGui::SameLine(0, between);
            }

            ImGui::PushItemWidth(input_width);
            ImGui::PushID(i);
            ImGuiSp::draw_float_wrap("##v", values[i], 0.01f);
            const bool active = ImGui::IsItemActive();
            ImGui::PopID();
            ImGui::PopItemWidth();

            const ImVec2 field_min = ImGui::GetItemRectMin();
            const ImVec2 field_max = ImGui::GetItemRectMax();
            const float cap_width  = IM_ROUND((field_max.y - field_min.y) * 0.8f);
            const ImVec4 tint      = ImGui::EditorUi::axis_color(i);
            ImDrawList* draw_list  = ImGui::GetWindowDrawList();
            draw_list->AddRectFilled(field_min, ImVec2(field_min.x + cap_width, field_max.y), ImGui::EditorUi::color(ImGui::EditorUi::alpha(tint, active ? 0.50f : 0.28f)), ImGui::GetStyle().FrameRounding, ImDrawFlags_RoundCornersLeft);
            ImFont* font            = Editor::font_bold ? Editor::font_bold : ImGui::GetFont();
            const float font_size   = ImGui::GetFontSize();
            const float letter_w    = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, axis[i]).x;
            const ImVec2 letter_pos = ImVec2(IM_ROUND(field_min.x + (cap_width - letter_w) * 0.5f), IM_ROUND(field_min.y + (field_max.y - field_min.y - font_size) * 0.5f));
            draw_list->AddText(font, font_size, letter_pos, ImGui::EditorUi::color(ImGui::Style::lerp(tint, ImVec4(1.0f, 1.0f, 1.0f, 1.0f), 0.25f)), axis[i]);
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

        const float browse_width = ImGui::CalcTextSize("...").x + 8.0f;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - browse_width - design::spacing_sm);
        ImGui::InputText(("##" + string(label)).c_str(), name, ImGuiInputTextFlags_ReadOnly);
        ImGui::EditorUi::decorate_field();

        ImGui::SameLine(0, design::spacing_sm);

        if (file_selection::browse_button(("browse_" + string(label)).c_str()))
        {
            file_selection::open(on_browse);
            return true;
        }

        return false;
    }

    //----------------------------------------------------------
    // shared visual helpers
    //----------------------------------------------------------

    ImVec4 to_imvec(const Color& color, const float alpha = 1.0f)
    {
        return ImVec4(color.r, color.g, color.b, alpha);
    }

    // brightest channel lifted to one, a light's hue without its energy so it can be drawn
    ImVec4 hue_of(const Color& color)
    {
        const float peak = ImMax(ImMax(color.r, color.g), ImMax(color.b, 0.0001f));
        return ImVec4(color.r / peak, color.g / peak, color.b / peak, 1.0f);
    }

    // a field that shares its row with a small trailing button, true when the button is pressed
    float trailing_button_width(const char* label)
    {
        return ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    }

    bool trailing_button(const char* label, const char* tooltip)
    {
        ImGui::SameLine(0, design::spacing_sm);
        const bool pressed = ImGuiSp::button(label);
        if (tooltip)
        {
            ImGuiSp::tooltip(tooltip);
        }
        return pressed;
    }

    // moves the cursor to the value column without spending a row on an empty label
    void value_column()
    {
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
        ImGui::SameLine(layout::label_width());
    }

    //----------------------------------------------------------
    // material visuals
    //----------------------------------------------------------

    const char* roughness_word(const float roughness)
    {
        if (roughness < 0.08f) return "mirror";
        if (roughness < 0.25f) return "glossy";
        if (roughness < 0.50f) return "satin";
        if (roughness < 0.75f) return "matte";
        return "rough";
    }

    const char* metalness_word(const float metalness)
    {
        if (metalness <= 0.01f) return "non-metal";
        if (metalness >= 0.99f) return "metal";
        return "part metal";
    }

    const char* ior_word(const float ior)
    {
        struct Medium
        {
            float ior;
            const char* name;
        };
        static const Medium media[] = { { 1.0f, "air" }, { 1.33f, "water" }, { 1.376f, "eye" }, { 1.5f, "glass" }, { 1.77f, "sapphire" }, { 2.42f, "diamond" } };
        const Medium* best = &media[0];
        for (const Medium& medium : media)
        {
            if (fabsf(medium.ior - ior) < fabsf(best->ior - ior))
            {
                best = &medium;
            }
        }
        return best->name;
    }

    // what the material is at a glance, the first word of its summary and its identity chip
    const char* material_kind(Material* material)
    {
        if (material->GetProperty(MaterialProperty::EmissiveFromAlbedo) > 0.0f || material->HasTextureOfType(MaterialTextureType::Emission))
        {
            return "Emissive";
        }
        if (material->IsTransparent())
        {
            return material->GetProperty(MaterialProperty::Ior) > 1.01f ? "Glass" : "Transparent";
        }
        return material->GetProperty(MaterialProperty::Metalness) >= 0.5f ? "Metal" : "Non-metal";
    }

    uint32_t material_texture_count(Material* material)
    {
        uint32_t count = 0;
        for (uint32_t type = 0; type < static_cast<uint32_t>(MaterialTextureType::Packed); type++)
        {
            count += material->HasTextureOfType(static_cast<MaterialTextureType>(type)) ? 1 : 0;
        }
        return count;
    }

    // a lit ball drawn with the material's own color, roughness, metalness, clearcoat, opacity and glow,
    // an approximation that answers "what does this look like" without a render target
    void material_ball(ImDrawList* draw_list, const ImVec2& center, const float radius, Material* material)
    {
        const Color base        = Color(material->GetProperty(MaterialProperty::ColorR), material->GetProperty(MaterialProperty::ColorG), material->GetProperty(MaterialProperty::ColorB), 1.0f);
        const float opacity     = ImClamp(material->GetProperty(MaterialProperty::ColorA), 0.0f, 1.0f);
        const float roughness   = ImClamp(material->GetProperty(MaterialProperty::Roughness), 0.0f, 1.0f);
        const float metalness   = ImClamp(material->GetProperty(MaterialProperty::Metalness), 0.0f, 1.0f);
        const float clearcoat   = ImClamp(material->GetProperty(MaterialProperty::Clearcoat), 0.0f, 1.0f);
        const float emission    = material->GetProperty(MaterialProperty::EmissiveFromAlbedo);
        RHI_Texture* albedo_map = material->GetTexture(MaterialTextureType::Color);
        const ImVec2 min        = ImVec2(center.x - radius, center.y - radius);
        const ImVec2 max        = ImVec2(center.x + radius, center.y + radius);
        const ImVec2 light_dir  = ImVec2(-0.55f, -0.62f);

        // a checkerboard behind the ball is the universal sign for see-through
        if (opacity < 0.999f)
        {
            const float cell = ImMax(radius / 4.0f, 3.0f);
            draw_list->PushClipRect(min, max, true);
            for (int y = 0; y < 8; y++)
            {
                for (int x = 0; x < 8; x++)
                {
                    const ImVec2 a = ImVec2(min.x + x * cell, min.y + y * cell);
                    draw_list->AddRectFilled(a, ImVec2(a.x + cell, a.y + cell), ImGui::EditorUi::color(((x + y) & 1) ? ImVec4(0.32f, 0.32f, 0.34f, 1.0f) : ImVec4(0.18f, 0.18f, 0.20f, 1.0f)));
                }
            }
            draw_list->PopClipRect();
        }

        if (emission > 0.0f)
        {
            const float strength = ImClamp(0.35f + logf(1.0f + emission * 100.0f) * 0.15f, 0.0f, 1.0f);
            ImGui::EditorUi::draw_glow(draw_list, min, max, hue_of(base), radius, radius * 0.45f, strength);
        }

        // metals have no diffuse, their color lives in the reflection
        const float diffuse_scale = 1.0f - metalness * 0.8f;
        const ImVec4 diffuse      = ImVec4(base.r * diffuse_scale, base.g * diffuse_scale, base.b * diffuse_scale, ImMax(opacity, 0.15f));
        if (albedo_map)
        {
            draw_list->AddImageRounded(reinterpret_cast<ImTextureID>(albedo_map), min, max, ImVec2(0, 0), ImVec2(1, 1), ImGui::EditorUi::color(diffuse), radius);
        }
        else
        {
            draw_list->AddCircleFilled(center, radius, ImGui::EditorUi::color(emission > 0.0f ? ImVec4(base.r, base.g, base.b, diffuse.w) : diffuse), 64);
        }

        // form shadow, strokes that darken toward the side facing away from the light
        if (emission <= 0.0f)
        {
            const int steps     = 18;
            const float stroke  = radius / steps * 1.6f;
            for (int i = 0; i < steps; i++)
            {
                const float t      = static_cast<float>(i) / steps;
                const float shift  = t * radius * 0.28f;
                const float ring_r = radius - shift - t * radius * 0.55f - stroke * 0.5f;
                if (ring_r <= 0.0f)
                {
                    break;
                }
                const ImVec2 ring_c = ImVec2(center.x + light_dir.x * shift, center.y + light_dir.y * shift);
                const float shade   = 0.62f * powf(1.0f - t, 1.6f) * ImMax(opacity, 0.4f);
                draw_list->AddCircle(ring_c, ring_r, ImGui::EditorUi::color(ImVec4(0.0f, 0.0f, 0.0f, shade / steps * 4.0f)), 64, stroke);
            }
        }

        // the highlight, small and hot when smooth, wide and faint when rough, tinted by the base color on metals
        {
            const ImVec2 spot     = ImVec2(center.x + light_dir.x * radius * 0.42f, center.y + light_dir.y * radius * 0.42f);
            const float size      = radius * (0.10f + 0.62f * roughness);
            const float peak      = 0.10f + 0.90f * powf(1.0f - roughness, 2.0f) * (0.55f + 0.45f * metalness);
            const ImVec4 tint     = ImVec4(1.0f + (base.r - 1.0f) * metalness, 1.0f + (base.g - 1.0f) * metalness, 1.0f + (base.b - 1.0f) * metalness, 1.0f);
            const int layers      = 10;
            for (int i = 0; i < layers; i++)
            {
                const float t = static_cast<float>(i) / layers;
                draw_list->AddCircleFilled(spot, size * (1.0f - t), ImGui::EditorUi::color(ImVec4(tint.x, tint.y, tint.z, peak / layers * 1.8f)), 32);
            }
        }

        // a clearcoat is a second, glass-sharp reflection sitting on top
        if (clearcoat > 0.0f)
        {
            const ImVec2 spot = ImVec2(center.x + light_dir.x * radius * 0.5f + radius * 0.08f, center.y + light_dir.y * radius * 0.5f);
            draw_list->AddCircleFilled(spot, radius * 0.07f, ImGui::EditorUi::color(ImVec4(1.0f, 1.0f, 1.0f, 0.85f * clearcoat)), 16);
        }

        // fresnel, every surface reflects more at grazing angles, smooth ones show it as a bright rim
        draw_list->AddCircle(center, radius - 1.0f, ImGui::EditorUi::color(ImVec4(1.0f, 1.0f, 1.0f, 0.06f + 0.22f * (1.0f - roughness))), 64, 1.5f);
    }

    //----------------------------------------------------------
    // light visuals
    //----------------------------------------------------------

    struct Landmark
    {
        float value;
        const char* name;
    };

    // a number of lumens means nothing until it sits next to a light bulb, the log axis puts both on one line
    const Landmark landmarks_lumens[] =
    {
        { 12.0f,     "a candle" },
        { 100.0f,    "a flashlight" },
        { 200.0f,    "a 25 W bulb" },
        { 800.0f,    "a 60 W bulb" },
        { 1600.0f,   "a 100 W bulb" },
        { 2600.0f,   "a 150 W bulb" },
        { 8500.0f,   "a 500 W flood light" },
        { 200000.0f, "a stadium light" }
    };

    const Landmark landmarks_lux[] =
    {
        { 0.25f,     "full moonlight" },
        { 10.0f,     "twilight" },
        { 1000.0f,   "an overcast day" },
        { 20000.0f,  "daylight in shade" },
        { 100000.0f, "direct sunlight" }
    };

    // the log ruler under an intensity field, every landmark is a click target that snaps to it
    bool intensity_ruler(float* value, const bool lux, const ImVec4& tint)
    {
        const Landmark* marks = lux ? landmarks_lux : landmarks_lumens;
        const int count       = lux ? IM_ARRAYSIZE(landmarks_lux) : IM_ARRAYSIZE(landmarks_lumens);
        const float log_min   = lux ? -1.0f : 0.0f;
        const float log_max   = lux ? 5.3f : 6.0f;

        value_column();
        const float width  = ImGui::GetContentRegionAvail().x;
        const float track  = ImGui::EditorUi::scaled(10.0f);
        const float height = track + ImGui::GetTextLineHeight() + ImGui::EditorUi::scaled(4.0f);
        const ImVec2 min   = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##intensity_ruler", ImVec2(width, height));
        const bool hovered = ImGui::IsItemHovered();
        const bool clicked = ImGui::IsItemClicked();

        const float radius = ImGui::EditorUi::scaled(3.0f);
        const float x0     = min.x + radius;
        const float x1     = min.x + width - radius;
        auto to_x = [&](const float v)
        {
            const float t = (log10f(ImMax(v, 0.0001f)) - log_min) / (log_max - log_min);
            return x0 + ImClamp(t, 0.0f, 1.0f) * (x1 - x0);
        };

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        const float y         = IM_ROUND(min.y + track * 0.5f);
        const float bar       = ImGui::EditorUi::scaled(2.0f);
        const ImU32 dark      = ImGui::EditorUi::color(ImGui::EditorUi::alpha(tint, 0.08f));
        const ImU32 bright    = ImGui::EditorUi::color(ImGui::EditorUi::alpha(tint, 0.85f));
        draw_list->AddRectFilledMultiColor(ImVec2(x0, y - bar * 0.5f), ImVec2(x1, y + bar * 0.5f), dark, bright, bright, dark);

        // the landmark closest to the pointer is the one a click lands on
        int hovered_mark = -1;
        if (hovered)
        {
            float best = ImGui::EditorUi::scaled(12.0f);
            for (int i = 0; i < count; i++)
            {
                const float distance = fabsf(ImGui::GetIO().MousePos.x - to_x(marks[i].value));
                if (distance < best)
                {
                    best         = distance;
                    hovered_mark = i;
                }
            }
        }

        for (int i = 0; i < count; i++)
        {
            const bool hot = i == hovered_mark;
            draw_list->AddCircleFilled(ImVec2(to_x(marks[i].value), y), hot ? radius * 1.5f : radius, ImGui::EditorUi::color(hot ? ImGui::Style::color_text : ImGui::Style::color_text_faint), 12);
        }

        // the current value rides on the ruler as a lit notch
        const float marker_x = to_x(*value);
        ImGui::EditorUi::draw_glow(draw_list, ImVec2(marker_x - 1.0f, y - track * 0.5f), ImVec2(marker_x + 1.0f, y + track * 0.5f), tint, 1.0f, ImGui::EditorUi::scaled(5.0f), 0.8f);
        draw_list->AddRectFilled(ImVec2(marker_x - 1.0f, y - track * 0.5f), ImVec2(marker_x + 1.0f, y + track * 0.5f), ImGui::EditorUi::color(ImGui::Style::color_text), 1.0f);

        // the sentence under the ruler is what the number means
        char text[128];
        ImVec4 text_tint = ImGui::Style::color_text_muted;
        if (*value <= 0.0f)
        {
            snprintf(text, sizeof(text), "Emits no light");
            text_tint = design::warning();
        }
        else
        {
            const float log_value = log10f(*value);
            int nearest = 0;
            for (int i = 1; i < count; i++)
            {
                if (fabsf(log10f(marks[i].value) - log_value) < fabsf(log10f(marks[nearest].value) - log_value))
                {
                    nearest = i;
                }
            }

            if (fabsf(log10f(marks[nearest].value) - log_value) < 0.06f)
            {
                snprintf(text, sizeof(text), "About %s", marks[nearest].name);
            }
            else if (*value < marks[0].value)
            {
                snprintf(text, sizeof(text), "Dimmer than %s", marks[0].name);
            }
            else if (*value > marks[count - 1].value)
            {
                snprintf(text, sizeof(text), "Brighter than %s", marks[count - 1].name);
            }
            else
            {
                int below = 0;
                while (below + 1 < count && marks[below + 1].value <= *value)
                {
                    below++;
                }
                snprintf(text, sizeof(text), "Between %s and %s", marks[below].name, marks[below + 1].name);
            }
        }
        draw_list->AddText(ImVec2(min.x, min.y + track + ImGui::EditorUi::scaled(2.0f)), ImGui::EditorUi::color(text_tint), text);

        if (hovered_mark >= 0)
        {
            ImGui::SetTooltip("%s, %s %s\nclick to use", marks[hovered_mark].name, format::grouped(marks[hovered_mark].value).c_str(), lux ? "lux" : "lumens");
            if (clicked)
            {
                *value = marks[hovered_mark].value;
                return true;
            }
        }

        return false;
    }

    // a side view of where a local light's energy goes: the throw fades with inverse square out to the
    // range, the cone is drawn at its real angle, and the lux readouts say how bright a surface would be
    void light_reach(Light* light)
    {
        const LightType type = light->GetLightType();
        const float range    = light->GetRange();
        const float sensible = light->GetRangeSensible();
        const float extent   = ImMax(ImMax(range, sensible) * 1.08f, 1.0f);

        ImVec2 min, max;
        ImDrawList* draw_list = canvas("##light_reach", ImGui::EditorUi::scaled(104.0f), &min, &max);
        const float pad       = ImGui::EditorUi::scaled(10.0f);
        const float source_x  = min.x + pad + ImGui::EditorUi::scaled(6.0f);
        const float center_y  = IM_ROUND((min.y + max.y) * 0.5f);
        const float axis_x1   = max.x - pad;
        const float half_max  = (max.y - min.y) * 0.5f - pad;
        auto to_x = [&](const float distance)
        {
            return source_x + (axis_x1 - source_x) * ImClamp(distance / extent, 0.0f, 1.0f);
        };

        // brightness is judged on a log scale between the quarter lux the engine fades to and a close surface
        const float e_min   = 0.25f;
        const float e_ref   = ImMax(light->GetIlluminanceAt(ImMax(extent * 0.02f, 0.05f)), e_min * 2.0f);
        auto fade = [&](const float distance)
        {
            const float e = light->GetIlluminanceAt(ImMax(distance, 0.001f));
            return ImClamp(logf(ImMax(e, e_min) / e_min) / logf(e_ref / e_min), 0.0f, 1.0f);
        };

        const float spread  = type == LightType::Spot ? ImClamp(light->GetAngle(), 0.02f, 1.45f) : 1.45f;
        const ImVec4 hue    = hue_of(light->GetColor());
        const float range_x = to_x(range);
        const int slices    = 64;
        for (int i = 0; i < slices; i++)
        {
            const float xa = source_x + (range_x - source_x) * (static_cast<float>(i) / slices);
            const float xb = source_x + (range_x - source_x) * (static_cast<float>(i + 1) / slices);
            const float da = (xa - source_x) / (axis_x1 - source_x) * extent;
            const float ha = ImMin(tanf(spread) * (xa - source_x) + (type == LightType::Area ? half_max * 0.35f : 0.0f), half_max);
            const float hb = ImMin(tanf(spread) * (xb - source_x) + (type == LightType::Area ? half_max * 0.35f : 0.0f), half_max);
            const ImU32 c  = ImGui::EditorUi::color(ImGui::EditorUi::alpha(hue, 0.55f * fade(da)));
            draw_list->AddQuadFilled(ImVec2(xa, center_y - ha), ImVec2(xb, center_y - hb), ImVec2(xb, center_y + hb), ImVec2(xa, center_y + ha), c);
        }

        // the outline keeps the shape readable when the light is dim or off
        if (type != LightType::Point)
        {
            const float base     = type == LightType::Area ? half_max * 0.35f : 0.0f;
            const float edge_h   = ImMin(tanf(spread) * (range_x - source_x) + base, half_max);
            const float bend_x   = source_x + (tanf(spread) > 0.0f ? ImMin((half_max - base) / tanf(spread), range_x - source_x) : range_x - source_x);
            const ImU32 edge     = ImGui::EditorUi::color(ImGui::EditorUi::alpha(hue, 0.45f));
            for (const float sign : { -1.0f, 1.0f })
            {
                draw_list->AddLine(ImVec2(source_x, center_y + sign * base), ImVec2(bend_x, center_y + sign * ImMin(tanf(spread) * (bend_x - source_x) + base, half_max)), edge, 1.0f);
                draw_list->AddLine(ImVec2(bend_x, center_y + sign * ImMin(tanf(spread) * (bend_x - source_x) + base, half_max)), ImVec2(range_x, center_y + sign * edge_h), edge, 1.0f);
            }
        }

        // the emitter itself
        if (type == LightType::Area)
        {
            const float h = half_max * 0.35f;
            draw_list->AddRectFilled(ImVec2(source_x - ImGui::EditorUi::scaled(3.0f), center_y - h), ImVec2(source_x, center_y + h), ImGui::EditorUi::color(hue), 1.0f);
        }
        else
        {
            ImGui::EditorUi::draw_glow(draw_list, ImVec2(source_x - 2.0f, center_y - 2.0f), ImVec2(source_x + 2.0f, center_y + 2.0f), hue, 2.0f, ImGui::EditorUi::scaled(9.0f), 1.0f);
            draw_list->AddCircleFilled(ImVec2(source_x, center_y), ImGui::EditorUi::scaled(4.0f), ImGui::EditorUi::color(ImVec4(1, 1, 1, 1)), 16);
        }

        // the range is a hard stop, so it gets a hard line
        const ImU32 line_color = ImGui::EditorUi::color(ImGui::EditorUi::alpha(ImGui::Style::color_text, 0.55f));
        for (float y = min.y + pad; y < max.y - pad; y += ImGui::EditorUi::scaled(6.0f))
        {
            draw_list->AddLine(ImVec2(range_x, y), ImVec2(range_x, ImMin(y + ImGui::EditorUi::scaled(3.0f), max.y - pad)), line_color, 1.0f);
        }

        char text[96];
        const bool is_auto = fabsf(range - sensible) <= ImMax(0.01f, sensible * 0.001f);
        snprintf(text, sizeof(text), is_auto ? "%.1f m, auto" : "%.1f m", range);
        const ImVec2 size = ImGui::CalcTextSize(text);
        const float text_x = ImClamp(range_x - size.x * 0.5f, min.x + pad, max.x - pad - size.x);
        draw_list->AddText(ImVec2(text_x, min.y + ImGui::EditorUi::scaled(4.0f)), ImGui::EditorUi::color(ImGui::Style::color_text), text);

        // what a surface facing the light receives at a near and a far distance
        const float near_distance = ImMax(ImMin(1.0f, range * 0.5f), 0.1f);
        const float far_distance  = range * 0.5f > near_distance * 1.5f ? range * 0.5f : range;
        snprintf(text, sizeof(text), "%s lx at %.1f m", format::grouped(light->GetIlluminanceAt(near_distance)).c_str(), near_distance);
        draw_list->AddText(ImVec2(min.x + pad, max.y - pad - ImGui::GetTextLineHeight()), ImGui::EditorUi::color(ImGui::Style::color_text_muted), text);
        snprintf(text, sizeof(text), "%.1f lx at %.1f m", light->GetIlluminanceAt(far_distance), far_distance);
        const ImVec2 far_size = ImGui::CalcTextSize(text);
        const float far_x     = to_x(far_distance);
        const float gap       = ImGui::EditorUi::scaled(6.0f);
        float far_text_x      = far_x - far_size.x * 0.5f;
        if (far_text_x + far_size.x > range_x - gap && far_text_x < range_x + gap)
        {
            far_text_x = range_x - gap - far_size.x;
        }
        far_text_x = ImClamp(far_text_x, min.x + pad + ImGui::CalcTextSize("0 lx at 0.0 m").x + gap * 2.0f, max.x - pad - far_size.x);
        const float tick_y = max.y - pad - ImGui::GetTextLineHeight() - ImGui::EditorUi::scaled(3.0f);
        draw_list->AddLine(ImVec2(far_x, tick_y - ImGui::EditorUi::scaled(4.0f)), ImVec2(far_x, tick_y), line_color, 1.0f);
        draw_list->AddText(ImVec2(far_text_x, max.y - pad - ImGui::GetTextLineHeight()), ImGui::EditorUi::color(ImGui::Style::color_text_muted), text);
    }
}

Properties::Properties(Editor* editor) : Widget(editor)
{
    m_title          = "Properties";
    m_dock           = WidgetDock::RightDown;
    m_size_initial.x = 500;

    color_picker_light          = make_unique<ButtonColorPicker>("Light Color Picker");
    color_picker_material      = make_unique<ButtonColorPicker>("Material Color Picker");
    color_picker_particle_start = make_unique<ButtonColorPicker>("Particle Start Color");
    color_picker_particle_end   = make_unique<ButtonColorPicker>("Particle End Color");

    file_selection::initialize(editor);
}

void Properties::OnTickVisible()
{
    bool is_in_game_mode = spartan::Engine::IsFlagSet(spartan::EngineMode::Playing);
    if (is_in_game_mode)
    {
        ImGui::TextColored(ImGui::Style::color_warning, "Read-only during playback");
        ImGui::TextDisabled("Stop playback to edit components.");
        ImGui::Separator();
    }
    ImGui::BeginDisabled(is_in_game_mode);
    {
        uint32_t selected_count = get_selected_entity_count();

        if (selected_count > 1)
        {
            // multiple entities selected - show summary
            ImGui::Dummy(ImVec2(0, design::spacing_md));

            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::Style::color_accent_1);
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

        }
        else if (Entity* entity = get_selected_entity())
        {
            editor_history::EntityScope history(entity, true);
            // push entity id so each entity gets its own collapse state for components
            ImGui::PushID(static_cast<int>(entity->GetObjectId()));

            ShowEntity(entity);
            ShowScript(entity->GetComponent<Script>());
            ShowLight(entity->GetComponent<Light>());
            ShowCamera(entity->GetComponent<Camera>());
            ShowTerrain(entity->GetComponent<Terrain>());
            ShowSpline(entity->GetComponent<Spline>());
            ShowSplineFollower(entity->GetComponent<SplineFollower>());
            ShowPedestrians(entity->GetComponent<Pedestrians>());
            ShowNavigation(entity->GetComponent<Navigation>());
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
            ImGui::Dummy(ImVec2(0, ImGui::EditorUi::scaled(24.0f)));
            ImGui::EditorUi::panel_header("Nothing selected", "Select an entity in the viewport or scene hierarchy to inspect its components.", Editor::font_bold);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::Style::color_text_muted);
            ImGui::TextWrapped("Double-click an entity to focus it.");
            ImGui::TextWrapped("Ctrl + click to select multiple entities.");
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndDisabled();

    // a request lives for exactly one inspector frame, a component that is not there just ignores it
    expand_request.clear();
    expand_request_consumed = false;

    // handle file browser dialog
    file_selection::tick();
}

void Properties::RequestExpand(const string& component, const bool open)
{
    expand_request      = component;
    expand_request_open = open;
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
    // the header says where the entity lives, in the world or under a parent
    {
        Entity* parent = entity->GetParent();
        string summary = parent ? "child of " + parent->GetObjectName() : "";
        if (entity->GetChildrenCount() > 0)
        {
            const uint32_t children = entity->GetChildrenCount();
            summary += (summary.empty() ? "" : " \xC2\xB7 ") + to_string(children) + (children == 1 ? " child" : " children");
        }
        if (!entity->GetActive())
        {
            summary = "inactive" + (summary.empty() ? "" : " \xC2\xB7 " + summary);
        }
        component_summary(summary, entity->GetActive() ? ImVec4(0, 0, 0, 0) : ImGui::Style::color_warning);
    }

    if (component_begin("Transform", design::accent_entity(), nullptr, true, false, true))
    {
        // entity name display
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::Style::color_text);
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
            if (is_code)
            {
                layout::note("Defined in code. Transform edits plus components and children you add are saved as overrides and re-applied on load.", ImGui::Style::color_text_muted);
            }
            else
            {
                layout::note("Transform edits plus components and children you add are saved as overrides. Update Prefab bakes the current hierarchy into the .prefab file.", ImGui::Style::color_text_muted);
            }

            // prefab action buttons, tinted from the theme so they follow it
            layout::separator();

            // update prefab (for file prefabs only, code prefabs have no file to write)
            if (is_file)
            {
                float button_width = ImGui::GetContentRegionAvail().x;
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::EditorUi::alpha(ImGui::Style::color_ok, 0.28f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::EditorUi::alpha(ImGui::Style::color_ok, 0.40f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::EditorUi::alpha(ImGui::Style::color_ok, 0.22f));
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
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::EditorUi::alpha(ImGui::Style::color_error, 0.24f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::EditorUi::alpha(ImGui::Style::color_error, 0.36f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::EditorUi::alpha(ImGui::Style::color_error, 0.18f));
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

        // a child's numbers are relative to its parent, which is the first thing that confuses anyone moving one
        if (Entity* parent = entity->GetParent())
        {
            const Vector3 world = entity->GetPosition();
            char text[256];
            snprintf(text, sizeof(text), "Relative to %s. In the world it sits at %.2f, %.2f, %.2f.", parent->GetObjectName().c_str(), world.x, world.y, world.z);
            layout::note(text, ImGui::Style::color_text_muted);
        }
    }
    component_end();
}

void Properties::ShowScript(spartan::Script* script) const
{
    if (!script)
    {
        return;
    }

    {
        const bool loaded = script->script.valid();
        const string file = FileSystem::GetFileNameFromFilePath(script->file_path);
        component_summary(loaded ? file : (script->file_path.empty() ? "no file" : "not loaded"), loaded || script->file_path.empty() ? ImVec4(0, 0, 0, 0) : ImGui::Style::color_warning);
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

        // status as a colored line, a green dot needs no reading, a red one says what to do
        const bool is_loaded = script->script.valid();
        if (is_loaded)
        {
            layout::note("Loaded. Values below are the script's globals, edits apply live and are lost on reload.", ImGui::Style::color_ok);
        }
        else if (script->file_path.empty())
        {
            layout::note("Pick a .lua file, or drop one here from the asset browser.", ImGui::Style::color_text_muted);
        }
        else
        {
            layout::note("The file did not load, check the console for the Lua error and reload.", ImGui::Style::color_error);
        }

        // the script's globals as ordinary rows, so a script reads like any other component; functions and tables are its code, not its settings
        if (is_loaded)
        {
            vector<string> code_members;
            for (auto&& [K, V] : script->script)
            {
                const std::string key = K.as<std::string>();
                ImGui::PushID(key.c_str());

                if (V.is<bool>())
                {
                    bool value = V.as<bool>();
                    if (property_toggle(key.c_str(), &value))
                    {
                        script->script[K] = value;
                    }
                }
                else if (V.is<int>())
                {
                    int value = V.as<int>();
                    layout::begin_property(key.c_str());
                    if (ImGui::DragInt("##value", &value))
                    {
                        script->script[K] = value;
                    }
                    ImGui::EditorUi::decorate_field();
                }
                else if (V.is<float>() || V.is<double>())
                {
                    float value = V.as<float>();
                    layout::begin_property(key.c_str());
                    if (ImGui::DragFloat("##value", &value, 0.01f, 0.0f, 0.0f, "%.3f"))
                    {
                        script->script[K] = value;
                    }
                    ImGui::EditorUi::decorate_field();
                }
                else if (V.is<std::string>())
                {
                    std::string value = V.as<std::string>();
                    layout::begin_property(key.c_str());
                    if (ImGui::InputText("##value", &value))
                    {
                        script->script[K] = value;
                    }
                    ImGui::EditorUi::decorate_field();
                }
                else
                {
                    code_members.push_back(key);
                }

                ImGui::PopID();
            }

            if (!code_members.empty())
            {
                sort(code_members.begin(), code_members.end());
                string text = "Also defines ";
                for (size_t i = 0; i < code_members.size(); i++)
                {
                    text += (i == 0 ? "" : ", ") + code_members[i];
                }
                text += ".";
                layout::note(text.c_str(), ImGui::Style::color_text_faint);
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

    const LightType type      = light->GetLightType();
    const bool is_directional = type == LightType::Directional;
    {
        char summary[128];
        const string intensity_text = format::grouped(light->GetIntensityPhotometric());
        if (is_directional)
        {
            const float time = World::GetTimeOfDay(light->GetFlag(LightFlags::DayNightCycle) && light->GetFlag(LightFlags::RealTimeCycle));
            const int minutes = static_cast<int>(time * 1440.0f) % 1440;
            snprintf(summary, sizeof(summary), "Sun \xC2\xB7 %02d:%02d UTC \xC2\xB7 %s lux", minutes / 60, minutes % 60, intensity_text.c_str());
        }
        else if (type == LightType::Spot)
        {
            snprintf(summary, sizeof(summary), "Spot \xC2\xB7 %.0f\xC2\xB0 \xC2\xB7 %s lm \xC2\xB7 %.1f m", light->GetAngle() * math::rad_to_deg * 2.0f, intensity_text.c_str(), light->GetRange());
        }
        else if (type == LightType::Area)
        {
            snprintf(summary, sizeof(summary), "Area \xC2\xB7 %.1f \xC3\x97 %.1f m \xC2\xB7 %s lm", light->GetAreaWidth(), light->GetAreaHeight(), intensity_text.c_str());
        }
        else
        {
            snprintf(summary, sizeof(summary), "Point \xC2\xB7 %s lm \xC2\xB7 %.1f m", intensity_text.c_str(), light->GetRange());
        }
        component_summary(summary, light->GetIntensityPhotometric() <= 0.0f ? design::warning() : ImVec4(0, 0, 0, 0));
    }

    if (component_begin("Light", design::accent_light(), light))
    {
        //= REFLECT ==========================================================================
        float intensity           = light->GetIntensityPhotometric();
        float temperature_kelvin  = light->GetTemperature();
        float angle               = light->GetAngle() * math::rad_to_deg * 2.0f;
        bool shadows              = light->GetFlag(spartan::LightFlags::Shadows);
        bool shadows_screen_space = light->GetFlag(spartan::LightFlags::ShadowsScreenSpace);
        float range               = light->GetRange();
        float area_width          = light->GetAreaWidth();
        float area_height         = light->GetAreaHeight();
        color_picker_light->SetColor(light->GetColor());
        //====================================================================================

        // four types, all visible at once, switching is one click and the current one is never hidden in a list
        static vector<string> types = { "Directional", "Point", "Spot", "Area" };
        uint32_t type_index = static_cast<uint32_t>(type);
        if (property_segmented("Type", types, &type_index, "directional is the sun, point shines every way, spot is a cone, area is a glowing rectangle"))
        {
            light->SetLightType(static_cast<LightType>(type_index));
            intensity = light->GetIntensityPhotometric();
            range     = light->GetRange();
        }

        layout::group_spacing();

        if (is_directional)
        {
            property_slider("Intensity", &intensity, 0.1f, 200000.0f, intensity < 10.0f ? "%.2f lux" : "%.0f lux", "top of atmosphere illuminance, the atmosphere derives the ground level color and dimming. ctrl click to type a value", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
            intensity_ruler(&intensity, true, design::accent_light());

            property_toggle("Shadows", &shadows, "cast shadows from the sun");
            ImGui::BeginDisabled(!shadows);
            property_toggle("Contact shadows", &shadows_screen_space, "screen space shadows that fill in the small gaps the shadow map is too coarse for");
            ImGui::EndDisabled();

            // the sun follows the world's clock, the sky and the weather, those are edited where the world is
            layout::group_spacing();
            {
                const bool real_time = light->GetFlag(spartan::LightFlags::DayNightCycle) && light->GetFlag(spartan::LightFlags::RealTimeCycle);
                const int minutes    = static_cast<int>(World::GetTimeOfDay(real_time) * 1440.0f) % 1440;
                const float altitude = roundf(Environment::GetSunAltitude(Environment::GetDays(real_time)));
                char where[48];
                if (altitude == 0.0f)
                {
                    snprintf(where, sizeof(where), "on the horizon");
                }
                else
                {
                    snprintf(where, sizeof(where), "%.0f\xC2\xB0 %s the horizon", fabsf(altitude), altitude > 0.0f ? "above" : "below");
                }
                char text[224];
                if (World::GetDirectionalLight() == light)
                {
                    snprintf(text, sizeof(text), "This is the world's sun, the world clock places it, %02d:%02d UTC, %s. Time of day, weather and climate are in Environment.", minutes / 60, minutes % 60, where);
                }
                else
                {
                    snprintf(text, sizeof(text), "Another directional light is the world's sun, this one only adds light. Time of day, weather and climate are in Environment.");
                }
                layout::note(text, design::accent_light());
                ImGui::Dummy(ImVec2(0, design::spacing_xs));
                if (ImGui::Button("Open Environment"))
                {
                    if (WorldEnvironment* environment = m_editor->GetWidget<WorldEnvironment>())
                    {
                        environment->Focus();
                    }
                }
            }
        }
        else
        {
            // where the light goes and how bright it is when it gets there
            light_reach(light);
            layout::group_spacing();

            // color: a temperature for real lamps, a picker for everything else
            property_color("Color", color_picker_light.get(), "the light's tint, setting a temperature below overwrites it with that lamp's color");
            property_slider("Temperature", &temperature_kelvin, 1000.0f, 20000.0f, "%.0f K", "color temperature of a real lamp, 1900 K candle, 2700 K household bulb, 5500 K daylight, 6500 K overcast", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
            {
                // the kelvin scale drawn under the slider, it answers "which way is warmer" without a tooltip
                const ImVec2 slider_min = ImGui::GetItemRectMin();
                const ImVec2 slider_max = ImGui::GetItemRectMax();
                const float strip       = ImGui::EditorUi::scaled(2.0f);
                ImDrawList* draw_list   = ImGui::GetWindowDrawList();
                const int steps         = 24;
                for (int i = 0; i < steps; i++)
                {
                    const float t0 = static_cast<float>(i) / steps;
                    const float t1 = static_cast<float>(i + 1) / steps;
                    const ImU32 c0 = ImGui::EditorUi::color(hue_of(Color(1000.0f * powf(20.0f, t0))));
                    const ImU32 c1 = ImGui::EditorUi::color(hue_of(Color(1000.0f * powf(20.0f, t1))));
                    const float xa = slider_min.x + (slider_max.x - slider_min.x) * t0;
                    const float xb = slider_min.x + (slider_max.x - slider_min.x) * t1;
                    draw_list->AddRectFilledMultiColor(ImVec2(xa, slider_max.y - strip), ImVec2(xb, slider_max.y), c0, c1, c1, c0);
                }
            }

            // intensity
            const char* unit_tooltip = "total emitted luminous flux in lumens";
            if (type == LightType::Spot)
            {
                unit_tooltip = "total beam luminous flux in lumens, the cone angle focuses it";
            }
            else if (type == LightType::Area)
            {
                unit_tooltip = "total one sided luminous flux in lumens";
            }
            property_slider("Intensity", &intensity, 0.0f, 1000000.0f, "%.0f lm", unit_tooltip, ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
            intensity_ruler(&intensity, false, hue_of(light->GetColor()));

            // range, with the engine's own fade distance one click away
            {
                const float sensible = light->GetRangeSensible();
                const bool is_auto   = fabsf(range - sensible) <= ImMax(0.01f, sensible * 0.001f);
                layout::begin_property("Range", "hard cutoff distance, the light stays inverse square until here and then stops. auto follows the distance where it fades to a quarter lux");
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - (is_auto ? 0.0f : trailing_button_width("Auto") + design::spacing_sm));
                ImGui::SliderFloat("##range", &range, 0.1f, 1000.0f, "%.1f m", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
                ImGui::EditorUi::decorate_field();
                if (!is_auto && trailing_button("Auto", "go back to the distance where the light has faded to a quarter lux, it then follows intensity changes again"))
                {
                    range = sensible;
                }

                const float cut = light->GetIlluminanceAt(range);
                if (!is_auto && intensity > 0.0f && cut > 2.0f)
                {
                    char text[160];
                    snprintf(text, sizeof(text), "The light is still %.0f lx where the range cuts it, expect a visible edge. It fades out on its own at %.1f m.", cut, sensible);
                    layout::note(text, design::warning());
                }
            }

            if (type == LightType::Spot)
            {
                property_slider("Cone angle", &angle, 1.0f, 179.0f, "%.0f\xC2\xB0", "full opening angle of the beam, the same lumens spread over a wider cone look dimmer");

                std::string ies_profile = light->GetIesProfile();
                property_resource("IES profile", &ies_profile, "a measured lm-63 distribution, it replaces the soft cone and sets the angle to the profile's extent", [light](const std::string& path)
                {
                    if (FileSystem::GetExtensionFromFilePath(path) == ".ies")
                    {
                        light->SetIesProfile(FileSystem::GetRelativePath(path));
                    }
                });

                // a profile picked this frame moved the angle, keep the map below from writing the old one back
                if (ies_profile != light->GetIesProfile())
                {
                    angle = light->GetAngle() * math::rad_to_deg * 2.0f;
                }
                if (light->GetIesSlot() != 0)
                {
                    char text[160];
                    snprintf(text, sizeof(text), "The file measures %s lm at a %s cd peak. Set the intensity to it for the real fixture's output.", format::grouped(light->GetIesLumens()).c_str(), format::grouped(light->GetIesPeakCandela()).c_str());
                    layout::note(text, design::accent_light());
                }
            }

            if (type == LightType::Area)
            {
                property_slider("Width", &area_width, 0.01f, 100.0f, "%.2f m", "size of the glowing rectangle along the entity's right axis", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
                property_slider("Height", &area_height, 0.01f, 100.0f, "%.2f m", "size of the glowing rectangle along the entity's up axis", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
            }

            property_toggle("Shadows", &shadows, "cast shadows from this light, each shadowed local light costs a shadow map");

            // the distances are performance knobs, they matter when a scene gets heavy, so they stay folded
            float draw_distance       = light->GetDrawDistance();
            float shadow_distance     = light->GetShadowDistance();
            float volumetric_distance = light->GetVolumetricDistance();
            char summary[96];
            snprintf(summary, sizeof(summary), "%.0f \xC2\xB7 %.0f \xC2\xB7 %.0f m", draw_distance, shadow_distance, volumetric_distance);
            if (layout::fold("Culling distances", false, summary))
            {
                if (property_float("Visible up to", &draw_distance, 1.0f, 0.0f, 10000.0f, "beyond this distance from the camera the light is switched off entirely", "%.0f m"))
                {
                    light->SetDrawDistance(draw_distance);
                }
                if (property_float("Shadows up to", &shadow_distance, 1.0f, 0.0f, 10000.0f, "beyond this distance the light keeps shining but stops casting shadows", "%.0f m"))
                {
                    light->SetShadowDistance(shadow_distance);
                }
                if (property_float("Fog glow up to", &volumetric_distance, 1.0f, 0.0f, 10000.0f, "beyond this distance the light stops scattering in fog and mist", "%.0f m"))
                {
                    light->SetVolumetricDistance(volumetric_distance);
                }
                if (shadow_distance > draw_distance || volumetric_distance > draw_distance)
                {
                    layout::note("A distance past the visible limit has no effect, the light is already off there.", ImGui::Style::color_text_muted);
                }
            }
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
        if (!is_directional && temperature_kelvin != light->GetTemperature())
        {
            light->SetTemperature(temperature_kelvin);
            color_picker_light->SetColor(light->GetColor());
        }
        if (!is_directional && color_picker_light->GetColor() != light->GetColor())
        {
            light->SetColor(color_picker_light->GetColor());
        }
        light->SetFlag(spartan::LightFlags::ShadowsScreenSpace, is_directional && shadows && shadows_screen_space);
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

    const int lod_count         = render->GetLodCount();
    const uint32_t instances    = render->GetInstanceCount();
    const double triangles      = lod_count > 0 ? render->GetIndexCount(0) / 3.0 : 0.0;
    const bool is_visible       = render->IsVisible();
    {
        char summary[96];
        if (instances > 1)
        {
            snprintf(summary, sizeof(summary), "%s tris \xC3\x97 %s instances", format::compact(triangles).c_str(), format::grouped(instances).c_str());
        }
        else if (lod_count > 1)
        {
            snprintf(summary, sizeof(summary), "%s tris \xC2\xB7 LOD %u of %d", format::compact(triangles).c_str(), render->GetLodIndex(), lod_count);
        }
        else
        {
            snprintf(summary, sizeof(summary), "%s tris", format::compact(triangles).c_str());
        }
        component_summary(summary);
    }

    if (component_begin("Render", design::accent_render(), render))
    {
        //= REFLECT ========================================================================================================
        string& name_mesh                 = const_cast<string&>(render->GetMeshName());
        Material* material                = render->GetMaterial();
        static string name_material_empty = "none";
        string& name_material             = material ? const_cast<string&>(material->GetObjectName()) : name_material_empty;
        bool cast_shadows                 = render->HasFlag(RenderFlags::CastsShadows);
        //==================================================================================================================

        // the numbers that decide what this costs, before any field
        stat_strip("##render_stats",
        {
            { format::compact(triangles), "triangles" },
            { format::compact(lod_count > 0 ? render->GetVertexCount(0) : 0), "vertices" },
            { format::grouped(instances), instances == 1 ? "instance" : "instances" },
            { is_visible ? "yes" : "no", "in view", is_visible ? design::ok() : ImGui::Style::color_text_muted }
        });
        layout::group_spacing();

        property_input_text("Mesh", &name_mesh, true, "the geometry this entity draws");

        // material, with its color as a swatch so the row is recognisable before the name is read
        {
            layout::begin_property("Material", "drop a material from the asset browser here, or browse for one");

            const float button_width = trailing_button_width("...");
            const float reset_width  = trailing_button_width("x");
            const float swatch       = ImGui::GetFrameHeight();
            const ImVec2 drop_min    = ImGui::GetCursorScreenPos();
            const float value_w      = ImGui::GetContentRegionAvail().x;

            if (material)
            {
                const ImVec4 tint = ImVec4(
                    material->GetProperty(MaterialProperty::ColorR),
                    material->GetProperty(MaterialProperty::ColorG),
                    material->GetProperty(MaterialProperty::ColorB),
                    1.0f
                );
                ImDrawList* draw_list = ImGui::GetWindowDrawList();
                const ImVec2 center   = ImVec2(drop_min.x + swatch * 0.5f, drop_min.y + swatch * 0.5f);
                draw_list->AddCircleFilled(center, swatch * 0.36f, ImGui::EditorUi::color(tint), 24);
                draw_list->AddCircle(center, swatch * 0.36f, ImGui::EditorUi::color(ImGui::Style::color_border_strong), 24, 1.0f);
            }
            ImGui::Dummy(ImVec2(swatch, swatch));
            ImGui::SameLine(0, design::spacing_sm);

            ImGui::PushItemWidth(value_w - swatch - button_width - reset_width - design::spacing_sm * 3.0f);
            ImGui::InputText("##Material", &name_material, ImGuiInputTextFlags_ReadOnly);
            ImGui::PopItemWidth();

            if (trailing_button("...", "browse for a material file"))
            {
                file_selection::open([render](const std::string& path)
                {
                    if (FileSystem::IsEngineMaterialFile(path))
                    {
                        render->SetMaterial(path);
                    }
                });
            }
            if (trailing_button("x", "go back to the default material"))
            {
                render->SetDefaultMaterial();
            }

            // drop over the whole value row, not just the text field, and apply on mouse release
            // so imgui's two frame delivery cannot miss the assignment
            const ImVec2 drop_max = ImVec2(drop_min.x + value_w, drop_min.y + ImGui::GetFrameHeight());
            if (auto payload = ImGuiSp::receive_drag_drop_payload_rect(ImGuiSp::DragPayloadType::Material, drop_min, drop_max, ImGui::GetID("##material_drop")))
            {
                if (payload->path[0] != '\0' && FileSystem::IsEngineMaterialFile(payload->path))
                {
                    render->SetMaterial(payload->path);
                }
            }
        }

        // draw distance, the default is no limit and that is what it should say
        {
            float draw_distance    = render->GetMaxRenderDistance();
            const bool unlimited   = draw_distance >= 1.0e9f;
            layout::begin_property("Visible up to", "beyond this distance from the camera the object is not drawn");
            if (unlimited)
            {
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Any distance");
                ImGui::SameLine(0.0f, design::spacing_md);
                if (ImGuiSp::button("Set a limit"))
                {
                    render->SetMaxRenderDistance(500.0f);
                }
                ImGuiSp::tooltip("start from 500 m, small props rarely need more");
            }
            else
            {
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - trailing_button_width("No limit") - design::spacing_sm);
                if (ImGuiSp::draw_float_wrap("##draw_distance", &draw_distance, 1.0f, 0.0f, 100000.0f, "%.0f m"))
                {
                    render->SetMaxRenderDistance(draw_distance);
                }
                if (trailing_button("No limit", "draw at any distance"))
                {
                    render->SetMaxRenderDistance(numeric_limits<float>::max());
                }
            }
        }

        property_toggle("Cast shadows", &cast_shadows, "whether this object blocks light, thin or distant props can skip it to save shadow map work");

        // level of detail, bars instead of a number table: each is sized by its triangles next to lod 0
        if (lod_count > 1)
        {
            char summary[48];
            snprintf(summary, sizeof(summary), "%d levels", lod_count);
            if (layout::fold("Level of detail", true, summary))
            {
                const float row_h     = ImGui::GetFrameHeight();
                const float pad       = ImGui::EditorUi::scaled(10.0f);
                ImVec2 min, max;
                ImDrawList* draw_list = canvas("##lod_bars", row_h * lod_count + pad * 2.0f, &min, &max);
                const float label_w   = ImGui::CalcTextSize("LOD 0").x + pad;
                const float text_w    = ImGui::CalcTextSize("000.0k tris  100%").x;
                const float bar_x0    = min.x + pad + label_w;
                const float bar_x1    = max.x - pad - text_w - pad;
                const uint32_t in_use = render->HasInstancing() ? 0xffffffff : render->GetLodIndex();

                for (int i = 0; i < lod_count; i++)
                {
                    const float y        = min.y + pad + row_h * i;
                    const double tris    = render->GetIndexCount(i) / 3.0;
                    const float fraction = triangles > 0.0 ? static_cast<float>(tris / triangles) : 0.0f;
                    const bool active    = static_cast<uint32_t>(i) == in_use;
                    const ImVec4 tint    = active ? design::accent_render() : ImGui::EditorUi::alpha(design::accent_render(), 0.35f);

                    char text[48];
                    snprintf(text, sizeof(text), "LOD %d", i);
                    draw_list->AddText(ImVec2(min.x + pad, y + (row_h - ImGui::GetTextLineHeight()) * 0.5f), ImGui::EditorUi::color(active ? ImGui::Style::color_text : ImGui::Style::color_text_muted), text);

                    const float bar_h = ImGui::EditorUi::scaled(6.0f);
                    const float bar_y = y + (row_h - bar_h) * 0.5f;
                    draw_list->AddRectFilled(ImVec2(bar_x0, bar_y), ImVec2(bar_x1, bar_y + bar_h), ImGui::EditorUi::color(ImGui::Style::color_surface), bar_h);
                    draw_list->AddRectFilled(ImVec2(bar_x0, bar_y), ImVec2(bar_x0 + ImMax((bar_x1 - bar_x0) * fraction, bar_h), bar_y + bar_h), ImGui::EditorUi::color(tint), bar_h);

                    snprintf(text, sizeof(text), "%s tris  %3.0f%%", format::compact(tris).c_str(), fraction * 100.0f);
                    const ImVec2 size = ImGui::CalcTextSize(text);
                    draw_list->AddText(ImVec2(max.x - pad - size.x, y + (row_h - size.y) * 0.5f), ImGui::EditorUi::color(active ? ImGui::Style::color_text : ImGui::Style::color_text_muted), text);
                }

                if (in_use != 0xffffffff)
                {
                    char text[96];
                    snprintf(text, sizeof(text), "LOD %u is drawn right now, the next one takes over as the object gets smaller on screen.", in_use);
                    layout::note(text, design::accent_render());
                }
            }
        }

        // instancing
        if (instances > 1 || render->HasInstancing())
        {
            char summary[48];
            snprintf(summary, sizeof(summary), "%s copies", format::grouped(instances).c_str());
            if (layout::fold("Instances", false, summary))
            {
                layout::note("Every copy shares this mesh and material and is drawn in one batch. The copies are placed by whatever generated them, so they are listed here read only.", ImGui::Style::color_text_muted);
                if (render->HasInstancing())
                {
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::Style::color_canvas_deep);
                    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, ImGui::EditorUi::scaled(6.0f));
                    const float list_h = ImMin(static_cast<float>(instances), 8.0f) * ImGui::GetTextLineHeightWithSpacing() + design::spacing_md * 2.0f;
                    if (ImGui::BeginChild("##instance_list", ImVec2(0.0f, list_h), ImGuiChildFlags_AlwaysUseWindowPadding))
                    {
                        ImGuiListClipper clipper;
                        clipper.Begin(static_cast<int>(instances));
                        while (clipper.Step())
                        {
                            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
                            {
                                Vector3 position, scale;
                                Quaternion rotation;
                                render->GetInstance(static_cast<uint32_t>(i), false).Decompose(scale, rotation, position);
                                ImGui::TextColored(ImGui::Style::color_text_faint, "%5d", i);
                                ImGui::SameLine();
                                ImGui::Text("%8.2f %8.2f %8.2f", position.x, position.y, position.z);
                                ImGui::SameLine();
                                ImGui::TextColored(ImGui::Style::color_text_muted, "\xC3\x97%.2f", scale.x);
                            }
                        }
                    }
                    ImGui::EndChild();
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor();
                }
            }
        }

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

    static vector<string> body_types = { "Box", "Sphere", "Plane", "Capsule", "Mesh", "Convex hulls", "Character controller", "Vehicle", "Cloth", "Heightfield", "Unset" };
    const BodyType body_type = body->GetBodyType();
    const bool is_static     = body->IsStatic();
    const bool is_kinematic  = body->IsKinematic();
    const uint32_t motion    = is_static ? 0 : (is_kinematic ? 2 : 1);
    static vector<string> motions = { "Static", "Dynamic", "Kinematic" };
    {
        const uint32_t type_index = ImMin(static_cast<uint32_t>(body_type), static_cast<uint32_t>(body_types.size() - 1));
        char summary[96];
        if (motion == 1)
        {
            snprintf(summary, sizeof(summary), "%s \xC2\xB7 %s \xC2\xB7 %s kg", motions[motion].c_str(), body_types[type_index].c_str(), format::grouped(body->GetMass()).c_str());
        }
        else
        {
            snprintf(summary, sizeof(summary), "%s \xC2\xB7 %s", motions[motion].c_str(), body_types[type_index].c_str());
        }
        component_summary(summary);
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

        // static, dynamic and kinematic are one choice, two toggles let you ask for a static kinematic body
        uint32_t motion_index = motion;
        if (property_segmented("Motion", motions, &motion_index, "static never moves, dynamic is moved by forces and collisions, kinematic follows its transform or a script and pushes dynamic bodies without being pushed back"))
        {
            if (motion_index == 0)
            {
                body->SetStatic(true);
            }
            else if (motion_index == 1)
            {
                body->SetStatic(false);
                body->SetKinematic(false);
            }
            else
            {
                body->SetKinematic(true);
            }
        }

        uint32_t body_type_index = static_cast<uint32_t>(body_type);
        if (property_combo("Shape", body_types, &body_type_index, "the collision shape, simple shapes are cheaper and more stable than meshes"))
        {
            body->SetBodyType(static_cast<BodyType>(body_type_index));
        }
        if (body_type == BodyType::Mesh && motion_index == 1)
        {
            layout::note("A moving mesh collides as its convex hull. Exact triangle collision needs Static or Kinematic.", ImGui::Style::color_text_muted);
        }

        const bool dynamic = motion_index == 1;
        if (dynamic)
        {
            // a kilogram figure next to something you have lifted, or watched a crane lift
            struct Reference
            {
                float kg;
                const char* name;
            };
            const Reference references[] =
            {
                { 0.06f,   "a tennis ball" },
                { 0.45f,   "a football" },
                { 7.0f,    "a bowling ball" },
                { 20.0f,   "a suitcase" },
                { 80.0f,   "a person" },
                { 250.0f,  "a motorcycle" },
                { 1500.0f, "a car" },
                { 12000.0f,"a bus" }
            };
            int nearest = 0;
            for (int i = 1; i < IM_ARRAYSIZE(references); i++)
            {
                if (fabsf(log10f(references[i].kg) - log10f(ImMax(mass, 0.001f))) < fabsf(log10f(references[nearest].kg) - log10f(ImMax(mass, 0.001f))))
                {
                    nearest = i;
                }
            }
            const string mass_format = string(mass < 10.0f ? "%.2f" : "%.0f") + " kg  \xC2\xB7  like " + references[nearest].name;
            property_slider("Mass", &mass, 0.01f, 100000.0f, mass_format.c_str(), "how hard it is to push, collisions trade momentum by mass. ctrl click to type a value", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
        }

        // surface, what everything that touches this body feels, so it applies to static ones too
        {
            const char* grip   = friction < 0.1f ? "ice" : (friction < 0.3f ? "polished" : (friction < 0.5f ? "wood" : (friction < 0.75f ? "concrete" : "rubber")));
            const char* bounce = restitution < 0.1f ? "dead" : (restitution < 0.3f ? "soft" : (restitution < 0.6f ? "lively" : (restitution < 0.85f ? "bouncy" : "superball")));
            const string friction_format    = "%.2f  \xC2\xB7  " + string(grip);
            const string restitution_format = "%.2f  \xC2\xB7  " + string(bounce);
            property_slider("Friction", &friction, 0.0f, 1.0f, friction_format.c_str(), "grip against sliding, 0 is ice on ice, 1 is rubber on concrete");
            property_slider("Bounce", &restitution, 0.0f, 1.0f, restitution_format.c_str(), "restitution, how much speed survives a collision, 0 stops dead, 1 bounces back fully");
        }

        if (motion_index == 0)
        {
            layout::note("Static bodies never move, so mass and axis locks do not apply.", ImGui::Style::color_text_muted);
        }
        else if (motion_index == 2)
        {
            layout::note("Moved by its transform or a script. It pushes dynamic bodies and is never pushed back.", ImGui::Style::color_text_muted);
        }

        if (dynamic)
        {
            int locked = freeze_pos_x + freeze_pos_y + freeze_pos_z + freeze_rot_x + freeze_rot_y + freeze_rot_z;
            char summary[48];
            snprintf(summary, sizeof(summary), locked == 0 ? "free" : "%d axes locked", locked);
            if (layout::fold("Axis locks", locked > 0, summary))
            {
                property_axes("Position", &freeze_pos_x, &freeze_pos_y, &freeze_pos_z, "stop the body from moving along a world axis, lock Y to keep something on the ground plane");
                property_axes("Rotation", &freeze_rot_x, &freeze_rot_y, &freeze_rot_z, "stop the body from turning around a world axis, lock X and Z to keep a character upright");
            }

            snprintf(summary, sizeof(summary), center_of_mass == Vector3::Zero ? "defaults" : "custom center of mass");
            if (layout::fold("Advanced", false, summary))
            {
                property_slider("Rolling friction", &friction_rolling, 0.0f, 1.0f, "%.3f", "resistance to rolling, keeps balls and wheels from rolling forever");
                property_vector3("Center of mass", center_of_mass, "offset from the entity origin, lower it to make a vehicle harder to tip over");
            }
        }

        // cloth
        if (body_type == BodyType::Cloth)
        {
            float cloth_stiffness       = body->GetClothStiffness();
            float cloth_damping         = body->GetClothDamping();
            float cloth_iterations      = static_cast<float>(body->GetClothIterations());
            Vector3 cloth_pin_direction = body->GetClothPinDirection();
            bool cloth_wind             = body->GetClothWindEnabled();

            char summary[48];
            snprintf(summary, sizeof(summary), "%.0f%% stiff \xC2\xB7 %.0f iterations", cloth_stiffness * 100.0f, cloth_iterations);
            if (layout::fold("Cloth", true, summary))
            {
                property_percent("Stiffness", &cloth_stiffness, "how much the fabric resists stretching per iteration, silk is low, canvas is high");
                property_slider("Damping", &cloth_damping, 0.0f, 1.0f, "%.3f", "how quickly flapping settles");
                property_slider("Iterations", &cloth_iterations, 1.0f, 32.0f, "%.0f", "solver passes per step, more is stiffer and more expensive");
                property_vector3("Pinned edge", cloth_pin_direction, "direction towards the edge that is held in place, like the top of a flag");
                if (property_toggle("Wind", &cloth_wind, "let the world wind blow the cloth"))
                {
                    body->SetClothWindEnabled(cloth_wind);
                }
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
    }
    component_end();
}

void Properties::ShowMaterial(Material* material, Render* render) const
{
    if (!material)
    {
        return;
    }

    editor_history::MaterialScope history(material);
    const bool default_open = render == nullptr;

    // the header says what the surface is before it is opened
    {
        const uint32_t maps = material_texture_count(material);
        char summary[128];
        snprintf(summary, sizeof(summary), "%s \xC2\xB7 %s%s", material_kind(material), roughness_word(material->GetProperty(MaterialProperty::Roughness)), maps > 0 ? (" \xC2\xB7 " + to_string(maps) + (maps == 1 ? " map" : " maps")).c_str() : "");
        component_summary(summary);
    }

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

        bool invert_x = uv_per_render
            ? render->ResolveUvInvertX() > 0.5f
            : material->GetProperty(MaterialProperty::TextureInvertX) > 0.5f;
        bool invert_y = uv_per_render
            ? render->ResolveUvInvertY() > 0.5f
            : material->GetProperty(MaterialProperty::TextureInvertY) > 0.5f;

        color_picker_material->SetColor(Color(
            material->GetProperty(MaterialProperty::ColorR),
            material->GetProperty(MaterialProperty::ColorG),
            material->GetProperty(MaterialProperty::ColorB),
            material->GetProperty(MaterialProperty::ColorA)
        ));
        //==========================================================

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

        // hero, a lit preview ball beside the name, the file and what kind of surface this is
        {
            const float height    = ImGui::EditorUi::scaled(96.0f);
            ImVec2 min, max;
            ImDrawList* draw_list = canvas("##material_hero", height, &min, &max);
            const float pad       = ImGui::EditorUi::scaled(10.0f);
            const float radius    = (height - pad * 2.0f) * 0.5f;
            material_ball(draw_list, ImVec2(min.x + pad + radius, (min.y + max.y) * 0.5f), radius, material);

            const float text_x = min.x + pad * 2.0f + radius * 2.0f + ImGui::EditorUi::scaled(4.0f);
            float y            = min.y + pad;
            ImGui::PushFont(Editor::font_bold, 0.0f);
            ImGui::RenderTextEllipsis(draw_list, ImVec2(text_x, y), ImVec2(max.x - pad, y + ImGui::GetTextLineHeight()), max.x - pad, material->GetObjectName().c_str(), nullptr, nullptr);
            ImGui::PopFont();
            y += ImGui::GetTextLineHeightWithSpacing();

            const string& path = material->GetResourceFilePath();
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::Style::color_text_faint);
            ImGui::RenderTextEllipsis(draw_list, ImVec2(text_x, y), ImVec2(max.x - pad, y + ImGui::GetTextLineHeight()), max.x - pad, path.empty() ? "not saved to a file" : path.c_str(), nullptr, nullptr);
            ImGui::PopStyleColor();

            // identity chips, the facts that change how the surface renders
            vector<pair<string, ImVec4>> chips;
            chips.push_back({ material_kind(material), design::accent_material() });
            if (material->IsTransparent())
            {
                char text[32];
                snprintf(text, sizeof(text), "%.0f%% opaque", material->GetProperty(MaterialProperty::ColorA) * 100.0f);
                chips.push_back({ text, ImGui::Style::color_accent_1 });
            }
            if (material->HasTextureOfType(MaterialTextureType::AlphaMask))
            {
                chips.push_back({ "Cutout", ImGui::Style::color_text_muted });
            }
            if (static_cast<uint32_t>(material->GetProperty(MaterialProperty::CullMode)) == 2)
            {
                chips.push_back({ "Two sided", ImGui::Style::color_text_muted });
            }
            if (material->GetProperty(MaterialProperty::Tessellation) != 0.0f)
            {
                chips.push_back({ "Tessellated", ImGui::Style::color_text_muted });
            }
            if (material->GetProperty(MaterialProperty::WindAnimation) != 0.0f)
            {
                chips.push_back({ "Wind", ImGui::Style::color_text_muted });
            }
            const uint32_t maps = material_texture_count(material);
            chips.push_back({ maps == 0 ? string("No maps") : to_string(maps) + (maps == 1 ? " map" : " maps"), ImGui::Style::color_text_muted });

            const ImVec2 chip_pad = ImGui::EditorUi::scaled(ImVec2(7.0f, 3.0f));
            const float chip_h    = ImGui::GetTextLineHeight() + chip_pad.y * 2.0f;
            float x               = text_x;
            y                     = max.y - pad - chip_h;
            for (const auto& [text, tint] : chips)
            {
                const float w = ImGui::CalcTextSize(text.c_str()).x + chip_pad.x * 2.0f;
                if (x + w > max.x - pad)
                {
                    break;
                }
                draw_list->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + chip_h), ImGui::EditorUi::color(ImGui::EditorUi::alpha(tint, 0.16f)), ImGui::EditorUi::scaled(4.0f));
                draw_list->AddText(ImVec2(x + chip_pad.x, y + chip_pad.y), ImGui::EditorUi::color(tint), text.c_str());
                x += w + ImGui::EditorUi::scaled(4.0f);
            }
        }

        // presets are a starting point, they sit above everything they overwrite
        {
            static vector<string> paint_presets =
            {
                "None",
                "Gloss solid",
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

            if (property_combo("Car paint", paint_presets, &paint_preset_index, "Start from an automotive paint. It keeps the current color and sets the coat, flakes and pearl for you."))
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
                "None",
                "Glass, clear",
                "Glass, tinted",
                "Headlight lens",
                "Taillight lens",
                "Rubber tire",
                "Carbon fiber",
                "Chrome",
                "Polished metal",
                "Brake disc",
                "Leather",
                "Black plastic",
                "Emissive red light",
                "Emissive white light"
            };

            uint32_t surface_preset_index = static_cast<uint32_t>(material->GetProperty(MaterialProperty::SurfacePreset));
            if (surface_preset_index >= surface_presets.size())
            {
                surface_preset_index = 0;
            }

            if (property_combo("Surface preset", surface_presets, &surface_preset_index, "Start from a known real-world surface. It overwrites color, roughness, metalness and the glass settings."))
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

        // a row is a compact texture slot followed by the value that scales it, so a map and its strength read as one thing
        const auto texture_row = [&](const char* name, const char* tooltip, const MaterialTextureType type, const function<void()>& value)
        {
            ImGui::PushID(name);
            const float slot_size  = 40.0f;
            const float slot_px    = slot_size * spartan::Window::GetDpiScale();
            const float label_w    = layout::label_width();
            const ImVec2 start     = ImGui::GetCursorPos();

            ImGui::SetCursorPos(ImVec2(start.x, start.y + (slot_px - ImGui::GetTextLineHeight()) * 0.5f));
            ImGui::TextColored(ImGui::Style::color_text_muted, "%s", name);
            if (tooltip)
            {
                ImGuiSp::tooltip(tooltip);
            }

            ImGui::SetCursorPos(ImVec2(start.x + label_w, start.y));
            for (uint32_t slot = 0; slot < material->GetUsedSlotCount(); ++slot)
            {
                auto setter = [material, type, slot](spartan::RHI_Texture* texture)
                {
                    material->SetTexture(type, texture, static_cast<uint8_t>(slot));
                };

                if (slot > 0)
                {
                    ImGui::SameLine(0, design::spacing_sm);
                }

                ImGui::PushID(static_cast<int>(slot));
                spartan::RHI_Texture* texture = material->GetTexture(type, static_cast<uint8_t>(slot));
                if (ImGuiSp::image_slot(texture, setter, slot_size))
                {
                    file_selection::open([setter](const std::string& path)
                    {
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

                // a 40 px thumbnail is enough to recognize a map, hovering shows it properly
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                {
                    ImGui::BeginTooltip();
                    if (texture)
                    {
                        ImGuiSp::image(texture, ImVec2(ImGui::EditorUi::scaled(192.0f), ImGui::EditorUi::scaled(192.0f)));
                        ImGui::TextColored(ImGui::Style::color_text_muted, "%u \xC3\x97 %u  \xC2\xB7  %s", texture->GetWidth(), texture->GetHeight(), FileSystem::GetFileNameFromFilePath(texture->GetResourceFilePath()).c_str());
                        ImGui::TextColored(ImGui::Style::color_text_faint, "Click to replace, drop a texture on it, or use the x to clear.");
                    }
                    else
                    {
                        ImGui::TextUnformatted("Click to pick a texture, or drop one here from the asset browser.");
                    }
                    ImGui::EndTooltip();
                }
                ImGui::PopID();
            }

            if (value)
            {
                ImGui::SameLine(0, design::spacing_md);
                ImGui::SetCursorPosY(start.y + (slot_px - ImGui::GetFrameHeight()) * 0.5f);
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                value();
            }

            ImGui::SetCursorPos(ImVec2(start.x, start.y + slot_px + ImGui::GetStyle().ItemSpacing.y));
            ImGui::Dummy(ImVec2(0.0f, 0.0f));
            ImGui::PopID();
        };

        // a value that multiplies a map says so, a value on its own says what it looks like
        const auto multiplier_slider = [&](const char* id, const MaterialProperty property, const MaterialTextureType type, const char* word)
        {
            float value    = material->GetProperty(property);
            const bool map = material->HasTextureOfType(type);
            char format[96];
            if (map)
            {
                snprintf(format, sizeof(format), "\xC3\x97%%.2f  \xC2\xB7  scales the map");
            }
            else
            {
                snprintf(format, sizeof(format), "%%.2f  \xC2\xB7  %s", format::literal(word).c_str());
            }
            if (ImGui::SliderFloat(id, &value, 0.0f, 1.0f, format, ImGuiSliderFlags_AlwaysClamp))
            {
                material->SetProperty(property, value);
            }
            ImGui::EditorUi::decorate_field();
        };

        layout::separator();
        layout::section_header("Surface");

        texture_row("Color", "The base color. With a map, the swatch tints it.", MaterialTextureType::Color, [&]()
        {
            color_picker_material->Update();
            ImGui::SameLine(0, design::spacing_md);
            const Color color = color_picker_material->GetColor();
            char hex[16];
            snprintf(hex, sizeof(hex), "#%02X%02X%02X", static_cast<int>(ImClamp(color.r, 0.0f, 1.0f) * 255.0f + 0.5f), static_cast<int>(ImClamp(color.g, 0.0f, 1.0f) * 255.0f + 0.5f), static_cast<int>(ImClamp(color.b, 0.0f, 1.0f) * 255.0f + 0.5f));
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(ImGui::Style::color_text_muted, "%s", hex);
        });

        {
            float opacity = color_picker_material->GetColor().a;
            if (property_percent("Opacity", &opacity, "Below 100% the surface is see-through and is drawn in the transparent pass. For glass, pair it with the Glass settings below."))
            {
                Color color = color_picker_material->GetColor();
                color.a     = opacity;
                color_picker_material->SetColor(color);
            }
        }

        texture_row("Roughness", "How blurry reflections are, from a mirror at 0 to chalk at 1.", MaterialTextureType::Roughness, [&]()
        {
            multiplier_slider("##roughness", MaterialProperty::Roughness, MaterialTextureType::Roughness, roughness_word(material->GetProperty(MaterialProperty::Roughness)));
        });

        texture_row("Metalness", "Metal or not. Real surfaces are almost always 0 or 1, values in between are for dusty or worn metal.", MaterialTextureType::Metalness, [&]()
        {
            multiplier_slider("##metalness", MaterialProperty::Metalness, MaterialTextureType::Metalness, metalness_word(material->GetProperty(MaterialProperty::Metalness)));
        });

        texture_row("Normal", "Fine surface bumps that catch the light without adding geometry.", MaterialTextureType::Normal, [&]()
        {
            float value = material->GetProperty(MaterialProperty::Normal);
            if (ImGui::SliderFloat("##normal", &value, 0.0f, 1.0f, material->HasTextureOfType(MaterialTextureType::Normal) ? "%.2f  \xC2\xB7  bump strength" : "%.2f  \xC2\xB7  needs a map", ImGuiSliderFlags_AlwaysClamp))
            {
                material->SetProperty(MaterialProperty::Normal, value);
            }
            ImGui::EditorUi::decorate_field();
        });

        texture_row("Height", "Depth for parallax, surfaces look carved in. With Displace on, the geometry really moves.", MaterialTextureType::Height, [&]()
        {
            float value = material->GetProperty(MaterialProperty::Height);
            if (ImGui::SliderFloat("##height", &value, 0.0f, 1.0f, material->HasTextureOfType(MaterialTextureType::Height) ? "%.2f  \xC2\xB7  depth" : "%.2f  \xC2\xB7  needs a map", ImGuiSliderFlags_AlwaysClamp))
            {
                material->SetProperty(MaterialProperty::Height, value);
            }
            ImGui::EditorUi::decorate_field();
        });

        if (material->HasTextureOfType(MaterialTextureType::Height))
        {
            bool tessellation = material->GetProperty(MaterialProperty::Tessellation) != 0.0f;
            if (property_toggle("Displace", &tessellation, "Tessellates the mesh and pushes the vertices out by the height map, so silhouettes and shadows change too. Costs GPU time."))
            {
                material->SetProperty(MaterialProperty::Tessellation, tessellation ? 1.0f : 0.0f);
            }
        }

        texture_row("Occlusion", "Baked shadowing in cracks and corners. It adds to the screen-space ambient occlusion.", MaterialTextureType::Occlusion, [&]()
        {
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(ImGui::Style::color_text_faint, material->HasTextureOfType(MaterialTextureType::Occlusion) ? "Darkens crevices" : "No map");
        });

        texture_row("Emission", "Light the surface gives off. A map glows by itself, the slider makes the base color glow.", MaterialTextureType::Emission, [&]()
        {
            float value = material->GetProperty(MaterialProperty::EmissiveFromAlbedo);
            char format[96];
            if (value <= 0.0f)
            {
                snprintf(format, sizeof(format), "%%.3f  \xC2\xB7  color does not glow");
            }
            else
            {
                snprintf(format, sizeof(format), "%%.3f  \xC2\xB7  %s nits", format::literal(format::grouped(value * lighting::lighting_emissive_nits_from_albedo)).c_str());
            }
            if (ImGui::SliderFloat("##emission", &value, 0.0f, 1.0f, format, ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic))
            {
                material->SetProperty(MaterialProperty::EmissiveFromAlbedo, value);
            }
            ImGui::EditorUi::decorate_field();
            ImGuiSp::tooltip("A phone screen is about 500 nits, a lit sign 2,000, a headlight lens tens of thousands.");
        });

        texture_row("Cutout", "An alpha mask, black pixels are discarded entirely. Leaves, fences and decals use it instead of transparency.", MaterialTextureType::AlphaMask, [&]()
        {
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(ImGui::Style::color_text_faint, material->HasTextureOfType(MaterialTextureType::AlphaMask) ? "Black is cut away" : "No mask");
        });

        // layers on top of the base, most materials use none so the group stays shut unless one is on
        {
            const float clearcoat  = material->GetProperty(MaterialProperty::Clearcoat);
            const float anisotropy = material->GetProperty(MaterialProperty::Anisotropic);
            const float sheen      = material->GetProperty(MaterialProperty::Sheen);
            const float subsurface = material->GetProperty(MaterialProperty::SubsurfaceScattering);
            string active;
            auto add = [&active](const char* name, const float value)
            {
                if (value > 0.0f)
                {
                    active += (active.empty() ? "" : " \xC2\xB7 ") + string(name);
                }
            };
            add("clearcoat", clearcoat);
            add("brushed", anisotropy);
            add("sheen", sheen);
            add("subsurface", subsurface);

            if (layout::fold("Layers", !active.empty(), active.empty() ? "none" : active.c_str()))
            {
                float value = clearcoat;
                if (property_percent("Clearcoat", &value, "A thin glossy varnish over the base, like car paint or lacquered wood."))
                {
                    material->SetProperty(MaterialProperty::Clearcoat, value);
                }
                if (clearcoat > 0.0f)
                {
                    value = material->GetProperty(MaterialProperty::Clearcoat_Roughness);
                    char format[64];
                    snprintf(format, sizeof(format), "%%.2f  \xC2\xB7  %s", roughness_word(value));
                    if (property_slider("Coat roughness", &value, 0.0f, 1.0f, format, "How blurry the varnish reflection is, separate from the base underneath."))
                    {
                        material->SetProperty(MaterialProperty::Clearcoat_Roughness, value);
                    }
                }

                value = anisotropy;
                if (property_percent("Brushed", &value, "Anisotropy, highlights stretch along one direction like brushed steel or a vinyl record."))
                {
                    material->SetProperty(MaterialProperty::Anisotropic, value);
                }
                if (anisotropy > 0.0f)
                {
                    float degrees = material->GetProperty(MaterialProperty::AnisotropicRotation) * 360.0f;
                    if (property_slider("Brush direction", &degrees, 0.0f, 360.0f, "%.0f\xC2\xB0", "Which way the brushing runs across the surface."))
                    {
                        material->SetProperty(MaterialProperty::AnisotropicRotation, degrees / 360.0f);
                    }
                }

                value = sheen;
                if (property_percent("Sheen", &value, "A soft glow at grazing angles, the look of velvet, felt or dusty surfaces."))
                {
                    material->SetProperty(MaterialProperty::Sheen, value);
                }

                value = subsurface;
                if (property_percent("Subsurface", &value, "Light entering and scattering inside, for skin, wax, marble and leaves."))
                {
                    material->SetProperty(MaterialProperty::SubsurfaceScattering, value);
                }
            }
        }

        // car paint only matters on cars, it opens by itself when a paint preset or any of its effects is in use
        {
            const float flake = material->GetProperty(MaterialProperty::FlakeStrength);
            const float pearl = material->GetProperty(MaterialProperty::PearlStrength);
            const float coat  = material->GetProperty(MaterialProperty::CoatTintStrength);
            const bool in_use = flake > 0.0f || pearl > 0.0f || coat > 0.0f || material->GetProperty(MaterialProperty::PaintPreset) > 0.0f;
            string active;
            if (flake > 0.0f) active += "flakes";
            if (pearl > 0.0f) active += (active.empty() ? "" : " \xC2\xB7 ") + string("pearl");
            if (coat > 0.0f)  active += (active.empty() ? "" : " \xC2\xB7 ") + string("tinted coat");

            if (layout::fold("Car paint", in_use, active.empty() ? "off" : active.c_str()))
            {
                float value = flake;
                if (property_percent("Metal flakes", &value, "Tiny sparkling flakes under the coat that glint as the view moves."))
                {
                    material->SetProperty(MaterialProperty::FlakeStrength, value);
                }
                if (flake > 0.0f)
                {
                    value = material->GetProperty(MaterialProperty::FlakeScale);
                    if (property_slider("Flake density", &value, 1.0f, 512.0f, "%.0f", "How many flakes, higher is finer and more even.", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic))
                    {
                        material->SetProperty(MaterialProperty::FlakeScale, value);
                    }
                }

                value = pearl;
                if (property_percent("Pearl", &value, "The color shifts toward the pearl tint as the viewing angle changes."))
                {
                    material->SetProperty(MaterialProperty::PearlStrength, value);
                }
                if (pearl > 0.0f)
                {
                    float pearl_color[3] =
                    {
                        material->GetProperty(MaterialProperty::PearlColorR),
                        material->GetProperty(MaterialProperty::PearlColorG),
                        material->GetProperty(MaterialProperty::PearlColorB)
                    };
                    layout::begin_property("Pearl tint", "The color seen at grazing angles.");
                    if (ImGui::ColorEdit3("##pearl_color", pearl_color, ImGuiColorEditFlags_NoInputs))
                    {
                        material->SetProperty(MaterialProperty::PearlColorR, pearl_color[0]);
                        material->SetProperty(MaterialProperty::PearlColorG, pearl_color[1]);
                        material->SetProperty(MaterialProperty::PearlColorB, pearl_color[2]);
                    }
                }

                value = coat;
                if (property_percent("Tinted coat", &value, "Colors the clearcoat itself, the deep look of candy paint."))
                {
                    material->SetProperty(MaterialProperty::CoatTintStrength, value);
                }
                if (coat > 0.0f)
                {
                    float coat_tint[3] =
                    {
                        material->GetProperty(MaterialProperty::CoatTintR),
                        material->GetProperty(MaterialProperty::CoatTintG),
                        material->GetProperty(MaterialProperty::CoatTintB)
                    };
                    layout::begin_property("Coat tint", "The color the coat absorbs toward.");
                    if (ImGui::ColorEdit3("##coat_color", coat_tint, ImGuiColorEditFlags_NoInputs))
                    {
                        material->SetProperty(MaterialProperty::CoatTintR, coat_tint[0]);
                        material->SetProperty(MaterialProperty::CoatTintG, coat_tint[1]);
                        material->SetProperty(MaterialProperty::CoatTintB, coat_tint[2]);
                    }
                }
            }
        }

        // glass only matters when light goes through, so it opens for transparent materials
        {
            const float ior = material->GetProperty(MaterialProperty::Ior);
            char summary[64];
            snprintf(summary, sizeof(summary), "IOR %.2f \xC2\xB7 %s", ior, ior_word(ior));
            if (layout::fold("Glass", material->IsTransparent(), summary))
            {
                float value = ior;
                char format[64];
                snprintf(format, sizeof(format), "%%.2f  \xC2\xB7  like %s", ior_word(value));
                if (property_slider("Refraction", &value, 1.0f, 2.6f, format, "Index of refraction, how strongly light bends going in. Water 1.33, glass 1.5, diamond 2.42."))
                {
                    material->SetProperty(MaterialProperty::Ior, value);
                }

                value = material->GetProperty(MaterialProperty::Absorption);
                if (property_slider("Tint depth", &value, 0.0f, 8.0f, value <= 0.0f ? "%.2f  \xC2\xB7  clear" : "%.2f", "How strongly the glass absorbs its own color as light travels through it, thicker glass looks deeper. Independent of opacity."))
                {
                    material->SetProperty(MaterialProperty::Absorption, value);
                }

                float millimeters = material->GetProperty(MaterialProperty::Thickness) * 1000.0f;
                if (property_slider("Thickness", &millimeters, 0.0f, 100.0f, "%.1f mm", "How thick the glass shell is, it shifts what is seen through it and how much it tints."))
                {
                    material->SetProperty(MaterialProperty::Thickness, millimeters / 1000.0f);
                }

                if (!material->IsTransparent())
                {
                    layout::note("These only apply below 100% opacity.", ImGui::Style::color_text_muted);
                }
            }
        }

        // texture placement, per object when this material sits on a renderer so one material can tile differently on each object
        uint32_t rotation_index = static_cast<uint32_t>(uv_per_render ? render->ResolveUvRotation() : material->GetProperty(MaterialProperty::TextureRotation)) % 4;
        bool world_space_uv     = uv_per_render ? render->ResolveUvWorldSpace() != 0.0f : material->GetProperty(MaterialProperty::WorldSpaceUv) != 0.0f;
        bool has_override       = false;
        if (uv_per_render)
        {
            const MaterialOverride& ovr = render->GetMaterialOverrideMutable();
            has_override = MaterialOverride::is_set(ovr.uv_tiling_x) || MaterialOverride::is_set(ovr.uv_tiling_y) || MaterialOverride::is_set(ovr.uv_offset_x) || MaterialOverride::is_set(ovr.uv_offset_y) ||
                           MaterialOverride::is_set(ovr.uv_rotation) || MaterialOverride::is_set(ovr.uv_invert_x) || MaterialOverride::is_set(ovr.uv_invert_y) || MaterialOverride::is_set(ovr.uv_world_space);
        }
        {
            char summary[96];
            if (world_space_uv)
            {
                snprintf(summary, sizeof(summary), "world space%s", has_override ? " \xC2\xB7 this object" : "");
            }
            else
            {
                snprintf(summary, sizeof(summary), "%.2f \xC3\x97 %.2f%s", tiling.x, tiling.y, has_override ? " \xC2\xB7 this object" : "");
            }

            const bool placed = has_override || tiling.x != 1.0f || tiling.y != 1.0f || offset.x != 0.0f || offset.y != 0.0f || rotation_index != 0 || invert_x || invert_y || world_space_uv;
            if (layout::fold("Texture placement", placed, summary))
            {
                const auto axis_pair = [](const char* label, Vector2* value, const float speed, const char* tooltip)
                {
                    layout::begin_property(label, tooltip);
                    ImGui::PushID(label);
                    const float gap   = design::spacing_sm;
                    const float width = (ImGui::GetContentRegionAvail().x - gap) * 0.5f;
                    bool changed      = false;
                    const char* axis_names[2] = { "U", "V" };
                                        for (int i = 0; i < 2; i++)
                    {
                        if (i > 0)
                        {
                            ImGui::SameLine(0, gap);
                        }
                        ImGui::PushID(i);
                        const ImVec2 at = ImGui::GetCursorScreenPos();
                        ImGui::SetNextItemWidth(width);
                        changed |= ImGui::DragFloat("##axis", i == 0 ? &value->x : &value->y, speed, 0.0f, 0.0f, "%.2f");
                        ImGui::EditorUi::decorate_field();
                        ImGui::GetWindowDrawList()->AddText(ImVec2(at.x + ImGui::GetStyle().FramePadding.x, at.y + ImGui::GetStyle().FramePadding.y), ImGui::EditorUi::color(ImGui::EditorUi::axis_color(i)), axis_names[i]);
                        ImGui::PopID();
                    }
                    ImGui::PopID();
                    return changed;
                };

                ImGui::BeginDisabled(world_space_uv);
                axis_pair("Repeat", &tiling, 0.01f, "How many times the textures repeat across the surface.");
                axis_pair("Shift", &offset, 0.005f, "Slides the textures across the surface, 1 is one full repeat.");
                ImGui::EndDisabled();

                uint32_t mirror = (invert_x ? 1u : 0u) | (invert_y ? 2u : 0u);
                static vector<string> mirror_labels = { "None", "Horizontal", "Vertical", "Both" };
                if (property_segmented("Mirror", mirror_labels, &mirror, "Flips the textures along U, V or both."))
                {
                    invert_x = (mirror & 1u) != 0;
                    invert_y = (mirror & 2u) != 0;
                }

                static vector<string> rotation_labels = { "0\xC2\xB0", "90\xC2\xB0", "180\xC2\xB0", "270\xC2\xB0" };
                if (property_segmented("Rotate", rotation_labels, &rotation_index, "Turns the textures in quarter steps."))
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

                if (property_toggle("World space", &world_space_uv, "Projects the textures from world position instead of the mesh's UVs, so scaled or neighboring objects line up seamlessly. Repeat and shift stop applying."))
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

                if (uv_per_render)
                {
                    if (has_override)
                    {
                        layout::note("Placement here applies to this object only, the material file keeps its own.", ImGui::Style::color_accent_1);
                        value_column();
                        if (ImGuiSp::button("Use the material's placement"))
                        {
                            render->GetMaterialOverrideMutable() = MaterialOverride();
                            refresh_material_color_picker();
                            invert_x       = render->ResolveUvInvertX() > 0.5f;
                            invert_y       = render->ResolveUvInvertY() > 0.5f;
                        }
                    }
                    else
                    {
                        layout::note("Following the material. An edit here applies to this object only.", ImGui::Style::color_text_muted);
                    }
                }
            }
        }

        // how the surface is drawn
        {
            uint32_t cull_mode_index = static_cast<uint32_t>(material->GetProperty(MaterialProperty::CullMode));
            static vector<string> cull_summaries = { "one sided", "inside out", "two sided" };
            const char* cull_summary = cull_mode_index < cull_summaries.size() ? cull_summaries[cull_mode_index].c_str() : "";
            if (layout::fold("Drawing", false, cull_summary))
            {
                static vector<string> cull_labels = { "One sided", "Inside out", "Two sided" };
                if (property_segmented("Faces", cull_labels, &cull_mode_index, "One sided hides the back of every triangle, the usual choice. Inside out hides the front, for skyboxes and interiors seen from within. Two sided draws both, for leaves, cloth and paper."))
                {
                    material->SetProperty(MaterialProperty::CullMode, static_cast<float>(cull_mode_index));
                }

                bool wind_animation = material->GetProperty(MaterialProperty::WindAnimation) != 0.0f;
                if (property_toggle("Sway in wind", &wind_animation, "Vertices move with the world's wind, for grass, leaves and flags."))
                {
                    material->SetProperty(MaterialProperty::WindAnimation, wind_animation ? 1.0f : 0.0f);
                }

                bool motion_blur_radial = material->GetProperty(MaterialProperty::MotionBlurRadial) != 0.0f;
                if (property_toggle("Spin blur", &motion_blur_radial, "Motion blur follows rotation instead of straight movement, for wheels and fans."))
                {
                    material->SetProperty(MaterialProperty::MotionBlurRadial, motion_blur_radial ? 1.0f : 0.0f);
                }
            }
        }

        // where objects meet the ground, the terrain can creep up their base and settle on their ledges
        {
            const float blend   = material->GetProperty(MaterialProperty::TerrainBlend);
            const float coating = material->GetProperty(MaterialProperty::TerrainCoating);
            string summary      = blend > 0.0f ? "blends in" : "off";
            if (coating > 0.0f)
            {
                summary += " \xC2\xB7 coated";
            }
            if (layout::fold("Ground contact", false, summary.c_str()))
            {
                float value = blend;
                if (property_slider("Blend height", &value, 0.0f, 4.0f, value <= 0.0f ? "%.2f  \xC2\xB7  off" : "%.2f\xC3\x97", "How far the terrain's ground creeps up where this surface sinks into it, relative to the object's size. 0 opts out."))
                {
                    material->SetProperty(MaterialProperty::TerrainBlend, value);
                }

                ImGui::BeginDisabled(blend <= 0.0f);
                value = material->GetProperty(MaterialProperty::TerrainBlendSharpness);
                if (property_slider("Blend edge", &value, 0.0f, 1.0f, value < 0.34f ? "%.2f  \xC2\xB7  soft fade" : (value < 0.67f ? "%.2f  \xC2\xB7  even" : "%.2f  \xC2\xB7  hard line"), "0 washes the ground the whole way up the band, 1 cuts a hard waterline across the middle of it."))
                {
                    material->SetProperty(MaterialProperty::TerrainBlendSharpness, value);
                }
                ImGui::EndDisabled();

                value = coating;
                if (property_percent("Ledge cover", &value, "Terrain cover like moss, dust or snow settling on upward-facing ledges. 0 opts out."))
                {
                    material->SetProperty(MaterialProperty::TerrainCoating, value);
                }

                ImGui::BeginDisabled(coating <= 0.0f);
                value = material->GetProperty(MaterialProperty::TerrainCoatingScale);
                if (property_slider("Patch size", &value, 0.25f, 20.0f, "%.2f m", "How large each patch of ledge cover is.", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic))
                {
                    material->SetProperty(MaterialProperty::TerrainCoatingScale, value);
                }
                ImGui::EndDisabled();
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

    // ev100 from the physical controls, the same equation the renderer uses for manual exposure
    auto ev100_of = [](const float aperture, const float shutter, const float iso)
    {
        return log2f((ImMax(aperture, 0.01f) * ImMax(aperture, 0.01f)) / ImMax(shutter, 0.0001f) * (100.0f / ImMax(iso, 1.0f)));
    };

    const bool is_automatic = camera->GetExposureMode() == CameraExposureMode::automatic;
    const bool is_ortho     = camera->GetProjectionType() == Projection_Orthographic;
    {
        char summary[96];
        const char* projection = is_ortho ? "Orthographic" : "Perspective";
        if (is_automatic)
        {
            snprintf(summary, sizeof(summary), "%s \xC2\xB7 %.0f\xC2\xB0 \xC2\xB7 auto %+.1f EV", projection, camera->GetFovHorizontalDeg(), camera->GetAutoExposureCompensation());
        }
        else
        {
            snprintf(summary, sizeof(summary), "%s \xC2\xB7 %.0f\xC2\xB0 \xC2\xB7 EV %.1f", projection, camera->GetFovHorizontalDeg(), ev100_of(camera->GetAperture(), camera->GetShutterSpeed(), camera->GetIso()));
        }
        component_summary(summary);
    }

    if (component_begin("Camera", design::accent_camera(), camera))
    {
        //= REFLECT ======================================================================
        float aperture                    = camera->GetAperture();
        float shutter_speed               = camera->GetShutterSpeed();
        float iso                         = camera->GetIso();
        float adaptation_speed            = camera->GetAutoExposureAdaptationSpeed();
        float exposure_compensation       = camera->GetAutoExposureCompensation();
        float fov                         = camera->GetFovHorizontalDeg();
        uint32_t preset_index             = static_cast<uint32_t>(camera->GetPreset());
        uint32_t exposure_mode_index      = static_cast<uint32_t>(camera->GetExposureMode());
        bool first_person_control_enabled = camera->GetFlag(CameraFlags::CanBeControlled);
        //================================================================================

        // lens
        static vector<string> projection_types = { "Perspective", "Orthographic" };
        uint32_t projection_index = static_cast<uint32_t>(camera->GetProjectionType());
        if (property_segmented("Projection", projection_types, &projection_index, "perspective shrinks things with distance like an eye, orthographic keeps sizes constant for plans and ui"))
        {
            camera->SetProjection(static_cast<ProjectionType>(projection_index));
        }

        // the equivalent full frame focal length is how photographers think about a field of view
        {
            const float focal_mm = 18.0f / tanf(ImClamp(fov, 1.0f, 179.0f) * math::deg_to_rad * 0.5f);
            char lens[64];
            snprintf(lens, sizeof(lens), "%.0f mm lens", focal_mm);
            const string fov_format = "%.0f\xC2\xB0  \xC2\xB7  " + format::literal(lens);
            ImGui::BeginDisabled(is_ortho);
            property_slider("Field of view", &fov, 1.0f, 179.0f, fov_format.c_str(), "horizontal field of view, the lens is its full frame equivalent: 24 mm is wide, 50 mm is natural, 85 mm is a portrait", ImGuiSliderFlags_AlwaysClamp);
            ImGui::EndDisabled();
        }

        // exposure, one switch for who is in charge and the controls that follow from it
        static vector<string> exposure_modes = { "Manual", "Automatic" };
        const char* exposure_summary = exposure_mode_index == static_cast<uint32_t>(CameraExposureMode::automatic) ? "metered" : "physical camera";
        if (layout::fold("Exposure", true, exposure_summary))
        {
            property_segmented("Mode", exposure_modes, &exposure_mode_index, "manual uses aperture, shutter and iso, automatic meters the scene like a phone camera");
            const bool automatic = exposure_mode_index == static_cast<uint32_t>(CameraExposureMode::automatic);

            if (automatic)
            {
                property_slider("Compensation", &exposure_compensation, -5.0f, 5.0f, "%+.1f EV", "brighter or darker than the meter wants, one ev doubles or halves the light", ImGuiSliderFlags_AlwaysClamp);
                // the shader eases exposure in ev space at 6x speed toward brighter scenes and 2x toward darker ones, 90% settles after ln(10) time constants
                char adaptation_format[96];
                if (adaptation_speed <= 0.0f)
                {
                    snprintf(adaptation_format, sizeof(adaptation_format), "%%.1f  \xC2\xB7  instant");
                }
                else
                {
                    snprintf(adaptation_format, sizeof(adaptation_format), "%%.1f  \xC2\xB7  %.1f s to brighten, %.1f s to darken", 2.3026f / (adaptation_speed * 6.0f), 2.3026f / (adaptation_speed * 2.0f));
                }
                property_slider("Adaptation", &adaptation_speed, 0.0f, 10.0f, adaptation_format, "How quickly the eye adjusts when the scene gets brighter or darker. The times are how long it takes to get 90% of the way there, 0 is instant.", ImGuiSliderFlags_AlwaysClamp);
            }

            static vector<string> camera_presets = { "Custom", "Daylight", "Overcast", "Golden hour", "Interior", "Night", "Cinematic" };
            if (property_combo("Preset", camera_presets, &preset_index, "sets aperture, shutter speed and iso for a typical scene"))
            {
                camera->SetPreset(static_cast<CameraPreset>(preset_index));
                aperture      = camera->GetAperture();
                shutter_speed = camera->GetShutterSpeed();
                iso           = camera->GetIso();
            }

            // each physical control also names its side effect, that is why you would pick one over another
            {
                const char* focus = aperture < 2.8f ? "shallow focus" : (aperture < 8.0f ? "moderate focus" : "deep focus");
                const string aperture_format = "f/%.1f  \xC2\xB7  " + string(focus);
                property_slider("Aperture", &aperture, 1.0f, 32.0f, aperture_format.c_str(), "lens opening, a low f-number lets in more light and blurs the background more, it also drives chromatic aberration", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
            }
            {
                // the shutter is edited in stops so every notch halves or doubles the light, and shown as a fraction
                float stops = log2f(1.0f / ImMax(shutter_speed, 0.0001f));
                const char* blur = shutter_speed >= 1.0f / 60.0f ? "long motion blur" : (shutter_speed >= 1.0f / 250.0f ? "some motion blur" : "frozen motion");
                const string shutter_format = format::literal(format::shutter(shutter_speed) + "  \xC2\xB7  " + blur);
                if (property_slider("Shutter", &stops, 0.0f, 13.0f, shutter_format.c_str(), "how long the sensor is exposed, slower lets in more light and smears moving things into motion blur", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput))
                {
                    shutter_speed = 1.0f / exp2f(stops);
                }
            }
            {
                const char* grain = iso <= 400.0f ? "clean" : (iso <= 1600.0f ? "light grain" : "grainy");
                const string iso_format = "ISO %.0f  \xC2\xB7  " + string(grain);
                property_slider("ISO", &iso, 50.0f, 12800.0f, iso_format.c_str(), "sensor sensitivity, higher is brighter and adds film grain", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
            }

            // the exposure value on a scale of real scenes, the three controls above collapse into this one number
            {
                const float ev = ev100_of(aperture, shutter_speed, iso);
                struct Band
                {
                    float ev;
                    const char* name;
                };
                const Band bands[] =
                {
                    { -2.0f, "night" },
                    { 3.0f,  "indoors" },
                    { 8.0f,  "sunset" },
                    { 11.0f, "overcast" },
                    { 14.0f, "sunny" },
                    { 16.0f, "snow" }
                };
                const float ev_min = -2.0f;
                const float ev_max = 18.0f;

                ImVec2 min, max;
                ImDrawList* draw_list = canvas("##ev_scale", ImGui::EditorUi::scaled(58.0f), &min, &max);
                const float pad = ImGui::EditorUi::scaled(10.0f);
                const float x0  = min.x + pad;
                const float x1  = max.x - pad;
                auto to_x = [&](const float value) { return x0 + (x1 - x0) * ImClamp((value - ev_min) / (ev_max - ev_min), 0.0f, 1.0f); };

                const float bar_y = min.y + pad + ImGui::GetTextLineHeight() + ImGui::EditorUi::scaled(4.0f);
                const float bar_h = ImGui::EditorUi::scaled(6.0f);
                const ImU32 dark  = ImGui::EditorUi::color(ImVec4(0.10f, 0.12f, 0.30f, 1.0f));
                const ImU32 light = ImGui::EditorUi::color(ImVec4(1.00f, 0.92f, 0.65f, 1.0f));
                draw_list->AddRectFilledMultiColor(ImVec2(x0, bar_y), ImVec2(x1, bar_y + bar_h), dark, light, light, dark);

                for (int i = 0; i < IM_ARRAYSIZE(bands); i++)
                {
                    const float x = to_x(bands[i].ev);
                    draw_list->AddLine(ImVec2(x, bar_y + bar_h), ImVec2(x, bar_y + bar_h + ImGui::EditorUi::scaled(4.0f)), ImGui::EditorUi::color(ImGui::Style::color_text_faint), 1.0f);
                    ImGui::EditorUi::draw_micro_label(draw_list, ImVec2(x + ImGui::EditorUi::scaled(3.0f), bar_y + bar_h + ImGui::EditorUi::scaled(3.0f)), ImGui::EditorUi::micro_label_size(), bands[i].name, ImGui::Style::color_text_faint);
                }

                const float marker = to_x(ev);
                const ImVec4 marker_tint = automatic ? ImGui::Style::color_text_muted : ImGui::Style::color_text;
                draw_list->AddTriangleFilled(ImVec2(marker - ImGui::EditorUi::scaled(5.0f), bar_y - ImGui::EditorUi::scaled(6.0f)), ImVec2(marker + ImGui::EditorUi::scaled(5.0f), bar_y - ImGui::EditorUi::scaled(6.0f)), ImVec2(marker, bar_y), ImGui::EditorUi::color(marker_tint));

                int band = 0;
                for (int i = 0; i < IM_ARRAYSIZE(bands); i++)
                {
                    if (ev >= bands[i].ev)
                    {
                        band = i;
                    }
                }
                char text[96];
                snprintf(text, sizeof(text), "EV %.1f, exposed for a %s scene", ev, bands[band].name);
                draw_list->AddText(ImVec2(x0, min.y + ImGui::EditorUi::scaled(5.0f)), ImGui::EditorUi::color(marker_tint), text);
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("exposure value at iso 100, every step is double the light\nmanual exposure scale %.6f", 1.0f / (1.2f * exp2f(ev)));
                }

                if (automatic)
                {
                    layout::note("The meter sets the brightness. Aperture, shutter and ISO still shape depth of field, motion blur and grain.", ImGui::Style::color_text_muted);
                }
            }
        }

        if (layout::fold("Control", true, first_person_control_enabled ? "first person" : "fixed"))
        {
            property_toggle("First person", &first_person_control_enabled, "fly with WASD and look with the mouse while holding the right button");
        }

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

    uint32_t instances = 0;
    uint32_t layers    = 0;
    for (const spartan::TerrainScatterLayer& layer : terrain->GetScatterLayers())
    {
        if (terrain->IsScatterActive(layer))
        {
            layers++;
            instances += layer.instance_count;
        }
    }

    {
        char summary[96];
        if (terrain->IsGenerating())
        {
            snprintf(summary, sizeof(summary), "generating...");
        }
        else if (!terrain->HasHeightfield())
        {
            snprintf(summary, sizeof(summary), "no surface yet");
        }
        else
        {
            snprintf(summary, sizeof(summary), "%.1f km\xC2\xB2 \xC2\xB7 %s props", terrain->GetArea(), format::compact(instances).c_str());
        }
        component_summary(summary, terrain->IsGenerating() ? design::accent_terrain() : ImVec4(0, 0, 0, 0));
    }

    if (component_begin("Terrain", design::accent_terrain(), terrain))
    {
        // the terrain is authored in its own window, the inspector only reports what is there and
        // hands over. every control that used to live here has a home in that window now
        char text[128];
        std::snprintf(text, sizeof(text), "%.1f km\xC2\xB2", terrain->GetArea());
        const string area = text;
        std::snprintf(text, sizeof(text), "%u\xC2\xB2", terrain->GetTileCountAxis());
        const string tiles = text;
        stat_strip("##terrain_stats",
        {
            { area,                                                       "area" },
            { format::grouped(terrain->GetWidth()) + " \xC3\x97 " + format::grouped(terrain->GetHeight()), "samples" },
            { tiles,                                                      "tiles" },
            { format::compact(instances),                                 layers == 1 ? "props, 1 layer" : "props" }
        });

        std::snprintf(text, sizeof(text), "%.0f m to %.0f m, sea at %.0f m", terrain->GetMinY(), terrain->GetMaxY(), terrain->GetSeaLevel());
        property_text("Elevation", text, "The world height band the height map spans.");

        if (terrain->IsGenerating())
        {
            layout::note("A worker thread is rebuilding the surface.", design::accent_terrain());
        }
        else if (!terrain->HasHeightfield())
        {
            layout::note("Nothing has been generated yet, open the terrain editor to shape it.", ImGui::Style::color_warning);
        }

        layout::group_spacing();

        if (primary_button("Open Terrain Editor", ImVec2(-1, 0)))
        {
            if (TerrainEditor* editor = m_editor->GetWidget<TerrainEditor>())
            {
                editor->SetVisible(true);
                ImGui::SetWindowFocus("Terrain");
            }
        }
        ImGuiSp::tooltip(
            "shape, ground materials, props and the baked analysis, plus the sculpt brush, all live "
            "in the terrain window"
        );
    }
    component_end();
}

void Properties::ShowWater(spartan::Water* water) const
{
    if (!water)
    {
        return;
    }

    auto wave_word = [](const float wave_size)
    {
        if (wave_size <= 0.01f) return "flat";
        if (wave_size < 0.6f)   return "calm";
        if (wave_size < 1.4f)   return "follows the wind";
        if (wave_size < 2.2f)   return "rough";
        return "stormy";
    };

    {
        char summary[96];
        snprintf(summary, sizeof(summary), "%s \xC2\xB7 sea at %.1f m", wave_word(water->GetWaveSize()), water->GetSeaLevel());
        component_summary(summary);
    }

    if (component_begin("Water", design::accent_water(), water))
    {
        float wave_size = water->GetWaveSize();
        float clarity   = water->GetClarity() * 100.0f;
        float sea_level = water->GetSeaLevel();

        char format[64];
        snprintf(format, sizeof(format), "%%.2f\xC3\x97  \xC2\xB7  %s", wave_word(wave_size));
        property_slider("Waves", &wave_size, 0.0f, 3.0f, format, "0 is flat water, 1 follows the world wind, 3 triples the wave height. Crest shape, foam and detail are automatic.");

        const char* clarity_word = clarity < 30.0f ? "murky" : (clarity < 70.0f ? "clear" : "crystal");
        snprintf(format, sizeof(format), "%%.0f%%%%  \xC2\xB7  %s", clarity_word);
        property_slider("Clarity", &clarity, 0.0f, 100.0f, format, "Higher values let you see farther through the water. Lower values add suspended particles and soften underwater sunlight.");

        layout::begin_property("Sea level", "Height of the still water surface in world meters.");
        ImGui::DragFloat("##sea_level", &sea_level, 0.1f, -1000.0f, 1000.0f, "%.1f m");
        ImGui::EditorUi::decorate_field();

        if (wave_size != water->GetWaveSize())
        {
            water->SetWaveSize(wave_size);
        }
        if (clarity != water->GetClarity() * 100.0f)
        {
            water->SetClarity(clarity / 100.0f);
        }
        if (sea_level != water->GetSeaLevel())
        {
            water->SetSeaLevel(sea_level);
        }
    }
    component_end();
}

void Properties::ShowText3D(spartan::Text3D* text_3d) const
{
    if (!text_3d)
    {
        return;
    }

    // the header shows the words themselves, that is how anyone finds the right sign
    {
        string shown = text_3d->GetText();
        replace(shown.begin(), shown.end(), '\n', ' ');
        if (shown.size() > 28)
        {
            shown = shown.substr(0, 27) + "...";
        }
        char summary[96];
        snprintf(summary, sizeof(summary), "\"%s\" \xC2\xB7 %.2f m", shown.c_str(), text_3d->GetSize());
        component_summary(summary, text_3d->HasMesh() ? ImVec4(0, 0, 0, 0) : ImGui::Style::color_text_faint);
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
        float resolution     = static_cast<float>(text_3d->GetResolution());
        uint32_t alignment   = static_cast<uint32_t>(text_3d->GetAlignment());

        // multi line text deserves a multi line box
        layout::begin_property("Text", "UTF-8 text built as geometry, press enter for a new line.");
        const float lines = static_cast<float>(ImClamp(static_cast<int>(count(text.begin(), text.end(), '\n')) + 1, 1, 6));
        if (ImGui::InputTextMultiline("##text", &text, ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetTextLineHeight() * lines + ImGui::GetStyle().FramePadding.y * 2.0f)))
        {
            text_3d->SetText(text);
        }
        ImGui::EditorUi::decorate_field();

        const uint64_t entity_id    = text_3d->GetEntity()->GetObjectId();
        const uint64_t component_id = text_3d->GetObjectId();
        property_resource("Font", &font_path, "A TrueType or OpenType font file.", [entity_id, component_id](const string& path)
        {
            Entity* entity     = World::GetEntityById(entity_id);
            Text3D* component  = entity ? entity->GetComponent<Text3D>() : nullptr;
            if (component && component->GetObjectId() == component_id && FileSystem::IsSupportedFontFile(path))
            {
                component->SetFontPath(path);
            }
        });

        static vector<string> alignment_names = { "Left", "Center", "Right" };
        if (property_segmented("Align", alignment_names, &alignment, "Where each line starts relative to the entity."))
        {
            text_3d->SetAlignment(static_cast<Text3DAlignment>(alignment));
        }

        if (property_slider("Size", &size, 0.01f, 1000.0f, "%.2f m tall", "Height of a capital letter in meters.", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic))
        {
            text_3d->SetSize(size);
        }

        char format[64];
        snprintf(format, sizeof(format), "%%.3f m  \xC2\xB7  %.0f%%%% of the height", size > 0.0f ? depth / size * 100.0f : 0.0f);
        if (property_slider("Depth", &depth, 0.001f, 1000.0f, format, "How far the letters are extruded.", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic))
        {
            text_3d->SetDepth(depth);
        }

        if (property_slider("Bold", &weight, 0.0f, 1.0f, weight <= 0.0f ? "%.3f m  \xC2\xB7  as drawn" : "%.3f m", "Thickens every stroke by this much, on top of the font's own weight.", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic))
        {
            text_3d->SetWeight(weight);
        }

        if (layout::fold("Spacing and detail", false))
        {
            if (property_slider("Letter spacing", &letter_spacing, -10.0f, 100.0f, "%.3f m", "Extra space between letters, negative pulls them together."))
            {
                text_3d->SetLetterSpacing(letter_spacing);
            }

            if (property_slider("Line spacing", &line_spacing, 0.1f, 10.0f, "%.2f\xC3\x97", "Distance between lines as a multiple of the size."))
            {
                text_3d->SetLineSpacing(line_spacing);
            }

            if (property_slider("Curve detail", &resolution, 32.0f, 512.0f, "%.0f", "How finely each letter's curves are tessellated, higher is smoother and heavier.", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic))
            {
                text_3d->SetResolution(static_cast<uint32_t>(resolution));
            }
        }

        if (text_3d->IsDirty())
        {
            layout::note("Building the geometry...", ImGui::Style::color_text_muted);
        }
        else if (!text_3d->HasMesh())
        {
            layout::note("No geometry. The text needs a readable font file and an entity without another mesh.", ImGui::Style::color_warning);
        }
    }
    component_end();
}

void Properties::ShowSpline(spartan::Spline* spline) const
{
    if (!spline)
    {
        return;
    }

    // the header says what the spline is and how long, the two things that tell splines apart
    {
        char summary[128];
        const uint32_t points = spline->GetControlPointCount();
        const char* kind      = spline->GetMeshEnabled() ? "Road" : "Path";
        if (spline->GetAttachMode() != static_cast<decltype(spline->GetAttachMode())>(0) && spline->GetSourceSplineEntityId() != 0)
        {
            snprintf(summary, sizeof(summary), "%s \xC2\xB7 attached \xC2\xB7 %.0f m", kind, spline->GetLength());
        }
        else if (points < 2)
        {
            snprintf(summary, sizeof(summary), "%s \xC2\xB7 needs 2 points", kind);
        }
        else
        {
            snprintf(summary, sizeof(summary), "%s \xC2\xB7 %.0f m \xC2\xB7 %u points%s", kind, spline->GetLength(), points, spline->GetClosedLoop() ? " \xC2\xB7 loop" : "");
        }
        component_summary(summary, points < 2 && spline->GetSourceSplineEntityId() == 0 ? ImGui::Style::color_warning : ImVec4(0, 0, 0, 0));
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
        bool grade_limit_enabled          = spline->GetGradeLimitEnabled();
        float max_grade_degrees           = spline->GetMaxGradeDegrees();
        float max_cut                     = spline->GetMaxCut();
        float grade_smoothing             = spline->GetGradeSmoothing();
        float smoothing_length            = spline->GetSmoothingLength();
        bool embankment_enabled           = spline->GetEmbankmentEnabled();
        float embankment_slope_degrees    = spline->GetEmbankmentSlopeDegrees();
        float embankment_max_height       = spline->GetEmbankmentMaxHeight();
        bool carve_terrain                = spline->GetCarveTerrain();
        float carve_bed_drop              = spline->GetCarveBedDrop();
        float carve_fill_slope_degrees    = spline->GetCarveFillSlopeDegrees();
        float carve_cut_slope_degrees     = spline->GetCarveCutSlopeDegrees();
        float carve_max_shoulder          = spline->GetCarveMaxShoulder();
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

        bool is_attached_loop_inherited = attach_mode != 0 && attach_inherit_closed_loop && source_spline_id != 0;
        ImGui::BeginDisabled(is_attached_loop_inherited);
        if (property_toggle("Closed Loop", &closed_loop, "connect the last point back to the first"))
        {
            spline->SetClosedLoop(closed_loop);
        }
        ImGui::EndDisabled();

        float resolution_f = static_cast<float>(resolution);
        if (property_float("Smoothness", &resolution_f, 1.0f, 2.0f, 100.0f, "line segments between two control points, more is smoother and costs more", "%.0f segments per span"))
        {
            spline->SetResolution(static_cast<uint32_t>(resolution_f));
        }

        // the alpha only makes sense through the three named curve types, so the readout names the nearest one
        float curve_alpha = spline->GetCurveAlpha();
        const char* curve_word = curve_alpha < 0.25f ? "uniform, can overshoot" : (curve_alpha < 0.75f ? "centripetal, never loops" : "chordal, hugs the points");
        char curve_format[64];
        snprintf(curve_format, sizeof(curve_format), "%%.2f \xC2\xB7 %s", curve_word);
        if (property_float("Curve type", &curve_alpha, 0.01f, 0.0f, 1.0f, "knot spacing, 0 uniform bulges between uneven points, 0.5 centripetal never loops, 1 chordal", curve_format))
        {
            spline->SetCurveAlpha(curve_alpha);
        }

        // attaching is rare, so it stays shut unless this spline already follows another one
        bool attachment_active = source_spline_id != 0 && attach_mode != 0;
        static const char* attachment_mode_words[] = { "none", "centerline", "left edge", "right edge", "left outer", "right outer" };
        const char* attachment_summary = attachment_active && attach_mode < 6 ? attachment_mode_words[attach_mode] : "none";
        if (layout::fold("Attachment", attachment_active, attachment_summary))
        {
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
        }

        // attached splines derive their path from the source so own control points are hidden
        if (!attachment_active)
        {
            char points_summary[64];
            snprintf(points_summary, sizeof(points_summary), "%u \xC2\xB7 %.0f m", point_count, point_count >= 2 ? spline->GetLength() : 0.0f);
            if (layout::fold("Control points", true, points_summary))
            {
                char stat_buf[64];
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

                // bulk cleanup, so a traced or imported road never needs point by point editing
                static float resample_spacing = 40.0f;
                static float simplify_tolerance = 2.0f;
                layout::group_spacing();
                property_float("Spacing", &resample_spacing, 1.0f, 2.0f, 500.0f, "distance between control points after resampling, 40 suits roads", "%.0f m");
                property_float("Tolerance", &simplify_tolerance, 0.1f, 0.0f, 50.0f, "points closer than this to the line through their neighbours are dropped", "%.1f m");

                ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - total_width) * 0.5f + ImGui::GetCursorPosX());
                ImGui::BeginDisabled(point_count < 2);
                if (ImGuiSp::button("Resample", ImVec2(button_width, 0)))
                {
                    spline->ResampleControlPoints(resample_spacing);
                }
                ImGui::SameLine(0, design::spacing_md);
                if (ImGuiSp::button("Simplify", ImVec2(button_width, 0)))
                {
                    spline->SimplifyControlPoints(simplify_tolerance);
                }
                ImGui::EndDisabled();
            }
        }
        else
        {
            char attached_length_buf[64];
            std::snprintf(attached_length_buf, sizeof(attached_length_buf), "%.2f m", spline->GetLength());
            property_text("Length", attached_length_buf);
        }

        if (layout::fold("Generation", true, mesh_enabled ? "road mesh" : "guide path only"))
        {
            static vector<string> spline_mode_names = { "Path", "Road" };
            uint32_t spline_mode = mesh_enabled ? 1 : 0;
            if (property_combo("Mode", spline_mode_names, &spline_mode, "path is a flying guide, road drapes on terrain and rebuilds as you move"))
            {
                spline->SetMeshEnabled(spline_mode == 1);
                if (spline_mode == 1)
                {
                    spline->SetConformToTerrain(true);
                    if (spline->GetTerrainOffset() < 0.25f)
                    {
                        spline->SetTerrainOffset(0.25f);
                    }
                    spline->GenerateRoadMesh();
                }
                else
                {
                    spline->ClearRoadMesh();
                }
            }

            mesh_enabled       = spline->GetMeshEnabled();
            conform_to_terrain = spline->GetConformToTerrain();

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
                if (property_float("Terrain Offset", &terrain_offset, 0.05f, 0.05f, 10.0f, "lift above ground and water, keeps the mesh from z fighting", "%.2f m"))
                {
                    spline->SetTerrainOffset(terrain_offset);
                }

                if (property_toggle("Grade Limit", &grade_limit_enabled, "ramp up before a hill instead of tracking it, keeps the road drivable"))
                {
                    spline->SetGradeLimitEnabled(grade_limit_enabled);
                }

                if (grade_limit_enabled)
                {
                    if (property_float("Max Grade", &max_grade_degrees, 0.5f, 1.0f, 45.0f, "steepest slope a vehicle should ever face along the road", "%.1f deg"))
                    {
                        spline->SetMaxGradeDegrees(max_grade_degrees);
                    }
                    if (property_float("Max Cut", &max_cut, 0.1f, 0.0f, 50.0f, "how deep the road may sink into a hill before it has to ramp instead", "%.2f m"))
                    {
                        spline->SetMaxCut(max_cut);
                    }
                    if (property_float("Grade Smoothing", &grade_smoothing, 0.05f, 0.0f, 1.0f, "how much of the smoothed profile to take over the raw terrain drape", "%.2f"))
                    {
                        spline->SetGradeSmoothing(grade_smoothing);
                    }
                    if (property_float("Smoothing Length", &smoothing_length, 5.0f, 0.0f, 600.0f, "arc length the elevation is averaged over, raise it to iron out rolling terrain", "%.0f m"))
                    {
                        spline->SetSmoothingLength(smoothing_length);
                    }
                }

                if (current_profile == spartan::SplineProfile::Road)
                {
                    if (property_toggle("Carve Terrain", &carve_terrain, "grade the ground to meet the road, moving or deleting the road puts it back"))
                    {
                        spline->SetCarveTerrain(carve_terrain);
                    }

                    if (carve_terrain)
                    {
                        if (property_float("Bed Drop", &carve_bed_drop, 0.01f, 0.0f, 2.0f, "how far the graded bed sits below the road surface", "%.2f m"))
                        {
                            spline->SetCarveBedDrop(carve_bed_drop);
                        }
                        if (property_float("Fill Slope", &carve_fill_slope_degrees, 0.5f, 5.0f, 85.0f, "angle of the embankment where the road stands above the ground", "%.1f deg"))
                        {
                            spline->SetCarveFillSlopeDegrees(carve_fill_slope_degrees);
                        }
                        if (property_float("Cut Slope", &carve_cut_slope_degrees, 0.5f, 5.0f, 85.0f, "angle of the cut face where the road sits below the ground", "%.1f deg"))
                        {
                            spline->SetCarveCutSlopeDegrees(carve_cut_slope_degrees);
                        }
                        if (property_float("Max Shoulder", &carve_max_shoulder, 1.0f, 0.0f, 300.0f, "how far the cut and fill faces may reach out from the road", "%.0f m"))
                        {
                            spline->SetCarveMaxShoulder(carve_max_shoulder);
                        }
                    }

                    if (property_toggle("Embankment", &embankment_enabled, "fill the gap between a raised deck and the ground with sloped banks"))
                    {
                        spline->SetEmbankmentEnabled(embankment_enabled);
                    }

                    if (embankment_enabled)
                    {
                        if (property_float("Bank Slope", &embankment_slope_degrees, 0.5f, 5.0f, 89.0f, "angle of the fill slope, steeper means less spread", "%.1f deg"))
                        {
                            spline->SetEmbankmentSlopeDegrees(embankment_slope_degrees);
                        }
                        if (property_float("Bank Max Height", &embankment_max_height, 0.5f, 0.0f, 100.0f, "above this the deck stops banking and reads as a viaduct", "%.1f m"))
                        {
                            spline->SetEmbankmentMaxHeight(embankment_max_height);
                        }
                    }
                }
            }

            ImGui::EndDisabled();
        }

        // instancing stays shut until a template is chosen, most splines never use it
        char inst_summary_buffer[64];
        snprintf(inst_summary_buffer, sizeof(inst_summary_buffer), "every %.1f m%s", inst_spacing, inst_mirror ? ", both sides" : "");
        const string inst_summary = inst_summary_buffer;
        if (layout::fold("Instancing", inst_template_id != 0, inst_template_id != 0 ? inst_summary.c_str() : "off"))
        {
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
    }
    component_end();
}

void Properties::ShowPedestrians(spartan::Pedestrians* pedestrians) const
{
    if (!pedestrians)
    {
        return;
    }

    // every navigation provider in the scene, automatic first
    vector<string> names  = { "Automatic" };
    vector<uint64_t> ids  = { 0 };
    uint32_t selected     = 0;
    const uint64_t current = pedestrians->GetNavigationEntityId();
    for (Entity* entity : World::GetEntities())
    {
        if (!entity->GetComponent<Navigation>())
        {
            continue;
        }
        ids.push_back(entity->GetObjectId());
        names.push_back(entity->GetObjectName());
        if (ids.back() == current)
        {
            selected = static_cast<uint32_t>(ids.size() - 1);
        }
    }
    const bool missing = current != 0 && selected == 0;
    if (missing)
    {
        ids.push_back(current);
        names.push_back("Missing navigation entity");
        selected = static_cast<uint32_t>(ids.size() - 1);
    }

    {
        string summary = pedestrians->GetUseNavigation() ? (missing ? "navmesh missing" : "navmesh \xC2\xB7 " + names[selected]) : "free walking";
        component_summary(summary, missing && pedestrians->GetUseNavigation() ? ImGui::Style::color_warning : ImVec4(0, 0, 0, 0));
    }

    if (component_begin("Pedestrians", design::accent_spline_follower(), pedestrians))
    {
        const bool playing = Engine::IsFlagSet(EngineMode::Playing);
        ImGui::BeginDisabled(playing);
        bool enabled = pedestrians->GetUseNavigation();
        if (property_toggle("Use navmesh", &enabled, "Walkers find routes on the navmesh and step around each other. Off, they wander freely."))
        {
            pedestrians->SetUseNavigation(enabled);
        }

        ImGui::BeginDisabled(!enabled);
        if (property_combo("Navmesh from", names, &selected, "The entity whose Navigation component provides the shared navmesh. Automatic uses the first active one."))
        {
            pedestrians->SetNavigationEntityId(ids[selected]);
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();

        if (playing)
        {
            layout::note("These apply when play starts, stop play to change them.", ImGui::Style::color_text_muted);
        }
        else if (enabled && names.size() == 1)
        {
            layout::note("There is no Navigation component in the scene, add one so walkers have a navmesh.", ImGui::Style::color_warning);
        }
        else if (missing)
        {
            layout::note("The chosen navigation entity no longer exists.", ImGui::Style::color_warning);
        }
    }
    component_end();
}

void Properties::ShowNavigation(spartan::Navigation* navigation) const
{
    if (!navigation)
    {
        return;
    }

    {
        string summary = navigation->GetEnabled() ? (navigation->GetFollowCamera() ? "on \xC2\xB7 around the camera" : "on \xC2\xB7 around this entity") : "off";
        if (navigation->GetDebugDraw())
        {
            summary += " \xC2\xB7 shown";
        }
        component_summary(summary);
    }

    if (component_begin("Navigation", design::accent_spline_follower(), navigation))
    {
        bool enabled       = navigation->GetEnabled();
        bool debug         = navigation->GetDebugDraw();
        bool follow_camera = navigation->GetFollowCamera();

        if (property_toggle("Enabled", &enabled, "Builds and simulates this navigation world."))
        {
            navigation->SetEnabled(enabled);
        }

        uint32_t center = follow_camera ? 0 : 1;
        static vector<string> center_labels = { "Camera", "This entity" };
        if (property_segmented("Build around", center_labels, &center, "The navmesh streams in tiles around this point, the camera for open worlds or this entity for a fixed area."))
        {
            navigation->SetFollowCamera(center == 0);
        }

        if (property_toggle("Show navmesh", &debug, "Draws the walkable surface in translucent blue, raised above the ground, in edit and play mode."))
        {
            navigation->SetDebugDraw(debug);
        }

        const bool can_rebuild = enabled && (Engine::IsFlagSet(EngineMode::Playing) || debug);
        value_column();
        ImGui::BeginDisabled(!can_rebuild);
        if (ImGuiSp::button("Rebuild navmesh"))
        {
            navigation->Rebuild();
        }
        ImGui::EndDisabled();
        if (!can_rebuild)
        {
            layout::note(enabled ? "The navmesh only exists in play mode or while it is shown, turn on Show navmesh to rebuild it here." : "Enable navigation to build a navmesh.", ImGui::Style::color_text_muted);
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

    static vector<string> mode_names = { "Stop at end", "Loop", "Back and forth" };

    {
        Entity* spline_entity = follower->GetSplineEntity();
        const uint32_t mode   = static_cast<uint32_t>(follower->GetFollowMode());
        char summary[128];
        if (spline_entity)
        {
            snprintf(summary, sizeof(summary), "%s \xC2\xB7 %.0f km/h \xC2\xB7 %s", spline_entity->GetObjectName().c_str(), follower->GetSpeed() * 3.6f, mode < 3 ? (mode == 0 ? "stops" : (mode == 1 ? "loops" : "back and forth")) : "");
        }
        else
        {
            snprintf(summary, sizeof(summary), "no spline");
        }
        component_summary(summary, spline_entity ? ImVec4(0, 0, 0, 0) : ImGui::Style::color_warning);
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
        bool animate_wheels = follower->GetAnimateWheels();
        float wheel_radius  = follower->GetWheelRadius();
        float steer_angle   = follower->GetMaxSteerAngle();
        //==================================================

        vector<string> spline_names = { "None" };
        vector<uint64_t> spline_ids = { 0 };
        uint32_t selected_index     = 0;
        for (Entity* entity : World::GetEntities())
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

        if (property_combo("Path", spline_names, &selected_index, "The spline entity this one travels along."))
        {
            follower->SetSplineEntityId(spline_ids[selected_index]);
        }

        char format[64];
        snprintf(format, sizeof(format), "%%.1f m/s  \xC2\xB7  %.0f km/h", speed * 3.6f);
        if (property_slider("Speed", &speed, 0.0f, 1000.0f, format, "How fast it travels along the path.", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic))
        {
            follower->SetSpeed(speed);
        }

        if (property_segmented("At the end", mode_names, &mode, "Stop at end parks it at the last point. Loop jumps back to the start. Back and forth reverses direction."))
        {
            follower->SetFollowMode(static_cast<spartan::SplineFollowMode>(mode));
        }

        char progress_text[32];
        snprintf(progress_text, sizeof(progress_text), "%.0f%%", progress * 100.0f);
        property_meter("Progress", progress, progress_text, "How far along the path it is right now.");

        if (property_toggle("Face along path", &align, "Turns the entity to point where the path goes."))
        {
            follower->SetAlignToSpline(align);
        }

        if (align && property_toggle("Mesh faces backward", &flip, "Turns it around, for meshes whose front points the other way."))
        {
            follower->SetFlipForward(flip);
        }

        if (layout::fold("Wheels", animate_wheels, animate_wheels ? "animated" : "off"))
        {
            if (property_toggle("Animate wheels", &animate_wheels, "Rolls every wheel with speed and steers the front ones into turns. Wheels are the child entities named tire or wheel."))
            {
                follower->SetAnimateWheels(animate_wheels);
            }

            ImGui::BeginDisabled(!animate_wheels);
            if (property_slider("Wheel radius", &wheel_radius, 0.0f, 5.0f, wheel_radius <= 0.0f ? "%.2f m  \xC2\xB7  measured from the mesh" : "%.2f m", "The rolling radius, 0 measures it from the wheel mesh."))
            {
                follower->SetWheelRadius(wheel_radius);
            }

            if (property_slider("Max steering", &steer_angle, 0.0f, 90.0f, "%.0f\xC2\xB0", "How far the front wheels turn at the tightest bend."))
            {
                follower->SetMaxSteerAngle(steer_angle);
            }
            ImGui::EndDisabled();
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

    Entity* entity = spawn_point->GetEntity();
    {
        char summary[96];
        const Vector3 position = entity->GetPosition();
        snprintf(summary, sizeof(summary), "%.1f, %.1f, %.1f", position.x, position.y, position.z);
        component_summary(summary);
    }

    if (component_begin("Spawn Point", design::accent_entity(), spawn_point))
    {
        // who points here, so a spawn point is never an orphan by surprise
        uint32_t users = 0;
        for (Entity* other : World::GetEntities())
        {
            if (CarReset* car_reset = other ? other->GetComponent<CarReset>() : nullptr)
            {
                users += car_reset->GetSpawnPointEntityId() == entity->GetObjectId() ? 1 : 0;
            }
        }

        layout::note("The transform above is the exact spawn pose, move and rotate the entity to place it.", ImGui::Style::color_text_muted);
        char text[96];
        if (users == 0)
        {
            snprintf(text, sizeof(text), "No Car Reset uses this spawn point yet.");
        }
        else
        {
            snprintf(text, sizeof(text), "Used by %u Car Reset%s.", users, users == 1 ? "" : "s");
        }
        layout::note(text, users == 0 ? ImGui::Style::color_text_faint : ImGui::Style::color_ok);
    }
    component_end();
}

void Properties::ShowCarReset(spartan::CarReset* car_reset) const
{
    if (!car_reset)
    {
        return;
    }

    const uint64_t spawn_point_id = car_reset->GetSpawnPointEntityId();
    vector<string> names          = { "None" };
    vector<uint64_t> ids          = { 0 };
    uint32_t selected_index       = 0;
    for (Entity* entity : World::GetEntities())
    {
        if (!entity || !entity->GetComponent<SpawnPoint>())
        {
            continue;
        }
        ids.push_back(entity->GetObjectId());
        names.push_back(entity->GetObjectName());
        if (entity->GetObjectId() == spawn_point_id)
        {
            selected_index = static_cast<uint32_t>(names.size() - 1);
        }
    }

    component_summary(selected_index > 0 ? "spawns at " + names[selected_index] : "no spawn point", selected_index > 0 ? ImVec4(0, 0, 0, 0) : ImGui::Style::color_warning);

    if (component_begin("Car Reset", design::accent_entity(), car_reset))
    {
        if (property_combo("Spawn point", names, &selected_index, "Where the car is placed when play starts and whenever it is reset."))
        {
            car_reset->SetSpawnPointEntityId(ids[selected_index]);
        }

        if (selected_index == 0)
        {
            layout::note(names.size() == 1 ? "The scene has no Spawn Point, add one to an entity placed where the car should start." : "Choose where the car starts and resets to.", ImGui::Style::color_warning);
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

    const bool synthesized = audio_source->IsSynthesisMode();
    const bool playing     = audio_source->IsPlaying();

    // the header answers is it making sound, and where
    {
        string summary = synthesized ? "Synthesized" : (playing ? "Playing" : "Stopped");
        summary       += audio_source->GetAmbient() ? " \xC2\xB7 ambience" : (audio_source->GetIs3d() ? " \xC2\xB7 3D" : " \xC2\xB7 2D");
        if (audio_source->GetMute())
        {
            summary += " \xC2\xB7 muted";
        }
        component_summary(summary, audio_source->GetMute() ? ImGui::Style::color_warning : (playing ? design::accent_audio() : ImVec4(0, 0, 0, 0)));
    }

    if (component_begin("Audio Source", design::accent_audio(), audio_source))
    {
        //= REFLECT ==============================================
        string audio_clip_name  = audio_source->GetAudioClipName();
        bool mute               = audio_source->GetMute();
        bool play_on_start      = audio_source->GetPlayOnStart();
        bool loop               = audio_source->GetLoop();
        bool is_3d              = audio_source->GetIs3d();
        bool ambient            = audio_source->GetAmbient();
        float volume            = audio_source->GetVolume();
        float pitch             = audio_source->GetPitch();
        bool reverb_enabled     = audio_source->GetReverbEnabled();
        float reverb_room_size  = audio_source->GetReverbRoomSize();
        float reverb_decay      = audio_source->GetReverbDecay();
        float reverb_wet        = audio_source->GetReverbWet();
        //========================================================

        if (synthesized)
        {
            property_text("Source", "Synthesized live", "The sound is generated by code every frame, the car's engine for example, so there is no clip to load or preview.");
        }
        else
        {
            property_resource("Clip", &audio_clip_name, "The audio file this source plays. Drop one from the asset browser or browse for it.", [audio_source](const std::string& path)
            {
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

            // transport, audition the clip right here without entering play mode
            const float duration = audio_source->GetDuration();
            if (!ambient && duration > 0.0f)
            {
                value_column();
                const char* action = playing ? "Stop" : "Play";
                if (ImGuiSp::button(action, ImVec2(ImGui::CalcTextSize("Stop").x + ImGui::GetStyle().FramePadding.x * 4.0f, 0.0f)))
                {
                    if (playing)
                    {
                        audio_source->StopClip();
                    }
                    else
                    {
                        audio_source->PlayClip();
                    }
                }
                ImGuiSp::tooltip(playing ? "Stop the preview." : "Preview the clip with the current volume, pitch and reverb.");

                ImGui::SameLine(0, design::spacing_sm);
                const ImVec2 min  = ImGui::GetCursorScreenPos();
                const ImVec2 max  = ImVec2(min.x + ImGui::GetContentRegionAvail().x, min.y + ImGui::GetFrameHeight());
                ImGui::Dummy(ImVec2(max.x - min.x, max.y - min.y));
                ImDrawList* draw_list = ImGui::GetWindowDrawList();
                const float rounding  = ImGui::GetStyle().FrameRounding;
                const float progress  = ImClamp(audio_source->GetProgress(), 0.0f, 1.0f);
                draw_list->AddRectFilled(min, max, ImGui::EditorUi::color(ImGui::Style::color_canvas_deep), rounding);
                if (progress > 0.0f)
                {
                    draw_list->AddRectFilled(min, ImVec2(min.x + (max.x - min.x) * progress, max.y), ImGui::EditorUi::color(ImGui::EditorUi::alpha(design::accent_audio(), playing ? 0.45f : 0.2f)), rounding);
                }

                // short clips like a door slam need tenths, long ones read as a clock
                auto clock = [duration](const float seconds)
                {
                    char text[16];
                    if (duration < 10.0f)
                    {
                        snprintf(text, sizeof(text), "%.1f", seconds);
                    }
                    else
                    {
                        const int total = static_cast<int>(seconds + 0.5f);
                        snprintf(text, sizeof(text), "%d:%02d", total / 60, total % 60);
                    }
                    return string(text);
                };
                const string time = clock(progress * duration) + " / " + clock(duration) + (duration < 10.0f ? " s" : "") + (loop ? "  \xC2\xB7  loops" : "");
                const ImVec2 size = ImGui::CalcTextSize(time.c_str());
                draw_list->AddText(ImVec2(IM_ROUND((min.x + max.x - size.x) * 0.5f), IM_ROUND((min.y + max.y - size.y) * 0.5f)), ImGui::EditorUi::color(playing ? ImGui::Style::color_text : ImGui::Style::color_text_muted), time.c_str());
            }
        }

        // loudness is heard in decibels, a percent alone hides that half the slider is only 6 db
        {
            float percent = volume * 100.0f;
            char format[64];
            if (volume <= 0.0f)
            {
                snprintf(format, sizeof(format), "%%.0f%%%%  \xC2\xB7  silent");
            }
            else
            {
                snprintf(format, sizeof(format), "%%.0f%%%%  \xC2\xB7  %+.1f dB", 20.0f * log10f(volume));
            }
            if (property_slider("Volume", &percent, 0.0f, 100.0f, format, "How loud the source is before distance and mixing, 100% is the clip as recorded. Every -6 dB halves the amplitude."))
            {
                volume = percent / 100.0f;
            }
        }

        // pitch as a speed multiplier and in semitones, the unit musicians and sound designers think in
        {
            char format[64];
            const float semitones = 12.0f * log2f(ImMax(pitch, 0.01f));
            if (fabsf(semitones) < 0.05f)
            {
                snprintf(format, sizeof(format), "%%.2f\xC3\x97  \xC2\xB7  original");
            }
            else
            {
                snprintf(format, sizeof(format), "%%.2f\xC3\x97  \xC2\xB7  %+.1f semitones", semitones);
            }
            property_slider("Pitch", &pitch, 0.01f, 5.0f, format, "Playback speed. Doubling it plays twice as fast and one octave (12 semitones) higher.", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
        }

        if (!synthesized)
        {
            property_toggle("Play on start", &play_on_start, "Starts by itself when play mode begins.");
            property_toggle("Loop", &loop, "Starts over when it reaches the end.");
        }
        property_toggle("Mute", &mute, "Silences the source while it keeps playing, so unmuting picks up where it would be.");

        // where the sound lives, these are exclusive: ambience takes over from 3d entirely
        {
            uint32_t placement = ambient ? 2 : (is_3d ? 1 : 0);
            static const char* placement_summaries[] = { "2D, everywhere", "3D, in the world", "ambience" };
            if (layout::fold("Space", true, placement_summaries[placement]))
            {
                static vector<string> placement_labels = { "2D", "3D", "Ambience" };
                if (property_segmented("Placement", placement_labels, &placement, "2D plays at the same loudness everywhere, for music and interface sounds. 3D is heard from the entity's position. Ambience is a stereo bed faded by the Volume on this entity."))
                {
                    ambient = placement == 2;
                    is_3d   = placement == 1 ? true : (placement == 0 ? false : is_3d);
                }

                if (placement == 1)
                {
                    // the engine rolls off as 1 / (1 + d^2 / 15^2), half as loud at 15 m and a tenth at 45 m
                    const float rolloff   = 15.0f;
                    const float extent    = 60.0f;
                    ImVec2 min, max;
                    ImDrawList* draw_list = canvas("##audio_falloff", ImGui::EditorUi::scaled(78.0f), &min, &max);
                    const float pad       = ImGui::EditorUi::scaled(10.0f);
                    const float x0        = min.x + pad;
                    const float x1        = max.x - pad;
                    const float y0        = min.y + pad + ImGui::GetTextLineHeight();
                    const float y1        = max.y - pad - ImGui::GetTextLineHeight();
                    auto point = [&](const float distance)
                    {
                        const float gain = 1.0f / (1.0f + (distance * distance) / (rolloff * rolloff));
                        return ImVec2(x0 + (x1 - x0) * distance / extent, y1 - (y1 - y0) * gain);
                    };
                    const ImU32 curve = ImGui::EditorUi::color(design::accent_audio());
                    ImVec2 previous   = point(0.0f);
                    for (int i = 1; i <= 64; i++)
                    {
                        const ImVec2 next = point(extent * i / 64.0f);
                        draw_list->AddQuadFilled(ImVec2(previous.x, previous.y), ImVec2(next.x, next.y), ImVec2(next.x, y1), ImVec2(previous.x, y1), ImGui::EditorUi::color(ImGui::EditorUi::alpha(design::accent_audio(), 0.12f)));
                        draw_list->AddLine(previous, next, curve, 1.5f);
                        previous = next;
                    }
                    draw_list->AddLine(ImVec2(x0, y1), ImVec2(x1, y1), ImGui::EditorUi::color(ImGui::Style::color_border), 1.0f);

                    struct Mark
                    {
                        float distance;
                        const char* text;
                    };
                    const Mark marks[] = { { 15.0f, "half at 15 m" }, { 45.0f, "a tenth at 45 m" } };
                    for (const Mark& mark : marks)
                    {
                        const ImVec2 p = point(mark.distance);
                        draw_list->AddCircleFilled(p, ImGui::EditorUi::scaled(3.0f), curve, 12);
                        draw_list->AddText(ImVec2(p.x + ImGui::EditorUi::scaled(5.0f), p.y - ImGui::GetTextLineHeight() - ImGui::EditorUi::scaled(2.0f)), ImGui::EditorUi::color(ImGui::Style::color_text_muted), mark.text);
                    }
                    draw_list->AddText(ImVec2(x0, max.y - pad - ImGui::GetTextLineHeight()), ImGui::EditorUi::color(ImGui::Style::color_text_faint), "0 m");
                    const ImVec2 far_size = ImGui::CalcTextSize("60 m");
                    draw_list->AddText(ImVec2(x1 - far_size.x, max.y - pad - ImGui::GetTextLineHeight()), ImGui::EditorUi::color(ImGui::Style::color_text_faint), "60 m");
                    draw_list->AddText(ImVec2(x0, min.y + pad - ImGui::EditorUi::scaled(2.0f)), ImGui::EditorUi::color(ImGui::Style::color_text_faint), "loudness over distance from the camera");

                    layout::note("It also pans left and right and shifts pitch with speed (Doppler) as the camera moves.", ImGui::Style::color_text_muted);
                }
                else if (placement == 2)
                {
                    uint32_t profile = audio_source->GetAmbientProfile();
                    static vector<string> profile_labels = { "Region", "Cicadas", "Birds", "Wind" };
                    if (property_segmented("Habitat", profile_labels, &profile, "Region plays everywhere in the Volume. Cicadas and birds follow the vegetation actually around the listener, wind follows terrain exposure and canopy shelter."))
                    {
                        audio_source->SetAmbientProfile(profile);
                    }

                    const float habitat = audio_source->GetHabitatGain();
                    const float mixed   = audio_source->GetAmbientGain();
                    char text[32];
                    snprintf(text, sizeof(text), "%.0f%%", habitat * 100.0f);
                    property_meter("Habitat match", habitat, text, "How well the listener's surroundings suit this habitat right now.");
                    snprintf(text, sizeof(text), "%.0f%%", mixed * 100.0f);
                    property_meter("Heard at", mixed, text, "The final loudness after the Volume blend and the habitat match.");
                    layout::note("Ambience only plays in play mode, faded in and out by the Volume on this entity.", ImGui::Style::color_text_muted);
                }
                else
                {
                    layout::note("Same loudness wherever the camera is, like music or interface sounds.", ImGui::Style::color_text_muted);
                }
            }
        }

        // reverb, the feedback delay network below is what these three knobs drive
        {
            // six taps of 4799 to 13313 samples at 48 khz scaled by room size, each loop multiplies by decay * 0.85
            const float echo_spacing = (9637.0f / 48000.0f) * (0.3f + reverb_room_size * 0.7f);
            const float feedback     = ImClamp(reverb_decay * 0.85f, 0.0001f, 0.99f);
            const float tail         = echo_spacing * (-3.0f / log10f(feedback));
            const char* room_word    = reverb_room_size < 0.34f ? "room" : (reverb_room_size < 0.67f ? "hall" : "cavern");
            char summary[64];
            if (reverb_enabled)
            {
                snprintf(summary, sizeof(summary), "%s \xC2\xB7 %.1f s tail", room_word, tail);
            }
            else
            {
                snprintf(summary, sizeof(summary), "off");
            }

            if (layout::fold("Reverb", reverb_enabled, summary))
            {
                property_toggle("Enabled", &reverb_enabled, "Adds the reflections of a space around the sound.");

                ImGui::BeginDisabled(!reverb_enabled);
                {
                    float percent = reverb_room_size * 100.0f;
                    char format[64];
                    snprintf(format, sizeof(format), "%%.0f%%%%  \xC2\xB7  %s, echoes %.0f ms apart", room_word, echo_spacing * 1000.0f);
                    if (property_slider("Room size", &percent, 0.0f, 100.0f, format, "How big the space sounds, it spaces the echoes further apart."))
                    {
                        reverb_room_size = percent / 100.0f;
                    }

                    snprintf(format, sizeof(format), "%%.2f  \xC2\xB7  fades in %.1f s", tail);
                    property_slider("Decay", &reverb_decay, 0.0f, 0.99f, format, "How long the reflections ring out. The time is how long they take to fall by 60 dB.");

                    percent = reverb_wet * 100.0f;
                    if (property_slider("Mix", &percent, 0.0f, 100.0f, "%.0f%%", "How much of the reverb is heard against the dry sound. High values sound distant."))
                    {
                        reverb_wet = percent / 100.0f;
                    }
                }
                ImGui::EndDisabled();

                layout::note("Inside a Volume with reverb on, the Volume sets these from its size during play.", ImGui::Style::color_text_muted);
            }
        }

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
        if (ambient != audio_source->GetAmbient())
        {
            audio_source->SetAmbient(ambient);
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

    const math::BoundingBox& bounding_box = volume->GetBoundingBox();
    const Vector3 local_size              = bounding_box.GetSize();

    // the header says how big the space is and what it changes
    {
        char summary[128];
        const size_t overrides = volume->GetOptions().size();
        const Vector3 shown    = volume->GetEntity() ? (bounding_box * volume->GetEntity()->GetMatrix()).GetSize() : local_size;
        snprintf(summary, sizeof(summary), "%.1f \xC3\x97 %.1f \xC3\x97 %.1f m%s%s", shown.x, shown.y, shown.z, overrides > 0 ? (" \xC2\xB7 " + to_string(overrides) + (overrides == 1 ? " override" : " overrides")).c_str() : "", volume->GetReverbEnabled() ? " \xC2\xB7 reverb" : "");
        component_summary(summary);
    }

    if (component_begin("Volume", design::accent_volume(), volume))
    {
        // a box is thought of as how big and where, the corners are what the engine stores
        {
            Vector3 center = bounding_box.GetCenter();
            Vector3 size   = local_size;
            property_vector3("Size", size, "Width, height and depth of the box in the entity's local space, the transform's scale multiplies it.");
            property_vector3("Center", center, "Where the box sits relative to the entity.");
            size = Vector3(ImMax(size.x, 0.01f), ImMax(size.y, 0.01f), ImMax(size.z, 0.01f));
            if (size != local_size || center != bounding_box.GetCenter())
            {
                volume->SetBoundingBox(math::BoundingBox(center - size * 0.5f, center + size * 0.5f));
            }

            if (Entity* entity = volume->GetEntity())
            {
                const Vector3 world_size = (volume->GetBoundingBox() * entity->GetMatrix()).GetSize();
                if ((world_size - local_size).Length() > 0.01f)
                {
                    char text[128];
                    snprintf(text, sizeof(text), "%.1f \xC3\x97 %.1f \xC3\x97 %.1f m in the world after the entity's scale.", world_size.x, world_size.y, world_size.z);
                    layout::note(text, ImGui::Style::color_text_muted);
                }
            }

            if (layout::fold("Corners", false))
            {
                Vector3 min = volume->GetBoundingBox().GetMin();
                Vector3 max = volume->GetBoundingBox().GetMax();
                property_vector3("Min", min, "The low corner of the box.");
                property_vector3("Max", max, "The high corner of the box.");
                if (min != volume->GetBoundingBox().GetMin() || max != volume->GetBoundingBox().GetMax())
                {
                    volume->SetBoundingBox(math::BoundingBox(min, max));
                }
            }
        }

        // only what this volume changes is listed, everything else follows the global render options
        {
            const auto& options = volume->GetOptions();
            char summary[48];
            snprintf(summary, sizeof(summary), "%zu active", options.size());
            if (layout::fold("Render overrides", true, options.empty() ? "none" : summary))
            {
                auto pretty = [](const string& cvar_name)
                {
                    string text = cvar_name.size() > 2 ? cvar_name.substr(2) : cvar_name;
                    replace(text.begin(), text.end(), '_', ' ');
                    return text;
                };

                // sorted so rows do not jump around as the map rehashes
                vector<string> active;
                for (const auto& [name, value] : options)
                {
                    active.push_back(name);
                }
                sort(active.begin(), active.end());

                string remove;
                for (const string& name : active)
                {
                    ImGui::PushID(name.c_str());
                    const CVarVariant* global = nullptr;
                    string_view hint;
                    for (const auto& [cvar_name, cvar] : ConsoleRegistry::Get().GetAll())
                    {
                        if (cvar_name == name)
                        {
                            global = cvar.m_value_ptr;
                            hint   = cvar.m_hint;
                            break;
                        }
                    }

                    const string label   = pretty(name);
                    const string tooltip = string(hint.empty() ? name : hint);
                    layout::begin_property(label.c_str(), tooltip.c_str());
                    const float remove_w = trailing_button_width("x");
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - remove_w - design::spacing_sm);
                    float value = volume->GetOption(name.c_str());
                    char format[64];
                    if (global && holds_alternative<float>(*global))
                    {
                        snprintf(format, sizeof(format), "%%.2f  \xC2\xB7  global %.2f", get<float>(*global));
                    }
                    else
                    {
                        snprintf(format, sizeof(format), "%%.2f");
                    }
                    if (ImGui::DragFloat("##value", &value, 0.01f, 0.0f, 0.0f, format))
                    {
                        volume->SetOption(name.c_str(), value);
                    }
                    ImGui::EditorUi::decorate_field();
                    if (trailing_button("x", "Stop overriding, the global value applies again inside this volume."))
                    {
                        remove = name;
                    }
                    ImGui::PopID();
                }
                if (!remove.empty())
                {
                    volume->RemoveOption(remove.c_str());
                }

                // searchable picker, a hundred toggles is a list to scroll, a search is one word away
                value_column();
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(FLT_MAX, ImGui::EditorUi::scaled(360.0f)));
                if (ImGui::BeginCombo("##add_override", "Add an override...", ImGuiComboFlags_HeightLarge))
                {
                    static char filter[64] = {};
                    if (ImGui::IsWindowAppearing())
                    {
                        filter[0] = '\0';
                        ImGui::SetKeyboardFocusHere();
                    }
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    ImGui::InputTextWithHint("##filter", "Search render options", filter, sizeof(filter));

                    string needle = filter;
                    transform(needle.begin(), needle.end(), needle.begin(), [](const unsigned char c) { return static_cast<char>(tolower(c)); });
                    for (const auto& [cvar_name, cvar] : ConsoleRegistry::Get().GetAll())
                    {
                        if (cvar_name.size() < 2 || cvar_name[0] != 'r' || cvar_name[1] != '.' || !holds_alternative<float>(*cvar.m_value_ptr))
                        {
                            continue;
                        }
                        const string name(cvar_name);
                        if (options.find(name) != options.end())
                        {
                            continue;
                        }
                        const string label = pretty(name);
                        string haystack    = label + " " + string(cvar.m_hint);
                        transform(haystack.begin(), haystack.end(), haystack.begin(), [](const unsigned char c) { return static_cast<char>(tolower(c)); });
                        if (!needle.empty() && haystack.find(needle) == string::npos)
                        {
                            continue;
                        }

                        if (ImGui::Selectable(label.c_str()))
                        {
                            volume->SetOption(name.c_str(), get<float>(*cvar.m_value_ptr));
                        }
                        if (!cvar.m_hint.empty())
                        {
                            ImGuiSp::tooltip(string(cvar.m_hint).c_str());
                        }
                    }
                    ImGui::EndCombo();
                }

                if (options.empty())
                {
                    layout::note("Inside this box, any render option added here replaces the global value, for example darker exposure in a tunnel.", ImGui::Style::color_text_muted);
                }
            }
        }

        // reverb is derived, not authored, so say what this box will sound like
        {
            bool reverb_enabled = volume->GetReverbEnabled();
            float longest       = ImMax(local_size.x, ImMax(local_size.y, local_size.z));
            if (Entity* entity = volume->GetEntity())
            {
                const Vector3 world_size = (volume->GetBoundingBox() * entity->GetMatrix()).GetSize();
                longest = ImMax(world_size.x, ImMax(world_size.y, world_size.z));
            }
            const float size_factor  = ImClamp(longest / 50.0f, 0.0f, 1.0f);
            const float room_size    = 0.6f + size_factor * 0.4f;
            const float decay        = 0.7f + size_factor * 0.28f;
            const float echo_spacing = (9637.0f / 48000.0f) * (0.3f + room_size * 0.7f);
            const float tail         = echo_spacing * (-3.0f / log10f(ImClamp(decay * 0.85f, 0.0001f, 0.99f)));
            char summary[64];
            snprintf(summary, sizeof(summary), reverb_enabled ? "%.1f s tail" : "off", tail);

            if (layout::fold("Reverb", reverb_enabled, summary))
            {
                if (property_toggle("Enabled", &reverb_enabled, "Sounds inside the box get the reflections of a space this size, overriding their own reverb during play."))
                {
                    volume->SetReverbEnabled(reverb_enabled);
                }

                char text[160];
                snprintf(text, sizeof(text), "A %.0f m space rings for about %.1f s. The reverb is sized from the longest side, 50 m and up sounds like the biggest hall.", longest, tail);
                layout::note(text, reverb_enabled ? ImGui::Style::color_text_muted : ImGui::Style::color_text_faint);
            }
        }

        // ambience regions, the audio sources on this entity fade in by where the listener stands
        {
            float fade     = volume->GetAudioFadeDistance();
            bool boundary  = volume->GetAudioBoundaryOnly();
            const size_t points = volume->GetAudioPolygon().size();
            char summary[64];
            snprintf(summary, sizeof(summary), "%s \xC2\xB7 %.0f m fade", boundary ? "shoreline" : "whole area", fade);

            if (layout::fold("Soundscape", false, summary))
            {
                if (property_slider("Fade distance", &fade, 0.01f, 10000.0f, "%.1f m", "How far the ambience takes to fade in from the edge, in local meters.", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic))
                {
                    volume->SetAudioFadeDistance(fade);
                }

                uint32_t area = boundary ? 1 : 0;
                static vector<string> area_labels = { "Whole area", "Shoreline" };
                if (property_segmented("Plays", area_labels, &area, "Whole area fills the inside of the shape. Shoreline only plays near its outline, for surf along a coast."))
                {
                    volume->SetAudioBoundaryOnly(area == 1);
                }

                char group[128];
                snprintf(group, sizeof(group), "%s", volume->GetAudioGroup().c_str());
                layout::begin_property("Mix group", "Overlapping regions in the same group share loudness instead of stacking.");
                if (ImGui::InputTextWithHint("##audio_group", "none", group, sizeof(group)))
                {
                    volume->SetAudioGroup(group);
                }
                ImGui::EditorUi::decorate_field();

                char text[128];
                if (points == 0)
                {
                    snprintf(text, sizeof(text), "The region is the box above.");
                }
                else
                {
                    snprintf(text, sizeof(text), "The region follows a %zu point outline instead of the box.", points);
                }
                layout::note(text, ImGui::Style::color_text_muted);
            }
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

    static vector<string> preset_names =
    {
        "Custom", "Fire", "Smoke", "Steam", "Sparks", "Dust", "Snow",
        "Rain", "Confetti", "Fireflies", "Blood", "Magic", "Explosion",
        "Waterfall", "Embers", "Tire smoke", "Exhaust"
    };

    // how many are alive once the emitter settles, rate times lifetime, against the cap that silently drops the rest
    const float steady_alive = particle_system->GetEmissionRate() * particle_system->GetLifetime();
    const float cap          = static_cast<float>(ImMax(particle_system->GetMaxParticles(), 1u));
    const bool over_budget   = steady_alive > cap;

    {
        const uint32_t preset = static_cast<uint32_t>(particle_system->GetPreset());
        char summary[128];
        snprintf(summary, sizeof(summary), "%s \xC2\xB7 %s/s \xC2\xB7 %s alive", preset < preset_names.size() ? preset_names[preset].c_str() : "Custom", format::compact(particle_system->GetEmissionRate()).c_str(), format::compact(ImMin(steady_alive, cap)).c_str());
        component_summary(summary, over_budget ? ImGui::Style::color_warning : ImVec4(0, 0, 0, 0));
    }

    if (component_begin("Particle System", design::accent_particles(), particle_system))
    {
        //= REFLECT =====================================================
        float emission_rate        = particle_system->GetEmissionRate();
        float lifetime             = particle_system->GetLifetime();
        float start_speed          = particle_system->GetStartSpeed();
        float start_size           = particle_system->GetStartSize();
        float end_size             = particle_system->GetEndSize();
        float gravity_modifier     = particle_system->GetGravityModifier();
        float emission_radius      = particle_system->GetEmissionRadius();
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
        float vortex_strength      = particle_system->GetVortexStrength();
        float vortex_radius        = particle_system->GetVortexRadius();
        float thermal_strength     = particle_system->GetThermalStrength();
        float thermal_decay        = particle_system->GetThermalDecay();
        float rollup_strength      = particle_system->GetRollupStrength();
        float wake_strength        = particle_system->GetWakeStrength();
        float churn_strength       = particle_system->GetChurnStrength();
        float collision_clearance  = particle_system->GetCollisionClearance();
        float spawn_burst          = particle_system->GetSpawnBurst();
        color_picker_particle_start->SetColor(particle_system->GetStartColor());
        color_picker_particle_end->SetColor(particle_system->GetEndColor());
        //===============================================================

        // a particle's whole life in one strip, size and color from birth to death along its lifetime
        {
            const Color start_color = particle_system->GetStartColor();
            const Color end_color   = particle_system->GetEndColor();
            ImVec2 min, max;
            ImDrawList* draw_list   = canvas("##particle_life", ImGui::EditorUi::scaled(72.0f), &min, &max);
            const float pad         = ImGui::EditorUi::scaled(10.0f);
            const float label_h     = ImGui::GetTextLineHeight();
            const float center_y    = IM_ROUND((min.y + max.y - label_h) * 0.5f);
            const float max_radius  = (max.y - min.y - label_h - pad * 2.0f) * 0.5f;
            const float largest     = ImMax(ImMax(start_size, end_size), 0.0001f);
            const int samples       = 9;
            const float x0          = min.x + pad + max_radius;
            const float x1          = max.x - pad - max_radius;
            for (int i = 0; i < samples; i++)
            {
                const float t      = static_cast<float>(i) / (samples - 1);
                const float size   = start_size + (end_size - start_size) * t;
                const float radius = ImMax(max_radius * size / largest, 1.0f);
                const ImVec4 color = ImVec4(
                    start_color.r + (end_color.r - start_color.r) * t,
                    start_color.g + (end_color.g - start_color.g) * t,
                    start_color.b + (end_color.b - start_color.b) * t,
                    start_color.a + (end_color.a - start_color.a) * t
                );
                const ImVec2 at = ImVec2(x0 + (x1 - x0) * t, center_y);
                draw_list->AddCircleFilled(at, radius, ImGui::EditorUi::color(ImVec4(ImMin(color.x, 1.0f), ImMin(color.y, 1.0f), ImMin(color.z, 1.0f), ImClamp(color.w, 0.06f, 1.0f))), 24);
                draw_list->AddCircle(at, radius, ImGui::EditorUi::color(ImGui::EditorUi::alpha(ImGui::Style::color_text, 0.12f)), 24, 1.0f);
            }

            char text[64];
            const float label_y = max.y - pad - label_h + ImGui::EditorUi::scaled(4.0f);
            snprintf(text, sizeof(text), "%.2f m", start_size);
            draw_list->AddText(ImVec2(min.x + pad, label_y), ImGui::EditorUi::color(ImGui::Style::color_text_muted), text);
            snprintf(text, sizeof(text), "%.1f s", lifetime * 0.5f);
            ImVec2 size = ImGui::CalcTextSize(text);
            draw_list->AddText(ImVec2(IM_ROUND((min.x + max.x - size.x) * 0.5f), label_y), ImGui::EditorUi::color(ImGui::Style::color_text_faint), text);
            snprintf(text, sizeof(text), "%.2f m at %.1f s", end_size, lifetime);
            size = ImGui::CalcTextSize(text);
            draw_list->AddText(ImVec2(max.x - pad - size.x, label_y), ImGui::EditorUi::color(ImGui::Style::color_text_muted), text);
        }

        // the budget, every particle over the cap is simply never born
        {
            char text[96];
            snprintf(text, sizeof(text), "%s of %s alive", format::grouped(steady_alive).c_str(), format::grouped(cap).c_str());
            property_meter("Budget", ImMin(steady_alive / cap, 1.0f), text, "Rate times lifetime is how many particles are alive once the emitter settles. Above the cap the rest are not spawned.");
            if (over_budget)
            {
                snprintf(text, sizeof(text), "The cap drops about %.0f%% of what is emitted. Raise the cap, or lower the rate or lifetime.", (1.0f - cap / steady_alive) * 100.0f);
                layout::note(text, ImGui::Style::color_warning);
            }
        }

        uint32_t preset_index = static_cast<uint32_t>(particle_system->GetPreset());
        if (property_combo("Preset", preset_names, &preset_index, "Start from a known effect, it overwrites every setting below."))
        {
            particle_system->ApplyPreset(static_cast<spartan::ParticlePreset>(preset_index));

            // refresh local copies after preset application
            emission_rate        = particle_system->GetEmissionRate();
            lifetime             = particle_system->GetLifetime();
            start_speed          = particle_system->GetStartSpeed();
            start_size           = particle_system->GetStartSize();
            end_size             = particle_system->GetEndSize();
            gravity_modifier     = particle_system->GetGravityModifier();
            emission_radius      = particle_system->GetEmissionRadius();
            emission_direction   = particle_system->GetEmissionDirection();
            emission_cone_angle  = particle_system->GetEmissionConeAngle();
            directional_blend    = particle_system->GetDirectionalBlend();
            emissive_strength    = particle_system->GetEmissiveStrength();
            soft_depth_scale     = particle_system->GetSoftDepthScale();
            volume_density       = particle_system->GetVolumeDensity();
            volume_anisotropy    = particle_system->GetVolumeAnisotropy();
            volume_shadowing     = particle_system->GetVolumeShadowing();
            drag                 = particle_system->GetDrag();
            turbulence_strength  = particle_system->GetTurbulenceStrength();
            wind_influence       = particle_system->GetWindInfluence();
            velocity_inheritance = particle_system->GetVelocityInheritance();
            velocity_stretch     = particle_system->GetVelocityStretch();
            vortex_strength      = particle_system->GetVortexStrength();
            vortex_radius        = particle_system->GetVortexRadius();
            thermal_strength     = particle_system->GetThermalStrength();
            thermal_decay        = particle_system->GetThermalDecay();
            rollup_strength      = particle_system->GetRollupStrength();
            wake_strength        = particle_system->GetWakeStrength();
            churn_strength       = particle_system->GetChurnStrength();
            collision_clearance  = particle_system->GetCollisionClearance();
            spawn_burst          = particle_system->GetSpawnBurst();
            color_picker_particle_start->SetColor(particle_system->GetStartColor());
            color_picker_particle_end->SetColor(particle_system->GetEndColor());
        }

        // the effect file, what gets shared between emitters
        {
            string effect_path = particle_system->GetEffectPath().empty() ? "not saved, lives in this world" : particle_system->GetEffectPath();
            layout::begin_property("Effect", "A .particle file lets several emitters share one effect. Load replaces these settings, save writes them out.");
            const float buttons_w = trailing_button_width("Load") + trailing_button_width("Save") + design::spacing_sm * 2.0f;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - buttons_w);
            ImGui::InputText("##effect_path", &effect_path, ImGuiInputTextFlags_ReadOnly);
            ImGui::EditorUi::decorate_field();
            if (trailing_button("Load", "Load a .particle effect."))
            {
                file_selection::open([particle_system](const std::string& path)
                {
                    if (FileSystem::GetExtensionFromFilePath(path) == ".particle")
                    {
                        particle_system->LoadEffect(path);
                    }
                });
            }
            if (trailing_button("Save", particle_system->GetEffectPath().empty() ? "Save to project/particles/custom.particle." : "Save over the effect file."))
            {
                const string path = particle_system->GetEffectPath().empty() ? "project/particles/custom.particle" : particle_system->GetEffectPath();
                particle_system->SaveEffect(path);
                particle_system->SetEffectPath(path);
            }
        }

        if (layout::fold("Emission", true))
        {
            property_slider("Rate", &emission_rate, 0.0f, 10000.0f, "%.0f per second", "Particles born every second.", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);

            float max_particles = cap;
            if (property_slider("Cap", &max_particles, 100.0f, 100000.0f, "%.0f at once", "The most particles this emitter keeps alive, it reserves GPU memory for all of them.", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic))
            {
                particle_system->SetMaxParticles(static_cast<uint32_t>(max_particles));
            }

            property_slider("Burst", &spawn_burst, 0.0f, 100000.0f, spawn_burst <= 0.0f ? "%.0f  \xC2\xB7  none" : "%.0f at once", "A one-off puff of particles when the effect loads or is triggered, on top of the rate.", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
            property_slider("Spawn radius", &emission_radius, 0.0f, 100.0f, emission_radius <= 0.0f ? "%.2f m  \xC2\xB7  a point" : "%.2f m", "Particles are born anywhere inside a sphere this size.", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);

            property_vector3("Direction", emission_direction, "The world-space direction particles are launched toward.");

            float cone_degrees = emission_cone_angle * (180.0f / math::pi);
            if (property_slider("Spread", &cone_degrees, 0.0f, 180.0f, cone_degrees < 1.0f ? "%.0f\xC2\xB0  \xC2\xB7  a straight line" : (cone_degrees >= 179.0f ? "%.0f\xC2\xB0  \xC2\xB7  every direction" : "%.0f\xC2\xB0"), "The cone around the direction particles scatter into."))
            {
                emission_cone_angle = cone_degrees * (math::pi / 180.0f);
            }

            float blend_percent = directional_blend * 100.0f;
            if (property_slider("Aim", &blend_percent, 0.0f, 100.0f, blend_percent < 1.0f ? "%.0f%%  \xC2\xB7  drifts up at random" : (blend_percent > 99.0f ? "%.0f%%  \xC2\xB7  follows the direction" : "%.0f%%"), "0 lets particles rise loosely like smoke, 100% sends them along the direction and spread."))
            {
                directional_blend = blend_percent / 100.0f;
            }
        }

        if (layout::fold("Motion", true))
        {
            property_slider("Lifetime", &lifetime, 0.01f, 60.0f, "%.2f s", "How long each particle lives.", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);

            char format[64];
            snprintf(format, sizeof(format), "%%.1f m/s  \xC2\xB7  %.0f km/h", start_speed * 3.6f);
            property_slider("Launch speed", &start_speed, 0.0f, 100.0f, format, "How fast particles leave the emitter.", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);

            property_slider("Gravity", &gravity_modifier, -20.0f, 20.0f, gravity_modifier < -0.005f ? "%.2f\xC3\x97  \xC2\xB7  falls" : (gravity_modifier > 0.005f ? "%.2f\xC3\x97  \xC2\xB7  rises" : "%.2f\xC3\x97  \xC2\xB7  floats"), "Multiplies earth's gravity. -1 falls like a stone, positive values rise like hot gas.");
            property_slider("Drag", &drag, 0.0f, 10.0f, "%.2f", "Air resistance, higher values slow particles down sooner so smoke settles.", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);

            float wind_percent = wind_influence * 100.0f;
            if (property_slider("Wind", &wind_percent, 0.0f, 500.0f, "%.0f%%", "How much of the world's wind carries the particles."))
            {
                wind_influence = wind_percent / 100.0f;
            }

            float inherit_percent = velocity_inheritance * 100.0f;
            if (property_slider("Carry motion", &inherit_percent, 0.0f, 200.0f, "%.0f%%", "How much of the emitter's own speed particles keep at birth, so exhaust trails a moving car."))
            {
                velocity_inheritance = inherit_percent / 100.0f;
            }
        }

        // forces are for specialists, the group stays shut unless one is already in use
        {
            string active;
            auto add = [&active](const char* name, const float value)
            {
                if (value != 0.0f)
                {
                    active += (active.empty() ? "" : " \xC2\xB7 ") + string(name);
                }
            };
            add("turbulence", turbulence_strength);
            add("vortex", vortex_strength);
            add("thermal", thermal_strength);
            add("rollup", rollup_strength);
            add("wake", wake_strength);
            add("churn", churn_strength);

            if (layout::fold("Forces", false, active.empty() ? "none" : active.c_str()))
            {
                property_slider("Turbulence", &turbulence_strength, 0.0f, 10.0f, "%.2f", "Swirling procedural motion that breaks up straight paths.", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
                property_slider("Vortex", &vortex_strength, -400.0f, 400.0f, vortex_strength == 0.0f ? "%.1f  \xC2\xB7  off" : (vortex_strength > 0.0f ? "%.1f  \xC2\xB7  counterclockwise" : "%.1f  \xC2\xB7  clockwise"), "Orbiting pull around an axis, scripts drive its center and axis, a spinning tire for example.");
                ImGui::BeginDisabled(vortex_strength == 0.0f);
                property_slider("Vortex radius", &vortex_radius, 0.0f, 20.0f, "%.2f m", "The size of the spinning core, the pull fades outside it.");
                ImGui::EndDisabled();
                property_slider("Thermal lift", &thermal_strength, 0.0f, 50.0f, "%.2f m/s\xC2\xB2", "Buoyancy of hot gas, it lifts the plume.", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
                ImGui::BeginDisabled(thermal_strength == 0.0f);
                property_slider("Cooling", &thermal_decay, 0.0f, 20.0f, "%.2f", "How fast the plume cools by mixing, higher stops the lift sooner.");
                ImGui::EndDisabled();
                property_slider("Rollup", &rollup_strength, 0.0f, 10.0f, "%.2f", "Curls a jet that runs along the ground up into a rolling plume.");
                property_slider("Wake", &wake_strength, 0.0f, 100.0f, "%.2f", "A pair of counter-rotating swirls trailing the emitter, it only shows when the emitter moves.", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
                ImGui::BeginDisabled(particle_system->GetTexture() == nullptr);
                property_slider("Churn", &churn_strength, 0.0f, 0.1f, particle_system->GetTexture() ? "%.3f" : "%.3f  \xC2\xB7  needs a texture", "Boils the texture from the inside over time.");
                ImGui::EndDisabled();
                property_slider("Collision clearance", &collision_clearance, 0.0f, 2.0f, "%.2f m", "How far a particle must travel before it collides with the world. Raise it when the emitter sits inside geometry, an exhaust tip for example.");
            }
        }

        if (layout::fold("Look", true))
        {
            property_slider("Start size", &start_size, 0.001f, 10.0f, "%.3f m", "Diameter at birth.", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
            property_slider("End size", &end_size, 0.0f, 10.0f, end_size <= 0.0f ? "%.3f m  \xC2\xB7  shrinks away" : "%.3f m", "Diameter at death, particles grow or shrink between the two.", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);

            ImGui::PushID("particle_start_color");
            property_color("Start color", color_picker_particle_start.get(), "Color and opacity at birth.");
            ImGui::PopID();
            ImGui::PushID("particle_end_color");
            property_color("End color", color_picker_particle_end.get(), "Color and opacity at death, a transparent end fades particles out.");
            ImGui::PopID();

            uint32_t lighting_mode = static_cast<uint32_t>(particle_system->GetLightingMode());
            static vector<string> lighting_modes = { "Lit", "Unlit", "Emissive" };
            if (property_segmented("Lighting", lighting_modes, &lighting_mode, "Lit is shaded by the scene like smoke. Unlit keeps its own color like vapor. Emissive glows in HDR like fire and sparks."))
            {
                particle_system->SetLightingMode(static_cast<spartan::ParticleLightingMode>(lighting_mode));
            }
            if (lighting_mode == 2)
            {
                property_slider("Glow", &emissive_strength, 0.0f, 100.0f, "%.1f\xC3\x97", "HDR brightness multiplier, values above 1 bloom.", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
            }

            uint32_t blend_mode = static_cast<uint32_t>(particle_system->GetBlendMode());
            static vector<string> blend_modes = { "Alpha", "Premultiplied", "Additive" };
            if (property_segmented("Blend", blend_modes, &blend_mode, "Alpha covers what is behind, for smoke and dust. Premultiplied is alpha for textures authored that way. Additive only brightens, for fire, sparks and magic."))
            {
                particle_system->SetBlendMode(static_cast<spartan::ParticleBlendMode>(blend_mode));
            }

            property_slider("Stretch", &velocity_stretch, 0.0f, 5.0f, velocity_stretch <= 0.0f ? "%.2f  \xC2\xB7  round" : "%.2f", "Stretches particles along their motion, for rain streaks and sparks.");
            property_slider("Soft edges", &soft_depth_scale, 0.0f, 100.0f, "%.1f", "How particles fade where they cut into geometry, lower is softer, higher is a tighter line.", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);

            uint32_t render_mode = static_cast<uint32_t>(particle_system->GetRenderMode());
            static vector<string> render_modes = { "Sprites", "Volumetric" };
            if (property_segmented("Draw as", render_modes, &render_mode, "Sprites are camera-facing quads, cheap and crisp. Volumetric raymarches the particles as density, for thick smoke that lights and shadows itself."))
            {
                particle_system->SetRenderMode(static_cast<spartan::ParticleRenderMode>(render_mode));
            }

            if (render_mode == 1)
            {
                property_slider("Density", &volume_density, 0.0f, 25.0f, "%.2f", "How thick the volume is.", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
                property_slider("Scattering", &volume_anisotropy, -0.9f, 0.9f, volume_anisotropy < -0.1f ? "%.2f  \xC2\xB7  back toward light" : (volume_anisotropy > 0.1f ? "%.2f  \xC2\xB7  glows against light" : "%.2f  \xC2\xB7  even"), "Where the volume sends light. Positive glows when looking toward the sun, like backlit fog.");
                float shadow_percent = volume_shadowing * 100.0f;
                if (property_slider("Self shadow", &shadow_percent, 0.0f, 100.0f, "%.0f%%", "How much dense parts shade the rest, it gives smoke its form."))
                {
                    volume_shadowing = shadow_percent / 100.0f;
                }
            }
        }

        // the sprite texture, and when it is an atlas, how it animates
        {
            RHI_Texture* texture       = particle_system->GetTexture();
            const uint32_t rows        = particle_system->GetFlipbookRows();
            const uint32_t columns     = particle_system->GetFlipbookColumns();
            const uint32_t frames      = rows * columns;
            char summary[64];
            if (!texture)
            {
                snprintf(summary, sizeof(summary), "soft circle");
            }
            else if (frames > 1)
            {
                snprintf(summary, sizeof(summary), "%u frame flipbook", frames);
            }
            else
            {
                snprintf(summary, sizeof(summary), "single image");
            }

            if (layout::fold("Texture", texture != nullptr, summary))
            {
                auto texture_setter = [particle_system](spartan::RHI_Texture* texture_new)
                {
                    particle_system->SetTexture(texture_new);
                };

                layout::begin_property("Sprite", "The image each particle draws, without one a soft procedural circle is used.");
                ImGui::Dummy(ImVec2(0.0f, 0.0f));
                ImGui::SameLine(layout::label_width());
                if (ImGuiSp::image_slot(texture, texture_setter, 56.0f))
                {
                    file_selection::open([particle_system](const std::string& path)
                    {
                        if (FileSystem::IsSupportedImageFile(path))
                        {
                            particle_system->SetTexture(path);
                        }
                    });
                }

                ImGui::BeginDisabled(!texture);
                int grid[2] = { static_cast<int>(columns), static_cast<int>(rows) };
                layout::begin_property("Flipbook grid", "Columns and rows when the texture is an atlas of animation frames, 1 by 1 is a single image.");
                if (ImGui::SliderInt2("##flipbook_grid", grid, 1, 32, "%d", ImGuiSliderFlags_AlwaysClamp))
                {
                    particle_system->SetFlipbookColumns(static_cast<uint32_t>(grid[0]));
                    particle_system->SetFlipbookRows(static_cast<uint32_t>(grid[1]));
                }
                ImGui::EditorUi::decorate_field();

                if (frames > 1)
                {
                    float fps = particle_system->GetFlipbookFps();
                    char format[64];
                    if (fps <= 0.0f)
                    {
                        snprintf(format, sizeof(format), "%%.0f fps  \xC2\xB7  one pass over the lifetime");
                    }
                    else
                    {
                        snprintf(format, sizeof(format), "%%.0f fps  \xC2\xB7  loops every %.2f s", frames / fps);
                    }
                    if (property_slider("Playback", &fps, 0.0f, 120.0f, format, "Frames per second, 0 stretches the frames over each particle's life."))
                    {
                        particle_system->SetFlipbookFps(fps);
                    }
                }
                ImGui::EndDisabled();
            }
        }

        //= MAP ==========================================================
        if (emission_rate != particle_system->GetEmissionRate())
        {
            particle_system->SetEmissionRate(emission_rate);
        }
        if (lifetime != particle_system->GetLifetime())
        {
            particle_system->SetLifetime(lifetime);
        }
        if (start_speed != particle_system->GetStartSpeed())
        {
            particle_system->SetStartSpeed(start_speed);
        }
        if (start_size != particle_system->GetStartSize())
        {
            particle_system->SetStartSize(start_size);
        }
        if (end_size != particle_system->GetEndSize())
        {
            particle_system->SetEndSize(end_size);
        }
        if (gravity_modifier != particle_system->GetGravityModifier())
        {
            particle_system->SetGravityModifier(gravity_modifier);
        }
        if (emission_radius != particle_system->GetEmissionRadius())
        {
            particle_system->SetEmissionRadius(emission_radius);
        }
        if (emission_direction != particle_system->GetEmissionDirection())
        {
            particle_system->SetEmissionDirection(emission_direction);
        }
        if (emission_cone_angle != particle_system->GetEmissionConeAngle())
        {
            particle_system->SetEmissionConeAngle(emission_cone_angle);
        }
        if (directional_blend != particle_system->GetDirectionalBlend())
        {
            particle_system->SetDirectionalBlend(directional_blend);
        }
        if (emissive_strength != particle_system->GetEmissiveStrength())
        {
            particle_system->SetEmissiveStrength(emissive_strength);
        }
        if (soft_depth_scale != particle_system->GetSoftDepthScale())
        {
            particle_system->SetSoftDepthScale(soft_depth_scale);
        }
        if (volume_density != particle_system->GetVolumeDensity())
        {
            particle_system->SetVolumeDensity(volume_density);
        }
        if (volume_anisotropy != particle_system->GetVolumeAnisotropy())
        {
            particle_system->SetVolumeAnisotropy(volume_anisotropy);
        }
        if (volume_shadowing != particle_system->GetVolumeShadowing())
        {
            particle_system->SetVolumeShadowing(volume_shadowing);
        }
        if (drag != particle_system->GetDrag())
        {
            particle_system->SetDrag(drag);
        }
        if (turbulence_strength != particle_system->GetTurbulenceStrength())
        {
            particle_system->SetTurbulenceStrength(turbulence_strength);
        }
        if (wind_influence != particle_system->GetWindInfluence())
        {
            particle_system->SetWindInfluence(wind_influence);
        }
        if (velocity_inheritance != particle_system->GetVelocityInheritance())
        {
            particle_system->SetVelocityInheritance(velocity_inheritance);
        }
        if (velocity_stretch != particle_system->GetVelocityStretch())
        {
            particle_system->SetVelocityStretch(velocity_stretch);
        }
        if (vortex_strength != particle_system->GetVortexStrength())
        {
            particle_system->SetVortexStrength(vortex_strength);
        }
        if (vortex_radius != particle_system->GetVortexRadius())
        {
            particle_system->SetVortexRadius(vortex_radius);
        }
        if (thermal_strength != particle_system->GetThermalStrength())
        {
            particle_system->SetThermalStrength(thermal_strength);
        }
        if (thermal_decay != particle_system->GetThermalDecay())
        {
            particle_system->SetThermalDecay(thermal_decay);
        }
        if (rollup_strength != particle_system->GetRollupStrength())
        {
            particle_system->SetRollupStrength(rollup_strength);
        }
        if (wake_strength != particle_system->GetWakeStrength())
        {
            particle_system->SetWakeStrength(wake_strength);
        }
        if (churn_strength != particle_system->GetChurnStrength())
        {
            particle_system->SetChurnStrength(churn_strength);
        }
        if (collision_clearance != particle_system->GetCollisionClearance())
        {
            particle_system->SetCollisionClearance(collision_clearance);
        }
        if (spawn_burst != particle_system->GetSpawnBurst())
        {
            particle_system->SetSpawnBurst(spawn_burst);
        }
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

    // one row of console keys, the primary action leads and the prefab key sits beside it
    Entity* entity        = get_selected_entity();
    const bool can_prefab = entity && !entity->IsCodePrefab();
    const float gap       = design::spacing_md * spartan::Window::GetDpiScale();
    const float avail     = ImGui::GetContentRegionAvail().x;
    const float add_width = can_prefab ? IM_ROUND((avail - gap) * 0.5f) : avail;

    if (ImGuiSp::command_button("Add Component", ImVec2(add_width, 0.0f), true))
    {
        ImGui::OpenPopup("##ComponentContextMenu_Add");
    }
    ComponentContextMenu_Add();

    // hidden for code prefabs since baking one to a file loses its code behavior
    if (can_prefab)
    {
        ImGui::SameLine(0.0f, gap);
        if (ImGuiSp::command_button("Save as Prefab", ImVec2(avail - add_width - gap, 0.0f), false))
        {
            ImGui::OpenPopup("##SaveAsPrefab");
        }
        ShowSaveAsPrefabPopup(entity);
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
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::Style::color_text);
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
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::Style::color_text_muted);
        ImGui::Text("file: %s", preview_path.c_str());
        ImGui::PopStyleColor();

        ImGui::Dummy(ImVec2(0, design::spacing_sm));

        // save button
        bool name_valid = strlen(prefab_name) > 0;
        ImGui::BeginDisabled(!name_valid);
        if (primary_button("Save", ImVec2(80.0f, 0)))
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
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::Style::color_text_muted);
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
                ImGui::Style::color_text_muted
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

            if (ImGui::MenuItem("Navigation"))
            {
                entity->AddComponent<Navigation>();
            }

            ImGui::Dummy(ImVec2(0, design::spacing_sm));

            // rendering
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::Style::color_text_muted);
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
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::Style::color_text_muted);
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
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::Style::color_text_muted);
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
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::Style::color_text_muted);
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
