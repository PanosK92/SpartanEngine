/*
Copyright(c) 2016-2020 Panos Karabelas

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

//= INCLUDES ====================================
#include "Widget_Properties.h"
#include "../ImGui_Extension.h"
#include "../WidgetsDeferred/ButtonColorPicker.h"
#include "../ImGui/Source/imgui_stdlib.h"
#include "Core/Engine.h"
#include "Rendering/Model.h"
#include "World/Entity.h"
#include "World/Components/Transform.h"
#include "World/Components/Renderable.h"
#include "World/Components/RigidBody.h"
#include "World/Components/SoftBody.h"
#include "World/Components/Collider.h"
#include "World/Components/Constraint.h"
#include "World/Components/Light.h"
#include "World/Components/AudioSource.h"
#include "World/Components/AudioListener.h"
#include "World/Components/Script.h"
#include "World/Components/Environment.h"
#include "World/Components/Terrain.h"
//===============================================

//= NAMESPACES =========
using namespace std;
using namespace Spartan;
using namespace Math;
//======================

weak_ptr<Entity> Widget_Properties::m_inspected_entity;
weak_ptr<Material> Widget_Properties::m_inspected_material;

namespace _Widget_Properties
{
    static ResourceCache* resource_cache;
    static World* scene;
    static Vector3 rotation_hint;
}

namespace ComponentProperty
{
    static string g_contex_menu_id;
    static float g_column = 180.0f;
    static const float g_max_width = 100.0f;
    static IComponent* g_copied;

    inline void ComponentContextMenu_Options(const string& id, IComponent* component, const bool removable)
    {
        if (ImGui::BeginPopup(id.c_str()))
        {
            if (removable)
            {
                if (ImGui::MenuItem("Remove"))
                {
                    if (auto entity = Widget_Properties::m_inspected_entity.lock())
                    {
                        if (component)
                        {
                            entity->RemoveComponentById(component->GetId());
                        }
                    }
                }
            }

            if (ImGui::MenuItem("Copy Attributes"))
            {
                g_copied = component;
            }

            if (ImGui::MenuItem("Paste Attributes"))
            {
                if (g_copied && g_copied->GetType() == component->GetType())
                {
                    component->SetAttributes(g_copied->GetAttributes());
                }
            }

            ImGui::EndPopup();
        }
    }

    inline bool Begin(const string& name, const Icon_Type icon_enum, IComponent* component_instance, bool options = true, const bool removable = true)
    {
        // Collapsible contents
        const auto collapsed = ImGui::CollapsingHeader(name.c_str(), ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_DefaultOpen);

        // Component Icon - Top left
        ImGui::SameLine();
        ImGui::Spacing();
        ImGui::SameLine();
        const auto original_pen_y = ImGui::GetCursorPosY();
        ImGui::SetCursorPosY(original_pen_y + 5.0f);
        ImGuiEx::Image(icon_enum, 15);

        // Component Options - Top right
        if (options)
        {
            const float icon_width = 16.0f;
            ImGui::SameLine(ImGui::GetWindowContentRegionWidth() - icon_width + 1.0f); ImGui::SetCursorPosY(original_pen_y);
            if (ImGuiEx::ImageButton(name.c_str(), Icon_Component_Options, icon_width))
            {
                g_contex_menu_id = name;
                ImGui::OpenPopup(g_contex_menu_id.c_str());
            }

            if (g_contex_menu_id == name)
            {
                ComponentContextMenu_Options(g_contex_menu_id, component_instance, removable);
            }
        }

        return collapsed;
    }

    inline void End()
    {
        ImGui::Separator();
    }
}

Widget_Properties::Widget_Properties(Editor* editor) : Widget(editor)
{
    m_title        = "Properties";
    m_size.x    = 500; // min width

    m_colorPicker_light        = make_unique<ButtonColorPicker>("Light Color Picker");
    m_colorPicker_material    = make_unique<ButtonColorPicker>("Material Color Picker");
    m_colorPicker_camera    = make_unique<ButtonColorPicker>("Camera Color Picker");

    _Widget_Properties::resource_cache    = m_context->GetSubsystem<ResourceCache>();
    _Widget_Properties::scene            = m_context->GetSubsystem<World>();  
}

void Widget_Properties::Tick()
{
    ImGui::PushItemWidth(ComponentProperty::g_max_width);

    if (!m_inspected_entity.expired())
    {
        auto entity_ptr         = m_inspected_entity.lock().get();
        Renderable* renderable  = entity_ptr->GetComponent<Renderable>();
        Material* material      = renderable ? renderable->GetMaterial() : nullptr;

        ShowTransform(entity_ptr->GetComponent<Transform>());
        ShowLight(entity_ptr->GetComponent<Light>());
        ShowCamera(entity_ptr->GetComponent<Camera>());
        ShowTerrain(entity_ptr->GetComponent<Terrain>());
        ShowEnvironment(entity_ptr->GetComponent<Environment>());
        ShowAudioSource(entity_ptr->GetComponent<AudioSource>());
        ShowAudioListener(entity_ptr->GetComponent<AudioListener>());
        ShowRenderable(renderable);
        ShowMaterial(material);
        ShowRigidBody(entity_ptr->GetComponent<RigidBody>());
        ShowSoftBody(entity_ptr->GetComponent<SoftBody>());
        ShowCollider(entity_ptr->GetComponent<Collider>());
        ShowConstraint(entity_ptr->GetComponent<Constraint>());
        for (auto& script : entity_ptr->GetComponents<Script>())
        {
            ShowScript(script);
        }

        ShowAddComponentButton();
        Drop_AutoAddComponents();
    }
    else if (!m_inspected_material.expired())
    {
        ShowMaterial(m_inspected_material.lock().get());
    }

    ImGui::PopItemWidth();
}

void Widget_Properties::Inspect(const weak_ptr<Entity>& entity)
{
    m_inspected_entity = entity;

    if (const auto shared_ptr = entity.lock())
    {
        _Widget_Properties::rotation_hint = shared_ptr->GetTransform()->GetRotationLocal().ToEulerAngles();
    }
    else
    {
        _Widget_Properties::rotation_hint = Vector3::Zero;
    }

    // If we were previously inspecting a material, save the changes
    if (!m_inspected_material.expired())
    {
        m_inspected_material.lock()->SaveToFile(m_inspected_material.lock()->GetResourceFilePathNative());
    }
    m_inspected_material.reset();
}

void Widget_Properties::Inspect(const weak_ptr<Material>& material)
{
    m_inspected_entity.reset();
    m_inspected_material = material;
}

void Widget_Properties::ShowTransform(Transform* transform) const
{
    if (ComponentProperty::Begin("Transform", Icon_Component_Transform, transform, true, false))
    {
        const bool is_playing = m_context->m_engine->EngineMode_IsSet(Engine_Game);

        //= REFLECT ==========================================================================================================
        Vector3 position    = transform->GetPositionLocal();
        Vector3 rotation    = !is_playing ? _Widget_Properties::rotation_hint : transform->GetRotationLocal().ToEulerAngles();
        Vector3 scale        = transform->GetScaleLocal();
        //====================================================================================================================

        const auto show_float = [](const char* label, float* value) 
        {
            const float label_float_spacing = 15.0f;
            const float step                = 0.01f;
            const string format                   = "%.4f";

            // Label
            ImGui::TextUnformatted(label);
            ImGui::SameLine(label_float_spacing);

            // Float
            ImGui::PushItemWidth(128.0f);
            ImGui::PushID(static_cast<int>(ImGui::GetCursorPosX() + ImGui::GetCursorPosY()));
            ImGuiEx::DragFloatWrap("##no_label", value, step, numeric_limits<float>::lowest(), numeric_limits<float>::max(), format.c_str());
            ImGui::PopID();
            ImGui::PopItemWidth();
        };

        const auto show_vector = [&show_float](const char* label, Vector3& vector)
        {
            const float label_indetation = 15.0f;

            ImGui::BeginGroup();
            ImGui::Indent(label_indetation);
            ImGui::TextUnformatted(label);
            ImGui::Unindent(label_indetation);
            show_float("X", &vector.x);
            show_float("Y", &vector.y);
            show_float("Z", &vector.z);
            ImGui::EndGroup();
        };
       
        show_vector("Position", position);
        ImGui::SameLine();
        show_vector("Rotation", rotation);
        ImGui::SameLine();
        show_vector("Scale", scale);
        
        //= MAP ===================================================================
        if (!is_playing)
        {
            transform->SetPositionLocal(position);
            transform->SetScaleLocal(scale);

            if (rotation != _Widget_Properties::rotation_hint)
            {
                transform->SetRotationLocal(Quaternion::FromEulerAngles(rotation));
                _Widget_Properties::rotation_hint = rotation;
            }
        }
        //=========================================================================
    }
    ComponentProperty::End();
}

void Widget_Properties::ShowLight(Light* light) const
{
    if (!light)
        return;

    if (ComponentProperty::Begin("Light", Icon_Component_Light, light))
    {
        //= REFLECT =================================================================
        static vector<char*> types  = { "Directional", "Point", "Spot" };
        const char* type_char_ptr   = types[static_cast<int>(light->GetLightType())];
        float intensity             = light->GetIntensity();
        float angle                 = light->GetAngle() * Math::Helper::RAD_TO_DEG;
        bool shadows                = light->GetShadowsEnabled();
        bool shadows_screen_space   = light->GetShadowsScreenSpaceEnabled();
        bool shadows_transparent    = light->GetShadowsTransparentEnabled();
        bool volumetric             = light->GetVolumetricEnabled();
        float bias                  = light->GetBias();
        float normal_bias           = light->GetNormalBias();
        float range                 = light->GetRange();
        float time_of_day           = light->GetTimeOfDay();
        m_colorPicker_light->SetColor(light->GetColor());

        bool is_directional = light->GetLightType() == LightType::Directional;
        //===========================================================================

        // Type
        ImGui::Text("Type");
        ImGui::PushItemWidth(110.0f);
        ImGui::SameLine(ComponentProperty::g_column); if (ImGui::BeginCombo("##LightType", type_char_ptr))
        {
            for (unsigned int i = 0; i < static_cast<unsigned int>(types.size()); i++)
            {
                const auto is_selected = (type_char_ptr == types[i]);
                if (ImGui::Selectable(types[i], is_selected))
                {
                    type_char_ptr = types[i];
                    light->SetLightType(static_cast<LightType>(i));
                }
                if (is_selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopItemWidth();

        // Time of day
        //if (light->GetLightType() == LightType_Directional)
        //{
        //    ImGui::Text("Time of day");
        //    ImGui::SameLine(ComponentProperty::g_column);
        //    ImGui::PushItemWidth(300); ImGuiEx::DragFloatWrap("##lightTime", &time_of_day, 0.1f, 0.0f, 24.0f); ImGui::PopItemWidth();
        //}

        // Color
        ImGui::Text("Color");
        ImGui::SameLine(ComponentProperty::g_column); m_colorPicker_light->Update();

        // Intensity
        ImGui::Text(is_directional ? "Intensity (Lux)" : "Intensity (Lumens)");
        ImGui::SameLine(ComponentProperty::g_column);
        float v_speed   = is_directional ? 20.0f : 5.0f;
        float v_max     = is_directional ? 128000.0f : 100000.0f;
        ImGui::PushItemWidth(300); ImGuiEx::DragFloatWrap("##lightIntensity", &intensity, v_speed, 0.0f, v_max); ImGui::PopItemWidth();

        // Shadows
        ImGui::Text("Shadows");
        ImGui::SameLine(ComponentProperty::g_column); ImGui::Checkbox("##light_shadows", &shadows);

        // Screen space shadows
        ImGui::Text("Screen Space Shadows");
        ImGui::SameLine(ComponentProperty::g_column); ImGui::Checkbox("##light_shadows_screen_space", &shadows_screen_space);
        ImGuiEx::Tooltip("Small scale shadows which add detail were surfaces meet, also known as contact shadows");

        if (shadows)
        {
            // Transparent shadows
            ImGui::Text("Transparent Shadows");
            ImGui::SameLine(ComponentProperty::g_column); ImGui::Checkbox("##light_shadows_transparent", &shadows_transparent);
            ImGuiEx::Tooltip("Allows transparent objects to cast colored translucent shadows");
        }

        // Volumetric
        ImGui::Text("Volumetric");
        ImGui::SameLine(ComponentProperty::g_column); ImGui::Checkbox("##light_volumetric", &volumetric);
        ImGuiEx::Tooltip("The shadow map is used to determine which parts of the \"air\" should be lit");

        // Bias
        ImGui::Text("Bias");
        ImGui::SameLine(ComponentProperty::g_column);
        ImGui::PushItemWidth(300); ImGui::InputFloat("##lightBias", &bias, 1.0f, 1.0f, "%.0f"); ImGui::PopItemWidth();

        // Normal Bias
        ImGui::Text("Normal Bias");
        ImGui::SameLine(ComponentProperty::g_column);
        ImGui::PushItemWidth(300); ImGui::InputFloat("##lightNormalBias", &normal_bias, 1.0f, 1.0f, "%.0f"); ImGui::PopItemWidth();

        // Range
        if (light->GetLightType() != LightType::Directional)
        {
            ImGui::Text("Range");
            ImGui::SameLine(ComponentProperty::g_column);
            ImGui::PushItemWidth(300); ImGuiEx::DragFloatWrap("##lightRange", &range, 0.01f, 0.0f, 100.0f); ImGui::PopItemWidth();
        }

        // Angle
        if (light->GetLightType() == LightType::Spot)
        {
            ImGui::Text("Angle");
            ImGui::SameLine(ComponentProperty::g_column);
            ImGui::PushItemWidth(300); ImGuiEx::DragFloatWrap("##lightAngle", &angle, 0.01f, 1.0f, 179.0f); ImGui::PopItemWidth();
        }

        //= MAP ======================================================================================================================
        if (intensity != light->GetIntensity())                             light->SetIntensity(intensity);
        if (shadows != light->GetShadowsEnabled())                          light->SetShadowsEnabled(shadows);
        if (shadows_screen_space != light->GetShadowsScreenSpaceEnabled())  light->SetShadowsScreenSpaceEnabled(shadows_screen_space);
        if (shadows_transparent != light->GetShadowsTransparentEnabled())   light->SetShadowsTransparentEnabled(shadows_transparent);
        if (volumetric != light->GetVolumetricEnabled())                    light->SetVolumetricEnabled(volumetric);
        if (bias != light->GetBias())                                       light->SetBias(bias);
        if (normal_bias != light->GetNormalBias())                          light->SetNormalBias(normal_bias);
        if (angle != light->GetAngle() * Math::Helper::RAD_TO_DEG)          light->SetAngle(angle * Math::Helper::DEG_TO_RAD);
        if (range != light->GetRange())                                     light->SetRange(range);
        if (time_of_day != light->GetTimeOfDay())                           light->SetTimeOfDay(time_of_day);
        if (m_colorPicker_light->GetColor() != light->GetColor())           light->SetColor(m_colorPicker_light->GetColor());
        //============================================================================================================================
    }
    ComponentProperty::End();
}

void Widget_Properties::ShowRenderable(Renderable* renderable) const
{
    if (!renderable)
        return;

    if (ComponentProperty::Begin("Renderable", Icon_Component_Renderable, renderable))
    {
        //= REFLECT =============================================================
        const string& mesh_name = renderable->GeometryName();
        Material* material      = renderable->GetMaterial();
        string material_name    = material ? material->GetResourceName() : "N/A";
        bool cast_shadows       = renderable->GetCastShadows();
        //=======================================================================

        ImGui::Text("Mesh");
        ImGui::SameLine(ComponentProperty::g_column); ImGui::Text(mesh_name.c_str());

        // Material
        ImGui::Text("Material");
        ImGui::SameLine(ComponentProperty::g_column);
        ImGui::PushID("##material_name");
        ImGui::PushItemWidth(200.0f);
        ImGui::InputText("", &material_name, ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_ReadOnly);
        if (auto payload = ImGuiEx::ReceiveDragPayload(ImGuiEx::DragPayload_Material))
        {
            renderable->SetMaterial(std::get<const char*>(payload->data));
        }
        ImGui::PopItemWidth();
        ImGui::PopID();

        // Cast shadows
        ImGui::Text("Cast Shadows");
        ImGui::SameLine(ComponentProperty::g_column); ImGui::Checkbox("##RenderableCastShadows", &cast_shadows);

        //= MAP ===================================================================================
        if (cast_shadows != renderable->GetCastShadows()) renderable->SetCastShadows(cast_shadows);
        //=========================================================================================
    }
    ComponentProperty::End();
}

void Widget_Properties::ShowRigidBody(RigidBody* rigid_body) const
{
    if (!rigid_body)
        return;

    if (ComponentProperty::Begin("RigidBody", Icon_Component_RigidBody, rigid_body))
    {
        //= REFLECT ================================================================
        auto mass               = rigid_body->GetMass();
        auto friction           = rigid_body->GetFriction();
        auto friction_rolling   = rigid_body->GetFrictionRolling();
        auto restitution        = rigid_body->GetRestitution();
        auto use_gravity        = rigid_body->GetUseGravity();
        auto is_kinematic       = rigid_body->GetIsKinematic();
        auto freeze_pos_x       = static_cast<bool>(rigid_body->GetPositionLock().x);
        auto freeze_pos_y       = static_cast<bool>(rigid_body->GetPositionLock().y);
        auto freeze_pos_z       = static_cast<bool>(rigid_body->GetPositionLock().z);
        auto freeze_rot_x       = static_cast<bool>(rigid_body->GetRotationLock().x);
        auto freeze_rot_y       = static_cast<bool>(rigid_body->GetRotationLock().y);
        auto freeze_rot_z       = static_cast<bool>(rigid_body->GetRotationLock().z);
        //==========================================================================

        const auto input_text_flags        = ImGuiInputTextFlags_CharsDecimal;
        const auto item_width            = 120.0f;
        const auto step                    = 0.1f;
        const auto step_fast            = 0.1f;
        const auto precision            = "%.3f";

        // Mass
        ImGui::Text("Mass");
        ImGui::SameLine(ComponentProperty::g_column); ImGui::PushItemWidth(item_width); ImGui::InputFloat("##RigidBodyMass", &mass, step, step_fast, precision, input_text_flags); ImGui::PopItemWidth();

        // Friction
        ImGui::Text("Friction");
        ImGui::SameLine(ComponentProperty::g_column); ImGui::PushItemWidth(item_width); ImGui::InputFloat("##RigidBodyFriction", &friction, step, step_fast, precision, input_text_flags); ImGui::PopItemWidth();

        // Rolling Friction
        ImGui::Text("Rolling Friction");
        ImGui::SameLine(ComponentProperty::g_column); ImGui::PushItemWidth(item_width); ImGui::InputFloat("##RigidBodyRollingFriction", &friction_rolling, step, step_fast, precision, input_text_flags); ImGui::PopItemWidth();

        // Restitution
        ImGui::Text("Restitution");
        ImGui::SameLine(ComponentProperty::g_column); ImGui::PushItemWidth(item_width); ImGui::InputFloat("##RigidBodyRestitution", &restitution, step, step_fast, precision, input_text_flags); ImGui::PopItemWidth();

        // Use Gravity
        ImGui::Text("Use Gravity");
        ImGui::SameLine(ComponentProperty::g_column); ImGui::Checkbox("##RigidBodyUseGravity", &use_gravity);

        // Is Kinematic
        ImGui::Text("Is Kinematic");
        ImGui::SameLine(ComponentProperty::g_column); ImGui::Checkbox("##RigidBodyKinematic", &is_kinematic);

        // Freeze Position
        ImGui::Text("Freeze Position");
        ImGui::SameLine(ComponentProperty::g_column); ImGui::Text("X");
        ImGui::SameLine(); ImGui::Checkbox("##RigidFreezePosX", &freeze_pos_x);
        ImGui::SameLine(); ImGui::Text("Y");
        ImGui::SameLine(); ImGui::Checkbox("##RigidFreezePosY", &freeze_pos_y);
        ImGui::SameLine(); ImGui::Text("Z");
        ImGui::SameLine(); ImGui::Checkbox("##RigidFreezePosZ", &freeze_pos_z);

        // Freeze Rotation
        ImGui::Text("Freeze Rotation");
        ImGui::SameLine(ComponentProperty::g_column); ImGui::Text("X");
        ImGui::SameLine(); ImGui::Checkbox("##RigidFreezeRotX", &freeze_rot_x);
        ImGui::SameLine(); ImGui::Text("Y");
        ImGui::SameLine(); ImGui::Checkbox("##RigidFreezeRotY", &freeze_rot_y);
        ImGui::SameLine(); ImGui::Text("Z");
        ImGui::SameLine(); ImGui::Checkbox("##RigidFreezeRotZ", &freeze_rot_z);

        //= MAP ===========================================================================================================================================================================================================
        if (mass != rigid_body->GetMass())                                      rigid_body->SetMass(mass);
        if (friction != rigid_body->GetFriction())                              rigid_body->SetFriction(friction);
        if (friction_rolling != rigid_body->GetFrictionRolling())               rigid_body->SetFrictionRolling(friction_rolling);
        if (restitution != rigid_body->GetRestitution())                        rigid_body->SetRestitution(restitution);
        if (use_gravity != rigid_body->GetUseGravity())                         rigid_body->SetUseGravity(use_gravity);
        if (is_kinematic != rigid_body->GetIsKinematic())                       rigid_body->SetIsKinematic(is_kinematic);
        if (freeze_pos_x != static_cast<bool>(rigid_body->GetPositionLock().x)) rigid_body->SetPositionLock(Vector3(static_cast<float>(freeze_pos_x), static_cast<float>(freeze_pos_y), static_cast<float>(freeze_pos_z)));
        if (freeze_pos_y != static_cast<bool>(rigid_body->GetPositionLock().y)) rigid_body->SetPositionLock(Vector3(static_cast<float>(freeze_pos_x), static_cast<float>(freeze_pos_y), static_cast<float>(freeze_pos_z)));
        if (freeze_pos_z != static_cast<bool>(rigid_body->GetPositionLock().z)) rigid_body->SetPositionLock(Vector3(static_cast<float>(freeze_pos_x), static_cast<float>(freeze_pos_y), static_cast<float>(freeze_pos_z)));
        if (freeze_rot_x != static_cast<bool>(rigid_body->GetRotationLock().x)) rigid_body->SetRotationLock(Vector3(static_cast<float>(freeze_rot_x), static_cast<float>(freeze_rot_y), static_cast<float>(freeze_rot_z)));
        if (freeze_rot_y != static_cast<bool>(rigid_body->GetRotationLock().y)) rigid_body->SetRotationLock(Vector3(static_cast<float>(freeze_rot_x), static_cast<float>(freeze_rot_y), static_cast<float>(freeze_rot_z)));
        if (freeze_rot_z != static_cast<bool>(rigid_body->GetRotationLock().z)) rigid_body->SetRotationLock(Vector3(static_cast<float>(freeze_rot_x), static_cast<float>(freeze_rot_y), static_cast<float>(freeze_rot_z)));
        //=================================================================================================================================================================================================================
    }
    ComponentProperty::End();
}

void Widget_Properties::ShowSoftBody(SoftBody* soft_body) const
{
    if (!soft_body)
        return;

    if (ComponentProperty::Begin("SoftBody", Icon_Component_SoftBody, soft_body))
    {
        //= REFLECT ===============================================================
        //=========================================================================

        //= MAP ===================================================================
        //=========================================================================
    }
    ComponentProperty::End();
}

void Widget_Properties::ShowCollider(Collider* collider) const
{
    if (!collider)
        return;

    if (ComponentProperty::Begin("Collider", Icon_Component_Collider, collider))
    {
        //= REFLECT =======================================================================
        static vector<string> type = {
            "Box",
            "Sphere",
            "Static Plane",
            "Cylinder",
            "Capsule",
            "Cone",
            "Mesh"
        };
        const char* shape_char_ptr        = type[static_cast<int>(collider->GetShapeType())].c_str();
        bool optimize                    = collider->GetOptimize();
        Vector3 collider_center            = collider->GetCenter();
        Vector3 collider_bounding_box    = collider->GetBoundingBox();
        //=================================================================================

        const auto input_text_flags        = ImGuiInputTextFlags_CharsDecimal;
        const auto step                    = 0.1f;
        const auto step_fast            = 0.1f;
        const auto precision            = "%.3f";

        // Type
        ImGui::Text("Type");
        ImGui::PushItemWidth(110);
        ImGui::SameLine(ComponentProperty::g_column); if (ImGui::BeginCombo("##colliderType", shape_char_ptr))
        {
            for (unsigned int i = 0; i < static_cast<unsigned int>(type.size()); i++)
            {
                const auto is_selected = (shape_char_ptr == type[i]);
                if (ImGui::Selectable(type[i].c_str(), is_selected))
                {
                    shape_char_ptr = type[i].c_str();
                    collider->SetShapeType(static_cast<ColliderShape>(i));
                }
                if (is_selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopItemWidth();

        // Center
        ImGui::Text("Center");
        ImGui::SameLine(ComponentProperty::g_column);   ImGui::PushID("colCenterX"); ImGui::InputFloat("X", &collider_center.x, step, step_fast, precision, input_text_flags); ImGui::PopID();
        ImGui::SameLine();                              ImGui::PushID("colCenterY"); ImGui::InputFloat("Y", &collider_center.y, step, step_fast, precision, input_text_flags); ImGui::PopID();
        ImGui::SameLine();                              ImGui::PushID("colCenterZ"); ImGui::InputFloat("Z", &collider_center.z, step, step_fast, precision, input_text_flags); ImGui::PopID();

        // Size
        ImGui::Text("Size");
        ImGui::SameLine(ComponentProperty::g_column);    ImGui::PushID("colSizeX"); ImGui::InputFloat("X", &collider_bounding_box.x, step, step_fast, precision, input_text_flags); ImGui::PopID();
        ImGui::SameLine();                               ImGui::PushID("colSizeY"); ImGui::InputFloat("Y", &collider_bounding_box.y, step, step_fast, precision, input_text_flags); ImGui::PopID();
        ImGui::SameLine();                               ImGui::PushID("colSizeZ"); ImGui::InputFloat("Z", &collider_bounding_box.z, step, step_fast, precision, input_text_flags); ImGui::PopID();

        // Optimize
        if (collider->GetShapeType() == ColliderShape_Mesh)
        {
            ImGui::Text("Optimize");
            ImGui::SameLine(ComponentProperty::g_column); ImGui::Checkbox("##colliderOptimize", &optimize);
        }

        //= MAP ====================================================================================================
        if (collider_center != collider->GetCenter())               collider->SetCenter(collider_center);
        if (collider_bounding_box != collider->GetBoundingBox())    collider->SetBoundingBox(collider_bounding_box);
        if (optimize != collider->GetOptimize())                    collider->SetOptimize(optimize);
        //==========================================================================================================
    }
    ComponentProperty::End();
}

void Widget_Properties::ShowConstraint(Constraint* constraint) const
{
    if (!constraint)
        return;

    if (ComponentProperty::Begin("Constraint", Icon_Component_AudioSource, constraint))
    {
        //= REFLECT ==========================================================================
        vector<char*> types     = {"Point", "Hinge", "Slider", "ConeTwist" };
        const char* type_str    = types[static_cast<int>(constraint->GetConstraintType())];
        auto other_body         = constraint->GetBodyOther();
        bool other_body_dirty   = false;
        Vector3 position        = constraint->GetPosition();
        Vector3 rotation        = constraint->GetRotation().ToEulerAngles();
        Vector2 high_limit      = constraint->GetHighLimit();
        Vector2 low_limit       = constraint->GetLowLimit();
        string other_body_name  = other_body.expired() ? "N/A" : other_body.lock()->GetName();
        //====================================================================================

        const auto inputTextFlags   = ImGuiInputTextFlags_CharsDecimal;
        const float step            = 0.1f;
        const float step_fast       = 0.1f;
        const char* precision       = "%.3f";

        // Type
        ImGui::Text("Type");
        ImGui::SameLine(ComponentProperty::g_column); if (ImGui::BeginCombo("##constraintType", type_str))
        {
            for (unsigned int i = 0; i < (unsigned int)types.size(); i++)
            {
                const bool is_selected = (type_str == types[i]);
                if (ImGui::Selectable(types[i], is_selected))
                {
                    type_str = types[i];
                    constraint->SetConstraintType(static_cast<ConstraintType>(i));
                }
                if (is_selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        // Other body
        ImGui::Text("Other Body"); ImGui::SameLine(ComponentProperty::g_column);
        ImGui::PushID("##OtherBodyName");
        ImGui::PushItemWidth(200.0f);
        ImGui::InputText("", &other_body_name, ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_ReadOnly);
        if (auto payload = ImGuiEx::ReceiveDragPayload(ImGuiEx::DragPayload_Entity))
        {
            const auto entity_id    = get<unsigned int>(payload->data);
            other_body                = _Widget_Properties::scene->EntityGetById(entity_id);
            other_body_dirty        = true;
        }
        ImGui::PopItemWidth();
        ImGui::PopID();

        // Position
        ImGui::Text("Position");
        ImGui::SameLine(ComponentProperty::g_column); ImGui::Text("X");
        ImGui::SameLine(); ImGui::InputFloat("##ConsPosX", &position.x, step, step_fast, precision, inputTextFlags);
        ImGui::SameLine(); ImGui::Text("Y");
        ImGui::SameLine(); ImGui::InputFloat("##ConsPosY", &position.y, step, step_fast, precision, inputTextFlags);
        ImGui::SameLine(); ImGui::Text("Z");
        ImGui::SameLine(); ImGui::InputFloat("##ConsPosZ", &position.z, step, step_fast, precision, inputTextFlags);

        // Rotation
        ImGui::Text("Rotation");
        ImGui::SameLine(ComponentProperty::g_column); ImGui::Text("X");
        ImGui::SameLine(); ImGui::InputFloat("##ConsRotX", &rotation.x, step, step_fast, precision, inputTextFlags);
        ImGui::SameLine(); ImGui::Text("Y");
        ImGui::SameLine(); ImGui::InputFloat("##ConsRotY", &rotation.y, step, step_fast, precision, inputTextFlags);
        ImGui::SameLine(); ImGui::Text("Z");
        ImGui::SameLine(); ImGui::InputFloat("##ConsRotZ", &rotation.z, step, step_fast, precision, inputTextFlags);

        // High Limit
        ImGui::Text("High Limit");
        ImGui::SameLine(ComponentProperty::g_column); ImGui::Text("X");
        ImGui::SameLine(); ImGui::InputFloat("##ConsHighLimX", &high_limit.x, step, step_fast, precision, inputTextFlags);
        if (constraint->GetConstraintType() == ConstraintType_Slider)
        {
            ImGui::SameLine(); ImGui::Text("Y");
            ImGui::SameLine(); ImGui::InputFloat("##ConsHighLimY", &high_limit.y, step, step_fast, precision, inputTextFlags);
        }

        // Low Limit
        ImGui::Text("Low Limit");
        ImGui::SameLine(ComponentProperty::g_column); ImGui::Text("X");
        ImGui::SameLine(); ImGui::InputFloat("##ConsLowLimX", &low_limit.x, step, step_fast, precision, inputTextFlags);
        if (constraint->GetConstraintType() == ConstraintType_Slider)
        {
            ImGui::SameLine(); ImGui::Text("Y");
            ImGui::SameLine(); ImGui::InputFloat("##ConsLowLimY", &low_limit.y, step, step_fast, precision, inputTextFlags);
        }

        //= MAP =======================================================================================================================
        if (other_body_dirty)                                       { constraint->SetBodyOther(other_body); other_body_dirty = false; }
        if (position != constraint->GetPosition())                  constraint->SetPosition(position);
        if (rotation != constraint->GetRotation().ToEulerAngles())  constraint->SetRotation(Quaternion::FromEulerAngles(rotation));
        if (high_limit != constraint->GetHighLimit())               constraint->SetHighLimit(high_limit);
        if (low_limit != constraint->GetLowLimit())                 constraint->SetLowLimit(low_limit);
        //=============================================================================================================================
    }
    ComponentProperty::End();
}

void Widget_Properties::ShowMaterial(Material* material) const
{
    if (!material)
        return;

    if (ComponentProperty::Begin("Material", Icon_Component_Material, nullptr, false))
    {
        const float offset_from_pos_x = 160;

        //= REFLECT =================================================
        auto tiling    = material->GetTiling();
        auto offset    = material->GetOffset();
        m_colorPicker_material->SetColor(material->GetColorAlbedo());
        //===========================================================

        // Name
        ImGui::Text("Name");
        ImGui::SameLine(offset_from_pos_x); ImGui::Text(material->GetResourceName().c_str());

        if (material->IsEditable())
        {
            // Texture slots
            {
                const auto show_property = [this, &offset_from_pos_x, &material](const char* name, const char* tooltip, const Material_Property type, bool show_texture, bool show_modifier)
                {
                    // Name
                    if (name)
                    {
                        ImGui::Text(name);
                        
                        if (tooltip)
                        {
                            ImGuiEx::Tooltip(tooltip);
                        }

                        if (show_texture || show_modifier)
                        {
                            ImGui::SameLine(offset_from_pos_x);
                        }
                    }

                    // Texture
                    if (show_texture)
                    {
                        ImGuiEx::ImageSlot(material->GetTexture_PtrShared(type), [&material, &type](const shared_ptr<RHI_Texture>& texture) { material->SetTextureSlot(type, texture); });

                        if (show_modifier)
                        {
                            ImGui::SameLine();
                        }
                    }

                    // Modifier
                    if (show_modifier)
                    {
                        if (type == Material_Color)
                        {
                            m_colorPicker_material->Update();
                        }
                        else
                        {
                            ImGui::PushID(static_cast<int>(ImGui::GetCursorPosX() + ImGui::GetCursorPosY()));
                            ImGuiEx::DragFloatWrap("", &material->GetProperty(type), 0.004f, 0.0f, 1.0f);
                            ImGui::PopID();
                        }
                    }
                };

                show_property("Clearcoat",              "Extra white specular layer on top of others",                                          Material_Clearcoat,             false, true);
                show_property("Clearcoat roughness",    "Roughness of clearcoat specular",                                                      Material_Clearcoat_Roughness,   false, true);
                show_property("Anisotropic",            "Amount of anisotropy for specular reflection",                                         Material_Anisotropic,           false, true);
                show_property("Anisotropic rotation",   "Rotates the direction of anisotropy, with 1.0 going full circle",                      Material_Anisotropic_Rotation,  false, true);
                show_property("Sheen",                  "Amount of soft velvet like reflection near edges",                                     Material_Sheen,                 false, true);
                show_property("Sheen tint",             "Mix between white and using base color for sheen reflection",                          Material_Sheen_Tint,            false, true);
                show_property("Color",                  "Diffuse or metal surface color",                                                       Material_Color,                 true, true);
                show_property("Roughness",              "Specifies microfacet roughness of the surface for diffuse and specular reflection",    Material_Roughness,             true, true);
                show_property("Metallic",               "Blends between a non-metallic and metallic material model",                            Material_Metallic,              true, true);
                show_property("Normal",                 "Controls the normals of the base layers",                                              Material_Normal,                true, true);
                show_property("Height",                 "Perceived depth for parallax mapping",                                                 Material_Height,                true, true);
                show_property("Occlusion",              "Amount of light loss, can be complementary to SSAO",                                   Material_Occlusion,             true, false);
                show_property("Emission",               "Light emission from the surface, works nice with bloom",                               Material_Emission,              true, false);
                show_property("Mask",                   "Discards pixels",                                                                      Material_Mask,                  true, false);
            }

            // UV
            {
                const float input_width = 128.0f;

                // Tiling
                ImGui::Text("Tiling");
                ImGui::SameLine(offset_from_pos_x); ImGui::Text("X");
                ImGui::PushItemWidth(input_width);
                ImGui::SameLine(); ImGui::InputFloat("##matTilingX", &tiling.x, 0.01f, 0.1f, "%.2f", ImGuiInputTextFlags_CharsDecimal);
                ImGui::SameLine(); ImGui::Text("Y");
                ImGui::SameLine(); ImGui::InputFloat("##matTilingY", &tiling.y, 0.01f, 0.1f, "%.2f", ImGuiInputTextFlags_CharsDecimal);
                ImGui::PopItemWidth();

                // Offset
                ImGui::Text("Offset");
                ImGui::SameLine(offset_from_pos_x); ImGui::Text("X");
                ImGui::PushItemWidth(input_width);
                ImGui::SameLine(); ImGui::InputFloat("##matOffsetX", &offset.x, 0.01f, 0.1f, "%.2f", ImGuiInputTextFlags_CharsDecimal);
                ImGui::SameLine(); ImGui::Text("Y");
                ImGui::SameLine(); ImGui::InputFloat("##matOffsetY", &offset.y, 0.01f, 0.1f, "%.2f", ImGuiInputTextFlags_CharsDecimal);
                ImGui::PopItemWidth();
            }
        }

        //= MAP =============================================================================================================================
        if (tiling != material->GetTiling())                                    material->SetTiling(tiling);
        if (offset != material->GetOffset())                                    material->SetOffset(offset);
        if (m_colorPicker_material->GetColor() != material->GetColorAlbedo())   material->SetColorAlbedo(m_colorPicker_material->GetColor());
        //===================================================================================================================================
    }
    ComponentProperty::End();
}

void Widget_Properties::ShowCamera(Camera* camera) const
{
    if (!camera)
        return;

    if (ComponentProperty::Begin("Camera", Icon_Component_Camera, camera))
    {
        //= REFLECT ==============================================================================================
        vector<const char*> projection_types    = { "Perspective", "Orthographic" };
        auto projection_char_ptr                = projection_types[static_cast<int>(camera->GetProjectionType())];
        float aperture                          = camera->GetAperture();
        float shutter_speed                     = camera->GetShutterSpeed();
        float iso                               = camera->GetIso();
        float fov                               = camera->GetFovHorizontalDeg();
        float near_plane                        = camera->GetNearPlane();
        float far_plane                         = camera->GetFarPlane();
        bool fps_control                        = camera->GetFpsControl();
        m_colorPicker_camera->SetColor(camera->GetClearColor());
        //========================================================================================================

        const auto input_text_flags = ImGuiInputTextFlags_CharsDecimal;

        // Background
        ImGui::Text("Background");
        ImGui::SameLine(ComponentProperty::g_column); m_colorPicker_camera->Update();

        // Projection
        ImGui::Text("Projection");
        ImGui::SameLine(ComponentProperty::g_column);
        ImGui::PushItemWidth(115.0f);
        if (ImGui::BeginCombo("##cameraProjection", projection_char_ptr))
        {
            for (auto i = 0; i < projection_types.size(); i++)
            {
                const auto is_selected = (projection_char_ptr == projection_types[i]);
                if (ImGui::Selectable(projection_types[i], is_selected))
                {
                    projection_char_ptr = projection_types[i];
                    camera->SetProjection(static_cast<ProjectionType>(i));
                }
                if (is_selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopItemWidth();

        // Aperture
        ImGui::SetCursorPosX(ComponentProperty::g_column);
        ImGuiEx::DragFloatWrap("Aperture (mm)", &aperture, 0.01f, 0.01f, 150.0f);
        ImGuiEx::Tooltip("Size of the lens diaphragm. Controls depth of field and chromatic aberration.");

        // Shutter speed
        ImGui::SetCursorPosX(ComponentProperty::g_column);
        ImGuiEx::DragFloatWrap("Shutter Speed (sec)", &shutter_speed, 0.0001f, 0.0f, 1.0f, "%.4f");
        ImGuiEx::Tooltip("Length of time for which the camera shutter is open. Controls the amount of motion blur.");

        // ISO
        ImGui::SetCursorPosX(ComponentProperty::g_column);
        ImGuiEx::DragFloatWrap("ISO", &iso, 0.1f, 0.0f, 2000.0f);
        ImGuiEx::Tooltip("Sensitivity to light. Controls camera noise.");

        // Field of View
        ImGui::SetCursorPosX(ComponentProperty::g_column);
        ImGuiEx::DragFloatWrap("Field of View", &fov, 0.1f, 1.0f, 179.0f);

        // Clipping Planes
        ImGui::Text("Clipping Planes");
        ImGui::SameLine(ComponentProperty::g_column);       ImGui::PushItemWidth(130); ImGui::InputFloat("Near", &near_plane, 0.1f, 0.1f, "%.3f", input_text_flags); ImGui::PopItemWidth();
        ImGui::SetCursorPosX(ComponentProperty::g_column);  ImGui::PushItemWidth(130); ImGui::InputFloat("Far", &far_plane, 0.1f, 0.1f, "%.3f", input_text_flags); ImGui::PopItemWidth();

        // FPS Control
        ImGui::Text("FPS Control");
        ImGui::SameLine(ComponentProperty::g_column); ImGui::Checkbox("##camera_fps_control", &fps_control);
        ImGuiEx::Tooltip("Enables FPS control while holding down the right mouse button");

        //= MAP ====================================================================================================================
        if (aperture != camera->GetAperture())                              camera->SetAperture(aperture);
        if (shutter_speed != camera->GetShutterSpeed())                     camera->SetShutterSpeed(shutter_speed);
        if (iso != camera->GetIso())                                        camera->SetIso(iso);
        if (fov != camera->GetFovHorizontalDeg())                           camera->SetFovHorizontalDeg(fov);
        if (near_plane != camera->GetNearPlane())                           camera->SetNearPlane(near_plane);
        if (far_plane != camera->GetFarPlane())                             camera->SetFarPlane(far_plane);
        if (fps_control != camera->GetFpsControl())                         camera->SetFpsControl(fps_control);
        if (m_colorPicker_camera->GetColor() != camera->GetClearColor())    camera->SetClearColor(m_colorPicker_camera->GetColor());
        //==========================================================================================================================
    }
    ComponentProperty::End();
}

void Widget_Properties::ShowEnvironment(Environment* environment) const
{
    if (!environment)
        return;

    if (ComponentProperty::Begin("Environment", Icon_Component_Environment, environment))
    {
        ImGui::Text("Sphere Map");

        ImGuiEx::ImageSlot(environment->GetTexture(), [&environment](const shared_ptr<RHI_Texture>& texture) { environment->SetTexture(texture); } );
    }
    ComponentProperty::End();
}

void Widget_Properties::ShowTerrain(Terrain* terrain) const
{
    if (!terrain)
        return;

    if (ComponentProperty::Begin("Terrain", Icon_Component_Terrain, terrain))
    {
        //= REFLECT =====================================
        float min_y             = terrain->GetMinY();
        float max_y             = terrain->GetMaxY();
        const float progress    = terrain->GetProgress();
        //===============================================

        const float cursor_y = ImGui::GetCursorPosY();

        ImGui::BeginGroup();
        {
            ImGui::Text("Height Map");

            ImGuiEx::ImageSlot(terrain->GetHeightMap(), [&terrain](const shared_ptr<RHI_Texture>& texture) { terrain->SetHeightMap(static_pointer_cast<RHI_Texture2D>(texture)); });

            if (ImGui::Button("Generate", ImVec2(82, 0)))
            {
                terrain->GenerateAsync();
            }
        }
        ImGui::EndGroup();

        ImGui::SameLine();
        ImGui::SetCursorPosY(cursor_y);
        ImGui::BeginGroup();
        {
            ImGui::InputFloat("Min Y", &min_y);
            ImGui::InputFloat("Max Y", &max_y);

            if (progress > 0.0f && progress < 1.0f)
            {
                ImGui::ProgressBar(progress, ImVec2(0.0f, 0.0f));
                ImGui::SameLine();
                ImGui::Text(terrain->GetProgressDescription().c_str());
            }
        }
        ImGui::EndGroup();

        //= MAP =================================================
        if (min_y != terrain->GetMinY()) terrain->SetMinY(min_y);
        if (max_y != terrain->GetMaxY()) terrain->SetMaxY(max_y);
        //=======================================================
    }
    ComponentProperty::End();
}

void Widget_Properties::ShowAudioSource(AudioSource* audio_source) const
{
    if (!audio_source)
        return;

    if (ComponentProperty::Begin("Audio Source", Icon_Component_AudioSource, audio_source))
    {
        //= REFLECT ===============================================
        string audio_clip_name  = audio_source->GetAudioClipName();
        bool mute               = audio_source->GetMute();
        bool play_on_start      = audio_source->GetPlayOnStart();
        bool loop               = audio_source->GetLoop();
        int priority            = audio_source->GetPriority();
        float volume            = audio_source->GetVolume();
        float pitch             = audio_source->GetPitch();
        float pan               = audio_source->GetPan();
        //=========================================================

        // Audio clip
        ImGui::Text("Audio Clip");
        ImGui::SameLine(ComponentProperty::g_column); ImGui::PushItemWidth(250.0f);
        ImGui::InputText("##audioSourceAudioClip", &audio_clip_name, ImGuiInputTextFlags_ReadOnly);
        ImGui::PopItemWidth();
        if (auto payload = ImGuiEx::ReceiveDragPayload(ImGuiEx::DragPayload_Audio))
        {
            audio_source->SetAudioClip(std::get<const char*>(payload->data));
        }

        // Mute
        ImGui::Text("Mute");
        ImGui::SameLine(ComponentProperty::g_column); ImGui::Checkbox("##audioSourceMute", &mute);

        // Play on start
        ImGui::Text("Play on Start");
        ImGui::SameLine(ComponentProperty::g_column); ImGui::Checkbox("##audioSourcePlayOnStart", &play_on_start);

        // Loop
        ImGui::Text("Loop");
        ImGui::SameLine(ComponentProperty::g_column); ImGui::Checkbox("##audioSourceLoop", &loop);

        // Priority
        ImGui::Text("Priority");
        ImGui::SameLine(ComponentProperty::g_column); ImGui::SliderInt("##audioSourcePriority", &priority, 0, 255);

        // Volume
        ImGui::Text("Volume");
        ImGui::SameLine(ComponentProperty::g_column); ImGui::SliderFloat("##audioSourceVolume", &volume, 0.0f, 1.0f);

        // Pitch
        ImGui::Text("Pitch");
        ImGui::SameLine(ComponentProperty::g_column); ImGui::SliderFloat("##audioSourcePitch", &pitch, 0.0f, 3.0f);

        // Pan
        ImGui::Text("Pan");
        ImGui::SameLine(ComponentProperty::g_column); ImGui::SliderFloat("##audioSourcePan", &pan, -1.0f, 1.0f);

        //= MAP ============================================================================================
        if (mute != audio_source->GetMute())                    audio_source->SetMute(mute);
        if (play_on_start != audio_source->GetPlayOnStart())    audio_source->SetPlayOnStart(play_on_start);
        if (loop != audio_source->GetLoop())                    audio_source->SetLoop(loop);
        if (priority != audio_source->GetPriority())            audio_source->SetPriority(priority);
        if (volume != audio_source->GetVolume())                audio_source->SetVolume(volume);
        if (pitch != audio_source->GetPitch())                    audio_source->SetPitch(pitch);
        if (pan != audio_source->GetPan())                        audio_source->SetPan(pan);
        //==================================================================================================
    }
    ComponentProperty::End();
}

void Widget_Properties::ShowAudioListener(AudioListener* audio_listener) const
{
    if (!audio_listener)
        return;

    if (ComponentProperty::Begin("Audio Listener", Icon_Component_AudioListener, audio_listener))
    {

    }
    ComponentProperty::End();
}

void Widget_Properties::ShowScript(Script* script) const
{
    if (!script)
        return;

    if (ComponentProperty::Begin(script->GetName(), Icon_Component_Script, script))
    {
        //= REFLECT =========================
        auto script_name = script->GetName();
        //===================================

        ImGui::Text("Script");
        ImGui::SameLine();
        ImGui::PushID("##ScriptNameTemp");
        ImGui::PushItemWidth(200.0f);
        ImGui::InputText("", &script_name, ImGuiInputTextFlags_ReadOnly);
        ImGui::PopItemWidth();
        ImGui::PopID();
    }
    ComponentProperty::End();
}

void Widget_Properties::ShowAddComponentButton() const
{
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5);
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() * 0.5f - 50);
    if (ImGui::Button("Add Component"))
    {
        ImGui::OpenPopup("##ComponentContextMenu_Add");
    }
    ComponentContextMenu_Add();
}

void Widget_Properties::ComponentContextMenu_Add() const
{
    if (ImGui::BeginPopup("##ComponentContextMenu_Add"))
    {
        if (auto entity = m_inspected_entity.lock())
        {
            // CAMERA
            if (ImGui::MenuItem("Camera"))
            {
                entity->AddComponent<Camera>();
            }

            // LIGHT
            if (ImGui::BeginMenu("Light"))
            {
                if (ImGui::MenuItem("Directional"))
                {
                    entity->AddComponent<Light>()->SetLightType(LightType::Directional);
                }
                else if (ImGui::MenuItem("Point"))
                {
                    entity->AddComponent<Light>()->SetLightType(LightType::Point);
                }
                else if (ImGui::MenuItem("Spot"))
                {
                    entity->AddComponent<Light>()->SetLightType(LightType::Spot);
                }

                ImGui::EndMenu();
            }

            // PHYSICS
            if (ImGui::BeginMenu("Physics"))
            {
                if (ImGui::MenuItem("Rigid Body"))
                {
                    entity->AddComponent<RigidBody>();
                }
                else if (ImGui::MenuItem("Soft Body"))
                {
                    entity->AddComponent<SoftBody>();
                }
                else if (ImGui::MenuItem("Collider"))
                {
                    entity->AddComponent<Collider>();
                }
                else if (ImGui::MenuItem("Constraint"))
                {
                    entity->AddComponent<Constraint>();
                }

                ImGui::EndMenu();
            }

            // AUDIO
            if (ImGui::BeginMenu("Audio"))
            {
                if (ImGui::MenuItem("Audio Source"))
                {
                    entity->AddComponent<AudioSource>();
                }
                else if (ImGui::MenuItem("Audio Listener"))
                {
                    entity->AddComponent<AudioListener>();
                }

                ImGui::EndMenu();
            }

            // ENVIRONMENT
            if (ImGui::BeginMenu("Environment"))
            {
                if (ImGui::MenuItem("Environment"))
                {
                    entity->AddComponent<Environment>()->LoadDefault();
                }

                ImGui::EndMenu();
            }

            // TERRAIN
            if (ImGui::MenuItem("Terrain"))
            {
                entity->AddComponent<Terrain>();
            }
        }

        ImGui::EndPopup();
    }
}

void Widget_Properties::Drop_AutoAddComponents() const
{
    if (auto payload = ImGuiEx::ReceiveDragPayload(ImGuiEx::DragPayload_Script))
    {
        if (auto script_component = m_inspected_entity.lock()->AddComponent<Script>())
        {
            script_component->SetScript(get<const char*>(payload->data));
        }
    }
}
