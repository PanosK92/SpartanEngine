/*
Copyright(c) 2016-2019 Panos Karabelas

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

//= INCLUDES ==================================
#include "Widget_Properties.h"
#include "Widget_World.h"
#include "../DragDrop.h"
#include "../ButtonColorPicker.h"
#include "../../ImGui/Source/imgui_stdlib.h"
#include "Rendering/Deferred/ShaderVariation.h"
#include "World/Entity.h"
#include "World/Components/Transform.h"
#include "World/Components/Renderable.h"
#include "World/Components/RigidBody.h"
#include "World/Components/Collider.h"
#include "World/Components/Constraint.h"
#include "World/Components/Light.h"
#include "World/Components/AudioSource.h"
#include "World/Components/AudioListener.h"
#include "World/Components/Camera.h"
#include "World/Components/Script.h"
#include "World/Components/Skybox.h"
#include "Audio/AudioClip.h"
#include "Core/Engine.h"
//=============================================

//= NAMESPACES ==========
using namespace std;
using namespace Spartan;
using namespace Math;
using namespace Helper;
//=======================

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
	static float g_column = 140.0f;
	static const float g_max_width = 100.0f;
	static shared_ptr<IComponent> g_copied;

	inline void ComponentContextMenu_Options(const string& id, const shared_ptr<IComponent>& component, const bool removable)
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
							entity->RemoveComponentById(component->GetID());
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

	inline bool Begin(const string& name, const Icon_Type icon_enum, const shared_ptr<IComponent>& component_instance, bool options = true, const bool removable = true)
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
			ImGui::SameLine(ImGui::GetWindowContentRegionWidth() * 0.973f); ImGui::SetCursorPosY(original_pen_y + 1.5f);
			if (ImGuiEx::ImageButton(name.c_str(), Icon_Component_Options, 12))
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

Widget_Properties::Widget_Properties(Context* context) : Widget(context)
{
	m_title					= "Properties";
	m_colorPicker_light		= make_unique<ButtonColorPicker>("Light Color Picker");
	m_colorPicker_material	= make_unique<ButtonColorPicker>("Material Color Picker");
	m_colorPicker_camera	= make_unique<ButtonColorPicker>("Camera Color Picker");

	_Widget_Properties::resource_cache	= m_context->GetSubsystem<ResourceCache>().get();
	_Widget_Properties::scene			= m_context->GetSubsystem<World>().get();
	m_xMin								= 500; // min width
}

void Widget_Properties::Tick(float delta_time)
{
	ImGui::PushItemWidth(ComponentProperty::g_max_width);

	if (!m_inspected_entity.expired())
	{
		auto entity_ptr = m_inspected_entity.lock().get();

		auto transform		= entity_ptr->GetComponent<Transform>();
		auto light			= entity_ptr->GetComponent<Light>();
		auto camera			= entity_ptr->GetComponent<Camera>();
		auto audio_source	= entity_ptr->GetComponent<AudioSource>();
		auto audio_listener	= entity_ptr->GetComponent<AudioListener>();
		auto renderable		= entity_ptr->GetComponent<Renderable>();
		auto material		= renderable ? renderable->MaterialPtr() : nullptr;
		auto rigid_body		= entity_ptr->GetComponent<RigidBody>();
		auto collider		= entity_ptr->GetComponent<Collider>();
		auto constraint		= entity_ptr->GetComponent<Constraint>();
		auto scripts		= entity_ptr->GetComponents<Script>();

		ShowTransform(transform);
		ShowLight(light);
		ShowCamera(camera);
		ShowAudioSource(audio_source);
		ShowAudioListener(audio_listener);
		ShowRenderable(renderable);
		ShowMaterial(material);
		ShowRigidBody(rigid_body);
		ShowCollider(collider);
		ShowConstraint(constraint);
		for (auto& script : scripts)
		{
			ShowScript(script);
		}

		ShowAddComponentButton();
		Drop_AutoAddComponents();
	}
	else if (!m_inspected_material.expired())
	{
		ShowMaterial(m_inspected_material.lock());
	}

	ImGui::PopItemWidth();
}

void Widget_Properties::Inspect(const weak_ptr<Entity>& entity)
{
	m_inspected_entity = entity;

	if (const auto shared_ptr = entity.lock())
	{
		_Widget_Properties::rotation_hint = shared_ptr->GetTransform_PtrRaw()->GetRotationLocal().ToEulerAngles();
	}
	else
	{
		_Widget_Properties::rotation_hint = Vector3::Zero;
	}

	// If we were previously inspecting a material, save the changes
	if (!m_inspected_material.expired())
	{
		m_inspected_material.lock()->SaveToFile(m_inspected_material.lock()->GetResourceFilePath());
	}
	m_inspected_material.reset();
}

void Widget_Properties::Inspect(const weak_ptr<Material>& material)
{
	m_inspected_entity.reset();
	m_inspected_material = material;
}

void Widget_Properties::ShowTransform(shared_ptr<Transform>& transform) const
{
	if (ComponentProperty::Begin("Transform", Icon_Component_Transform, transform, true, false))
	{
		const auto is_playing = Engine::EngineMode_IsSet(Engine_Game);

		//= REFLECT ======================================================================================================
		auto position	= transform->GetPositionLocal();
		auto rotation	= !is_playing ? _Widget_Properties::rotation_hint : transform->GetRotationLocal().ToEulerAngles();
		auto scale		= transform->GetScaleLocal();
		//================================================================================================================

		const auto start_column = ComponentProperty::g_column - 70.0f;

		const auto show_float = [](const char* id, const char* label, float* value) 
		{
			const auto step				= 1.0f;
			const auto step_fast		= 1.0f;
			char* decimals				= "%.4f";
			const auto input_text_flags = ImGuiInputTextFlags_CharsDecimal;

			ImGui::PushItemWidth(125.0f);
			ImGui::PushID(id);
			ImGui::InputFloat(label, value, step, step_fast, decimals, input_text_flags);
			ImGui::PopID();
			ImGui::PopItemWidth();
		};

		// Position
		ImGui::Text("Position");
		ImGui::SameLine(start_column);	show_float("TraPosX", "X", &position.x);
		ImGui::SameLine();				show_float("TraPosY", "Y", &position.y);
		ImGui::SameLine();				show_float("TraPosZ", "Z", &position.z);

		// Rotation
		ImGui::Text("Rotation");
		ImGui::SameLine(start_column);	show_float("TraRotX", "X", &rotation.x);
		ImGui::SameLine();				show_float("TraRotY", "Y", &rotation.y);
		ImGui::SameLine();				show_float("TraRotZ", "Z", &rotation.z);

		// Scale
		ImGui::Text("Scale");
		ImGui::SameLine(start_column);	show_float("TraScaX", "X", &scale.x);
		ImGui::SameLine();				show_float("TraScaY", "Y", &scale.y);
		ImGui::SameLine();				show_float("TraScaZ", "Z", &scale.z);

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

void Widget_Properties::ShowLight(shared_ptr<Light>& light) const
{
	if (!light)
		return;

	if (ComponentProperty::Begin("Light", Icon_Component_Light, light))
	{
		//= REFLECT =================================================================
		static vector<char*> types	= { "Directional", "Point", "Spot" };
		const char* type_char_ptr	= types[static_cast<int>(light->GetLightType())];
		auto intensity				= light->GetIntensity();
		auto angle					= light->GetAngle() * 179.0f;
		auto casts_shadows			= light->GetCastShadows();
		auto bias					= light->GetBias();
		auto normal_bias			= light->GetNormalBias();
		auto range					= light->GetRange();
		m_colorPicker_light->SetColor(light->GetColor());
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

		// Color
		ImGui::Text("Color");
		ImGui::SameLine(ComponentProperty::g_column); m_colorPicker_light->Update();

		// Intensity
		ImGui::Text("Intensity");
		ImGui::SameLine(ComponentProperty::g_column);
		ImGui::PushItemWidth(300); ImGui::DragFloat("##lightIntensity", &intensity, 0.01f, 0.0f, 100.0f); ImGui::PopItemWidth();

		// Cast shadows
		ImGui::Text("Shadows");
		ImGui::SameLine(ComponentProperty::g_column); ImGui::Checkbox("##lightShadows", &casts_shadows);

		// Cascade splits
		if (light->GetLightType() == LightType_Directional)
		{
			// Bias
			ImGui::Text("Bias");
			ImGui::SameLine(ComponentProperty::g_column);
			ImGui::PushItemWidth(300); ImGui::InputFloat("##lightBias", &bias, 0.0001f, 0.0001f, "%.4f"); ImGui::PopItemWidth();

			// Normal Bias
			ImGui::Text("Normal Bias");
			ImGui::SameLine(ComponentProperty::g_column);
			ImGui::PushItemWidth(300); ImGui::InputFloat("##lightNormalBias", &normal_bias, 1.0f, 1.0f, "%.0f"); ImGui::PopItemWidth();
		}

		// Range
		if (light->GetLightType() != LightType_Directional)
		{
			ImGui::Text("Range");
			ImGui::SameLine(ComponentProperty::g_column);
			ImGui::PushItemWidth(300); ImGui::DragFloat("##lightRange", &range, 0.01f, 0.0f, 100.0f); ImGui::PopItemWidth();
		}

		// Angle
		if (light->GetLightType() == LightType_Spot)
		{
			ImGui::Text("Angle");
			ImGui::SameLine(ComponentProperty::g_column);
			ImGui::PushItemWidth(300); ImGui::DragFloat("##lightAngle", &angle, 0.01f, 1.0f, 179.0f); ImGui::PopItemWidth();
		}

		//= MAP =====================================================================================================
		if (intensity != light->GetIntensity())						light->SetIntensity(intensity);
		if (casts_shadows != light->GetCastShadows())				light->SetCastShadows(casts_shadows);
		if (bias != light->GetBias())								light->SetBias(bias);
		if (normal_bias != light->GetNormalBias())					light->SetNormalBias(normal_bias);
		if (angle / 179.0f != light->GetAngle())					light->SetAngle(angle / 179.0f);
		if (range != light->GetRange())								light->SetRange(range);
		if (m_colorPicker_light->GetColor() != light->GetColor())	light->SetColor(m_colorPicker_light->GetColor());
		//===========================================================================================================
	}
	ComponentProperty::End();
}

void Widget_Properties::ShowRenderable(shared_ptr<Renderable>& renderable) const
{
	if (!renderable)
		return;

	if (ComponentProperty::Begin("Renderable", Icon_Component_Renderable, renderable))
	{
		//= REFLECT ====================================================================
		auto mesh_name			= renderable->GeometryName();
		auto material			= renderable->MaterialPtr();
		auto material_name		= material ? material->GetResourceName() : NOT_ASSIGNED;
		auto cast_shadows		= renderable->GetCastShadows();
		auto receive_shadows	=	 renderable->GetReceiveShadows();
		//==============================================================================

		ImGui::Text("Mesh");
		ImGui::SameLine(ComponentProperty::g_column); ImGui::Text(mesh_name.c_str());

		// Material
		ImGui::Text("Material");
		ImGui::SameLine(ComponentProperty::g_column); ImGui::Text(material_name.c_str());

		// Cast shadows
		ImGui::Text("Cast Shadows");
		ImGui::SameLine(ComponentProperty::g_column); ImGui::Checkbox("##RenderableCastShadows", &cast_shadows);

		// Receive shadows
		ImGui::Text("Receive Shadows");
		ImGui::SameLine(ComponentProperty::g_column); ImGui::Checkbox("##RenderableReceiveShadows", &receive_shadows);

		//= MAP ==============================================================================================
		if (cast_shadows != renderable->GetCastShadows())		renderable->SetCastShadows(cast_shadows);
		if (receive_shadows != renderable->GetReceiveShadows())	renderable->SetReceiveShadows(receive_shadows);
		//====================================================================================================
	}
	ComponentProperty::End();
}

void Widget_Properties::ShowRigidBody(shared_ptr<RigidBody>& rigid_body) const
{
	if (!rigid_body)
		return;

	if (ComponentProperty::Begin("RigidBody", Icon_Component_RigidBody, rigid_body))
	{
		//= REFLECT ================================================================
		auto mass				= rigid_body->GetMass();
		auto friction			= rigid_body->GetFriction();
		auto friction_rolling	= rigid_body->GetFrictionRolling();
		auto restitution		= rigid_body->GetRestitution();
		auto use_gravity		= rigid_body->GetUseGravity();
		auto is_kinematic		= rigid_body->GetIsKinematic();
		auto freeze_pos_x		= static_cast<bool>(rigid_body->GetPositionLock().x);
		auto freeze_pos_y		= static_cast<bool>(rigid_body->GetPositionLock().y);
		auto freeze_pos_z		= static_cast<bool>(rigid_body->GetPositionLock().z);
		auto freeze_rot_x		= static_cast<bool>(rigid_body->GetRotationLock().x);
		auto freeze_rot_y		= static_cast<bool>(rigid_body->GetRotationLock().y);
		auto freeze_rot_z		= static_cast<bool>(rigid_body->GetRotationLock().z);
		//==========================================================================

		const auto input_text_flags		= ImGuiInputTextFlags_CharsDecimal;
		const auto item_width			= 120.0f;
		const auto step					= 0.1f;
		const auto step_fast			= 0.1f;
		const auto precision			= "%.3f";

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

		//= MAP ====================================================================================================================================================================================================
		if (mass != rigid_body->GetMass())						rigid_body->SetMass(mass);
		if (friction != rigid_body->GetFriction())				rigid_body->SetFriction(friction);
		if (friction_rolling != rigid_body->GetFrictionRolling())	rigid_body->SetFrictionRolling(friction_rolling);
		if (restitution != rigid_body->GetRestitution())			rigid_body->SetRestitution(restitution);
		if (use_gravity != rigid_body->GetUseGravity())			rigid_body->SetUseGravity(use_gravity);
		if (is_kinematic != rigid_body->GetIsKinematic())			rigid_body->SetIsKinematic(is_kinematic);
		if (freeze_pos_x != static_cast<bool>(rigid_body->GetPositionLock().x))	rigid_body->SetPositionLock(Vector3(static_cast<float>(freeze_pos_x), static_cast<float>(freeze_pos_y), static_cast<float>(freeze_pos_z)));
		if (freeze_pos_y != static_cast<bool>(rigid_body->GetPositionLock().y))	rigid_body->SetPositionLock(Vector3(static_cast<float>(freeze_pos_x), static_cast<float>(freeze_pos_y), static_cast<float>(freeze_pos_z)));
		if (freeze_pos_z != static_cast<bool>(rigid_body->GetPositionLock().z))	rigid_body->SetPositionLock(Vector3(static_cast<float>(freeze_pos_x), static_cast<float>(freeze_pos_y), static_cast<float>(freeze_pos_z)));
		if (freeze_rot_x != static_cast<bool>(rigid_body->GetRotationLock().x))	rigid_body->SetRotationLock(Vector3(static_cast<float>(freeze_rot_x), static_cast<float>(freeze_rot_y), static_cast<float>(freeze_rot_z)));
		if (freeze_rot_y != static_cast<bool>(rigid_body->GetRotationLock().y))	rigid_body->SetRotationLock(Vector3(static_cast<float>(freeze_rot_x), static_cast<float>(freeze_rot_y), static_cast<float>(freeze_rot_z)));
		if (freeze_rot_z != static_cast<bool>(rigid_body->GetRotationLock().z))	rigid_body->SetRotationLock(Vector3(static_cast<float>(freeze_rot_x), static_cast<float>(freeze_rot_y), static_cast<float>(freeze_rot_z)));
		//==========================================================================================================================================================================================================
	}
	ComponentProperty::End();
}

void Widget_Properties::ShowCollider(shared_ptr<Collider>& collider) const
{
	if (!collider)
		return;

	if (ComponentProperty::Begin("Collider", Icon_Component_Collider, collider))
	{
		//= REFLECT =======================================================================
		static vector<char*> type = {
			"Box",
			"Sphere",
			"Static Plane",
			"Cylinder",
			"Capsule",
			"Cone",
			"Mesh"
		};
		const char* shape_char_ptr		= type[static_cast<int>(collider->GetShapeType())];
		bool optimize					= collider->GetOptimize();
		Vector3 collider_center			= collider->GetCenter();
		Vector3 collider_bounding_box	= collider->GetBoundingBox();
		//=================================================================================

		const auto input_text_flags		= ImGuiInputTextFlags_CharsDecimal;
		const auto step					= 0.1f;
		const auto step_fast			= 0.1f;
		const auto precision			= "%.3f";

		// Type
		ImGui::Text("Type");
		ImGui::PushItemWidth(110);
		ImGui::SameLine(ComponentProperty::g_column); if (ImGui::BeginCombo("##colliderType", shape_char_ptr))
		{
			for (unsigned int i = 0; i < static_cast<unsigned int>(type.size()); i++)
			{
				const auto is_selected = (shape_char_ptr == type[i]);
				if (ImGui::Selectable(type[i], is_selected))
				{
					shape_char_ptr = type[i];
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
		ImGui::SameLine(ComponentProperty::g_column);	ImGui::PushID("colCenterX"); ImGui::InputFloat("X", &collider_center.x, step, step_fast, precision, input_text_flags); ImGui::PopID();
		ImGui::SameLine();								ImGui::PushID("colCenterY"); ImGui::InputFloat("Y", &collider_center.y, step, step_fast, precision, input_text_flags); ImGui::PopID();
		ImGui::SameLine();								ImGui::PushID("colCenterZ"); ImGui::InputFloat("Z", &collider_center.z, step, step_fast, precision, input_text_flags); ImGui::PopID();

		// Size
		ImGui::Text("Size");
		ImGui::SameLine(ComponentProperty::g_column);	ImGui::PushID("colSizeX"); ImGui::InputFloat("X", &collider_bounding_box.x, step, step_fast, precision, input_text_flags); ImGui::PopID();
		ImGui::SameLine();								ImGui::PushID("colSizeY"); ImGui::InputFloat("Y", &collider_bounding_box.y, step, step_fast, precision, input_text_flags); ImGui::PopID();
		ImGui::SameLine();								ImGui::PushID("colSizeZ"); ImGui::InputFloat("Z", &collider_bounding_box.z, step, step_fast, precision, input_text_flags); ImGui::PopID();

		// Optimize
		if (collider->GetShapeType() == ColliderShape_Mesh)
		{
			ImGui::Text("Optimize");
			ImGui::SameLine(ComponentProperty::g_column); ImGui::Checkbox("##colliderOptimize", &optimize);
		}

		//= MAP ==================================================================================================
		if (collider_center != collider->GetCenter())				collider->SetCenter(collider_center);
		if (collider_bounding_box != collider->GetBoundingBox())		collider->SetBoundingBox(collider_bounding_box);
		if (optimize != collider->GetOptimize())					collider->SetOptimize(optimize);
		//========================================================================================================
	}
	ComponentProperty::End();
}

void Widget_Properties::ShowConstraint(shared_ptr<Constraint>& constraint) const
{
	if (!constraint)
		return;

	if (ComponentProperty::Begin("Constraint", Icon_Component_AudioSource, constraint))
	{
		//= REFLECT ============================================================================
		vector<char*> types		= {"Point", "Hinge", "Slider", "ConeTwist" };
		const char* type_str	= types[static_cast<int>(constraint->GetConstraintType())];
		auto other_body			= constraint->GetBodyOther();
		bool other_body_dirty	= false;
		Vector3 position		= constraint->GetPosition();
		Vector3 rotation		= constraint->GetRotation().ToEulerAngles();
		Vector2 high_limit		= constraint->GetHighLimit();
		Vector2 low_limit		= constraint->GetLowLimit();
		string other_body_name	= other_body.expired() ? "N/A" : other_body.lock()->GetName();
		//======================================================================================

		auto inputTextFlags		= ImGuiInputTextFlags_CharsDecimal;
		float step				= 0.1f;
		float step_fast			= 0.1f;
		const char* precision	= "%.3f";

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
		ImGui::InputText("", &other_body_name, ImGuiInputTextFlags_ReadOnly);
		if (auto payload = DragDrop::Get().GetPayload(DragPayload_entity))
		{
			const auto entity_id	= get<unsigned int>(payload->data);
			other_body				= _Widget_Properties::scene->EntityGetById(entity_id);
			other_body_dirty			= true;
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

		//= MAP ========================================================================================================================
		if (other_body_dirty)												{ constraint->SetBodyOther(other_body); other_body_dirty = false; }
		if (position != constraint->GetPosition())						constraint->SetPosition(position);
		if (rotation != constraint->GetRotation().ToEulerAngles())		constraint->SetRotation(Quaternion::FromEulerAngles(rotation));
		if (high_limit != constraint->GetHighLimit())					constraint->SetHighLimit(high_limit);
		if (low_limit != constraint->GetLowLimit())						constraint->SetLowLimit(low_limit);
		//==============================================================================================================================
	}
	ComponentProperty::End();
}

void Widget_Properties::ShowMaterial(shared_ptr<Material>& material) const
{
	if (!material)
		return;

	if (ComponentProperty::Begin("Material", Icon_Component_Material, nullptr, false))
	{
		//= REFLECT ====================================================
		float roughness = material->GetRoughnessMultiplier();
		float metallic	= material->GetMetallicMultiplier();
		float normal	= material->GetNormalMultiplier();
		float height	= material->GetHeightMultiplier();
		Vector2 tiling	= material->GetTiling();
		Vector2 offset	= material->GetOffset();
		m_colorPicker_material->SetColor(material->GetColorAlbedo());
		//==============================================================

		static const auto material_text_size = ImVec2(80, 80);

		const auto tex_albedo		= material->GetTextureSlotByType(TextureType_Albedo).ptr.get();
		const auto tex_roughness	= material->GetTextureSlotByType(TextureType_Roughness).ptr.get();
		const auto tex_metallic		= material->GetTextureSlotByType(TextureType_Metallic).ptr.get();
		const auto tex_normal		= material->GetTextureSlotByType(TextureType_Normal).ptr.get();
		const auto tex_height		= material->GetTextureSlotByType(TextureType_Height).ptr.get();
		const auto tex_occlusion	= material->GetTextureSlotByType(TextureType_Occlusion).ptr.get();
		const auto tex_emission		= material->GetTextureSlotByType(TextureType_Emission).ptr.get();
		const auto tex_mask			= material->GetTextureSlotByType(TextureType_Mask).ptr.get();

		// Name
		ImGui::Text("Name");
		ImGui::SameLine(ComponentProperty::g_column); ImGui::Text(material->GetResourceName().c_str());

		if (material->IsEditable())
		{
			const auto display_texture_slot = [&material](RHI_Texture* texture, const char* texture_name, TextureType textureType)
			{
				// Texture
				ImGui::Text(texture_name);
				ImGui::SameLine(ComponentProperty::g_column); ImGui::Image(
					texture ? texture->GetShaderResource() : nullptr,
					material_text_size,
					ImVec2(0, 0),
					ImVec2(1, 1),
					ImColor(255, 255, 255, 255),
					ImColor(255, 255, 255, 128)
				);

				// Drop target
				if (auto payload = DragDrop::Get().GetPayload(DragPayload_Texture))
				{
					try
					{
						if (const auto tex = _Widget_Properties::resource_cache->Load<RHI_Texture>(get<const char*>(payload->data)))
						{
							material->SetTextureSlot(textureType, tex);
						}
					}
					catch (const std::bad_variant_access& e) { LOGF_ERROR("Widget_Properties::ShowMaterial: %s", e.what()); }
				}

				// Remove texture button
				if (material->HasTexture(textureType))
				{
					const auto size = 15.0f;
					ImGui::SameLine(); ImGui::SetCursorPosX(ImGui::GetCursorPosX() - size * 2.0f);
					if (ImGuiEx::ImageButton(texture_name, Icon_Component_Material_RemoveTexture, size))
					{
						material->SetTextureSlot(textureType, nullptr);
					}
				}
			};

			// Albedo
			display_texture_slot(tex_albedo, "Albedo", TextureType_Albedo);
			ImGui::SameLine(); m_colorPicker_material->Update();

			// Roughness
			display_texture_slot(tex_roughness, "Roughness", TextureType_Roughness);
			roughness = material->GetRoughnessMultiplier();
			ImGui::SameLine(); ImGui::DragFloat("##matRoughness", &roughness, 0.001f, 0.0f, 1.0f);

			// Metallic
			display_texture_slot(tex_metallic, "Metallic", TextureType_Metallic);
			metallic = material->GetMetallicMultiplier();
			ImGui::SameLine(); ImGui::DragFloat("##matMetallic", &metallic, 0.001f, 0.0f, 1.0f);

			// Normal
			display_texture_slot(tex_normal, "Normal", TextureType_Normal);
			normal = material->GetNormalMultiplier();
			ImGui::SameLine(); ImGui::DragFloat("##matNormal", &normal, 0.001f, 0.0f, 1.0f);

			// Height
			display_texture_slot(tex_height, "Height", TextureType_Height);
			height = material->GetHeightMultiplier();
			ImGui::SameLine(); ImGui::DragFloat("##matHeight", &height, 0.001f, 0.0f, 1.0f);

			// Occlusion
			display_texture_slot(tex_occlusion, "Occlusion", TextureType_Occlusion);

			// Emission
			display_texture_slot(tex_emission, "Emission", TextureType_Emission);

			// Mask
			display_texture_slot(tex_mask, "Mask", TextureType_Mask);

			// Tiling
			ImGui::Text("Tiling");
			ImGui::SameLine(ComponentProperty::g_column); ImGui::Text("X"); ImGui::SameLine(); ImGui::InputFloat("##matTilingX", &tiling.x, ImGuiInputTextFlags_CharsDecimal);
			ImGui::SameLine(); ImGui::Text("Y"); ImGui::SameLine(); ImGui::InputFloat("##matTilingY", &tiling.y, ImGuiInputTextFlags_CharsDecimal);

			// Offset
			ImGui::Text("Offset");
			ImGui::SameLine(ComponentProperty::g_column); ImGui::Text("X"); ImGui::SameLine(); ImGui::InputFloat("##matOffsetX", &offset.x, ImGuiInputTextFlags_CharsDecimal);
			ImGui::SameLine(); ImGui::Text("Y"); ImGui::SameLine(); ImGui::InputFloat("##matOffsetY", &offset.y, ImGuiInputTextFlags_CharsDecimal);
		}

		//= MAP =============================================================================================================================
		if (roughness != material->GetRoughnessMultiplier())					material->SetRoughnessMultiplier(roughness);
		if (metallic != material->GetMetallicMultiplier())						material->SetMetallicMultiplier(metallic);
		if (normal != material->GetNormalMultiplier())							material->SetNormalMultiplier(normal);
		if (height != material->GetHeightMultiplier())							material->SetHeightMultiplier(height);
		if (tiling != material->GetTiling())									material->SetTiling(tiling);
		if (offset != material->GetOffset())									material->SetOffset(offset);
		if (m_colorPicker_material->GetColor() != material->GetColorAlbedo())	material->SetColorAlbedo(m_colorPicker_material->GetColor());
		//===================================================================================================================================
	}
	ComponentProperty::End();
}

void Widget_Properties::ShowCamera(shared_ptr<Camera>& camera) const
{
	if (!camera)
		return;

	if (ComponentProperty::Begin("Camera", Icon_Component_Camera, camera))
	{
		//= REFLECT ==============================================================================================
		vector<const char*> projection_types	= { "Perspective", "Orthographic" };
		auto projection_char_ptr				= projection_types[static_cast<int>(camera->GetProjectionType())];
		float fov								= camera->GetFovHorizontalDeg();
		float near_plane						= camera->GetNearPlane();
		float far_plane							= camera->GetFarPlane();
		m_colorPicker_camera->SetColor(camera->GetClearColor());
		//========================================================================================================

		const auto input_text_flags = ImGuiInputTextFlags_CharsDecimal;

		// Background
		ImGui::Text("Background");
		ImGui::SameLine(ComponentProperty::g_column); m_colorPicker_camera->Update();

		// Projection
		ImGui::Text("Projection");
		ImGui::SameLine(ComponentProperty::g_column);
		ImGui::PushItemWidth(110.0f);
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

		// Field of View
		ImGui::SetCursorPosX(ComponentProperty::g_column);  ImGui::SliderFloat("Field of View", &fov, 1.0f, 179.0f);

		// Clipping Planes
		ImGui::Text("Clipping Planes");
		ImGui::SameLine(ComponentProperty::g_column);		ImGui::PushItemWidth(130); ImGui::InputFloat("Near", &near_plane, 0.1f, 0.1f, "%.3f", input_text_flags); ImGui::PopItemWidth();
		ImGui::SetCursorPosX(ComponentProperty::g_column);	ImGui::PushItemWidth(130); ImGui::InputFloat("Far", &far_plane, 0.1f, 0.1f, "%.3f", input_text_flags); ImGui::PopItemWidth();

		//= MAP ====================================================================================================================
		if (fov != camera->GetFovHorizontalDeg())							camera->SetFovHorizontalDeg(fov);
		if (near_plane != camera->GetNearPlane())							camera->SetNearPlane(near_plane);
		if (far_plane != camera->GetFarPlane())								camera->SetFarPlane(far_plane);
		if (m_colorPicker_camera->GetColor() != camera->GetClearColor())	camera->SetClearColor(m_colorPicker_camera->GetColor());
		//==========================================================================================================================
	}
	ComponentProperty::End();
}

void Widget_Properties::ShowAudioSource(shared_ptr<AudioSource>& audio_source) const
{
	if (!audio_source)
		return;

	if (ComponentProperty::Begin("Audio Source", Icon_Component_AudioSource, audio_source))
	{
		//= REFLECT ==============================================
		string audio_clip_name	= audio_source->GetAudioClipName();
		bool mute				= audio_source->GetMute();
		bool play_on_start		= audio_source->GetPlayOnStart();
		bool loop				= audio_source->GetLoop();
		int priority			= audio_source->GetPriority();
		float volume			= audio_source->GetVolume();
		float pitch				= audio_source->GetPitch();
		float pan				= audio_source->GetPan();
		//========================================================

		// Audio clip
		ImGui::Text("Audio Clip");
		ImGui::SameLine(ComponentProperty::g_column); ImGui::PushItemWidth(250.0f);
		ImGui::InputText("##audioSourceAudioClip", &audio_clip_name, ImGuiInputTextFlags_ReadOnly);
		ImGui::PopItemWidth();
		if (auto payload = DragDrop::Get().GetPayload(DragPayload_Audio))
		{
			audio_clip_name			= FileSystem::GetFileNameFromFilePath(get<const char*>(payload->data));
			const auto audio_clip	= _Widget_Properties::resource_cache->Load<AudioClip>(get<const char*>(payload->data));
			audio_source->SetAudioClip(audio_clip);
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
		if (mute != audio_source->GetMute())					audio_source->SetMute(mute);
		if (play_on_start != audio_source->GetPlayOnStart())	audio_source->SetPlayOnStart(play_on_start);
		if (loop != audio_source->GetLoop())					audio_source->SetLoop(loop);
		if (priority != audio_source->GetPriority())			audio_source->SetPriority(priority);
		if (volume != audio_source->GetVolume())				audio_source->SetVolume(volume);
		if (pitch != audio_source->GetPitch())					audio_source->SetPitch(pitch);
		if (pan != audio_source->GetPan())						audio_source->SetPan(pan);
		//==================================================================================================
	}
	ComponentProperty::End();
}

void Widget_Properties::ShowAudioListener(shared_ptr<AudioListener>& audio_listener) const
{
	if (!audio_listener)
		return;

	if (ComponentProperty::Begin("Audio Listener", Icon_Component_AudioListener, audio_listener))
	{

	}
	ComponentProperty::End();
}

void Widget_Properties::ShowScript(shared_ptr<Script>& script) const
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
					entity->AddComponent<Light>()->SetLightType(LightType_Directional);
				}
				else if (ImGui::MenuItem("Point"))
				{
					entity->AddComponent<Light>()->SetLightType(LightType_Point);
				}
				else if (ImGui::MenuItem("Spot"))
				{
					entity->AddComponent<Light>()->SetLightType(LightType_Spot);
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
				if (ImGui::MenuItem("Skybox"))
				{
					entity->AddComponent<Skybox>();
				}

				ImGui::EndMenu();
			}
		}

		ImGui::EndPopup();
	}
}

void Widget_Properties::Drop_AutoAddComponents() const
{
	if (auto payload = DragDrop::Get().GetPayload(DragPayload_Script))
	{
		if (auto script_component = m_inspected_entity.lock()->AddComponent<Script>())
		{
			script_component->SetScript(get<const char*>(payload->data));
		}
	}
}
