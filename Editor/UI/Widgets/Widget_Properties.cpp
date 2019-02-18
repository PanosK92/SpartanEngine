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
#include "Rendering/Deferred/ShaderVariation.h"
//=============================================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
using namespace Math;
using namespace Helper;
//=======================

weak_ptr<Entity> Widget_Properties::m_inspectedentity;
weak_ptr<Material> Widget_Properties::m_inspectedMaterial;

namespace _Widget_Properties
{
	static ResourceCache* resourceCache;
	static World* scene;
	static Vector3 rotationHint;
}

namespace ComponentProperty
{
	static string g_contexMenuID;
	static float g_column = 140.0f;
	static const float g_maxWidth = 100.0f;
	static shared_ptr<IComponent> g_copied;

	inline void ComponentContextMenu_Options(const string& id, shared_ptr<IComponent> component, bool removable)
	{
		if (ImGui::BeginPopup(id.c_str()))
		{
			if (removable)
			{
				if (ImGui::MenuItem("Remove"))
				{
					if (auto entity = Widget_Properties::m_inspectedentity.lock())
					{
						if (component)
						{
							entity->RemoveComponentByID(component->GetID());
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

	inline bool Begin(const string& name, Icon_Type icon_enum, shared_ptr<IComponent> componentInstance, bool options = true, bool removable = true)
	{
		// Collapsible contents
		bool collapsed = ImGui::CollapsingHeader(name.c_str(), ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_DefaultOpen);

		// Component Icon - Top left
		ImGui::SameLine();
		ImGui::Spacing();
		ImGui::SameLine();
		float originalPenY = ImGui::GetCursorPosY();
		ImGui::SetCursorPosY(originalPenY + 5.0f);
		THUMBNAIL_IMAGE_BY_ENUM(icon_enum, 15);

		// Component Options - Top right
		if (options)
		{
			ImGui::SameLine(ImGui::GetWindowContentRegionWidth() * 0.973f); ImGui::SetCursorPosY(originalPenY + 1.5f);
			if (THUMBNAIL_BUTTON_TYPE_UNIQUE_ID(name.c_str(), Icon_Component_Options, 12))
			{
				g_contexMenuID = name;
				ImGui::OpenPopup(g_contexMenuID.c_str());
			}

			if (g_contexMenuID == name)
			{
				ComponentContextMenu_Options(g_contexMenuID, componentInstance, removable);
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

	_Widget_Properties::resourceCache	= m_context->GetSubsystem<ResourceCache>().get();
	_Widget_Properties::scene			= m_context->GetSubsystem<World>().get();
	m_xMin								= 500; // min width
}

void Widget_Properties::Tick(float deltaTime)
{
	ImGui::PushItemWidth(ComponentProperty::g_maxWidth);

	if (!m_inspectedentity.expired())
	{
		auto entityPtr = m_inspectedentity.lock().get();

		auto transform		= entityPtr->GetComponent<Transform>();
		auto light			= entityPtr->GetComponent<Light>();
		auto camera			= entityPtr->GetComponent<Camera>();
		auto audioSource	= entityPtr->GetComponent<AudioSource>();
		auto audioListener	= entityPtr->GetComponent<AudioListener>();
		auto renderable		= entityPtr->GetComponent<Renderable>();
		auto material		= renderable ? renderable->Material_Ptr() : nullptr;
		auto rigidBody		= entityPtr->GetComponent<RigidBody>();
		auto collider		= entityPtr->GetComponent<Collider>();
		auto constraint		= entityPtr->GetComponent<Constraint>();
		auto scripts		= entityPtr->GetComponents<Script>();

		ShowTransform(transform);
		ShowLight(light);
		ShowCamera(camera);
		ShowAudioSource(audioSource);
		ShowAudioListener(audioListener);
		ShowRenderable(renderable);
		ShowMaterial(material);
		ShowRigidBody(rigidBody);
		ShowCollider(collider);
		ShowConstraint(constraint);
		for (auto& script : scripts)
		{
			ShowScript(script);
		}

		ShowAddComponentButton();
		Drop_AutoAddComponents();
	}
	else if (!m_inspectedMaterial.expired())
	{
		ShowMaterial(m_inspectedMaterial.lock());
	}

	ImGui::PopItemWidth();
}

void Widget_Properties::Inspect(weak_ptr<Entity> entity)
{
	m_inspectedentity = entity;

	if (auto sharedPtr = entity.lock())
	{
		_Widget_Properties::rotationHint = sharedPtr->GetTransform_PtrRaw()->GetRotationLocal().ToEulerAngles();
	}
	else
	{
		_Widget_Properties::rotationHint = Vector3::Zero;
	}

	// If we were previously inspecting a material, save the changes
	if (!m_inspectedMaterial.expired())
	{
		m_inspectedMaterial.lock()->SaveToFile(m_inspectedMaterial.lock()->GetResourceFilePath());
	}
	m_inspectedMaterial.reset();
}

void Widget_Properties::Inspect(weak_ptr<Material> material)
{
	m_inspectedentity.reset();
	m_inspectedMaterial = material;
}

void Widget_Properties::ShowTransform(shared_ptr<Transform>& transform)
{
	if (ComponentProperty::Begin("Transform", Icon_Component_Transform, transform, true, false))
	{
		bool isPlaying = Engine::EngineMode_IsSet(Engine_Game);

		//= REFLECT ========================================================================================================
		Vector3 position	= transform->GetPositionLocal();
		Vector3 rotation	= !isPlaying ? _Widget_Properties::rotationHint : transform->GetRotationLocal().ToEulerAngles();
		Vector3 scale		= transform->GetScaleLocal();
		//==================================================================================================================
		
		float startColumn = ComponentProperty::g_column - 70.0f;

		auto showFloat = [](const char* id, const char* label, float* value) 
		{
			float itemWidth		= 125.0f;
			float step			= 1.0f;
			float step_fast		= 1.0f;
			char* decimals		= "%.4f";
			auto inputTextFlags = ImGuiInputTextFlags_CharsDecimal;

			ImGui::PushItemWidth(125.0f);
			ImGui::PushID(id);
			ImGui::InputFloat(label, value, step, step_fast, decimals, inputTextFlags);
			ImGui::PopID();
			ImGui::PopItemWidth();
		};

		// Position
		ImGui::Text("Position");
		ImGui::SameLine(startColumn);	showFloat("TraPosX", "X", &position.x);
		ImGui::SameLine();				showFloat("TraPosY", "Y", &position.y);
		ImGui::SameLine();				showFloat("TraPosZ", "Z", &position.z);

		// Rotation
		ImGui::Text("Rotation");
		ImGui::SameLine(startColumn);	showFloat("TraRotX", "X", &rotation.x);
		ImGui::SameLine();				showFloat("TraRotY", "Y", &rotation.y);
		ImGui::SameLine();				showFloat("TraRotZ", "Z", &rotation.z);

		// Scale
		ImGui::Text("Scale");
		ImGui::SameLine(startColumn);	showFloat("TraScaX", "X", &scale.x);
		ImGui::SameLine();				showFloat("TraScaY", "Y", &scale.y);
		ImGui::SameLine();				showFloat("TraScaZ", "Z", &scale.z);

		//= MAP ===================================================================
		if (!isPlaying)
		{
			transform->SetPositionLocal(position);
			transform->SetScaleLocal(scale);

			if (rotation != _Widget_Properties::rotationHint)
			{
				transform->SetRotationLocal(Quaternion::FromEulerAngles(rotation));
				_Widget_Properties::rotationHint = rotation;
			}
		}
		//=========================================================================
	}
	ComponentProperty::End();
}

void Widget_Properties::ShowLight(shared_ptr<Light>& light)
{
	if (!light)
		return;

	if (ComponentProperty::Begin("Light", Icon_Component_Light, light))
	{
		//= REFLECT =====================================================
		static vector<char*> types	= { "Directional", "Point", "Spot" };
		const char* typeCharPtr		= types[(int)light->GetLightType()];
		float intensity				= light->GetIntensity();
		float angle					= light->GetAngle() * 179.0f;
		bool castsShadows			= light->GetCastShadows();
		float bias					= light->GetBias();
		float normalBias			= light->GetNormalBias();
		float range					= light->GetRange();
		m_colorPicker_light->SetColor(light->GetColor());
		//===============================================================

		// Type
		ImGui::Text("Type");
		ImGui::PushItemWidth(110.0f);
		ImGui::SameLine(ComponentProperty::g_column); if (ImGui::BeginCombo("##LightType", typeCharPtr))
		{
			for (unsigned int i = 0; i < (unsigned int)types.size(); i++)
			{
				bool is_selected = (typeCharPtr == types[i]);
				if (ImGui::Selectable(types[i], is_selected))
				{
					typeCharPtr = types[i];
					light->SetLightType((LightType)i);
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
		ImGui::PushItemWidth(300); ImGui::SliderFloat("##lightIntensity", &intensity, 0.0f, 100.0f); ImGui::PopItemWidth();

		// Cast shadows
		ImGui::Text("Shadows");
		ImGui::SameLine(ComponentProperty::g_column); ImGui::Checkbox("##lightShadows", &castsShadows);

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
			ImGui::PushItemWidth(300); ImGui::InputFloat("##lightNormalBias", &normalBias, 1.0f, 1.0f, "%.0f"); ImGui::PopItemWidth();
		}

		// Range
		if (light->GetLightType() != LightType_Directional)
		{
			ImGui::Text("Range");
			ImGui::SameLine(ComponentProperty::g_column);
			ImGui::PushItemWidth(300); ImGui::SliderFloat("##lightRange", &range, 0.0f, 100.0f); ImGui::PopItemWidth();
		}

		// Angle
		if (light->GetLightType() == LightType_Spot)
		{
			ImGui::Text("Angle");
			ImGui::SameLine(ComponentProperty::g_column);
			ImGui::PushItemWidth(300); ImGui::SliderFloat("##lightAngle", &angle, 1.0f, 179.0f); ImGui::PopItemWidth();
		}

		//= MAP =====================================================================================================
		if (intensity != light->GetIntensity())							light->SetIntensity(intensity);
		if (castsShadows != light->GetCastShadows())					light->SetCastShadows(castsShadows);
		if (bias != light->GetBias())									light->SetBias(bias);
		if (normalBias != light->GetNormalBias())						light->SetNormalBias(normalBias);
		if (angle / 179.0f != light->GetAngle())						light->SetAngle(angle / 179.0f);
		if (range != light->GetRange())									light->SetRange(range);
		if (m_colorPicker_light->GetColor() != light->GetColor())	light->SetColor(m_colorPicker_light->GetColor());
		//===========================================================================================================
	}
	ComponentProperty::End();
}

void Widget_Properties::ShowRenderable(shared_ptr<Renderable>& renderable)
{
	if (!renderable)
		return;

	if (ComponentProperty::Begin("Renderable", Icon_Component_Renderable, renderable))
	{
		//= REFLECT ================================================================
		string meshName		= renderable->Geometry_Name();
		auto material		= renderable->Material_Ptr();
		string materialName	= material ? material->GetResourceName() : NOT_ASSIGNED;
		bool castShadows	= renderable->GetCastShadows();
		bool receiveShadows = renderable->GetReceiveShadows();
		//==========================================================================

		ImGui::Text("Mesh");
		ImGui::SameLine(ComponentProperty::g_column); ImGui::Text(meshName.c_str());

		// Material
		ImGui::Text("Material");
		ImGui::SameLine(ComponentProperty::g_column); ImGui::Text(materialName.c_str());

		// Cast shadows
		ImGui::Text("Cast Shadows");
		ImGui::SameLine(ComponentProperty::g_column); ImGui::Checkbox("##RenderableCastShadows", &castShadows);

		// Receive shadows
		ImGui::Text("Receive Shadows");
		ImGui::SameLine(ComponentProperty::g_column); ImGui::Checkbox("##RenderableReceiveShadows", &receiveShadows);

		//= MAP ==============================================================================================
		if (castShadows != renderable->GetCastShadows())		renderable->SetCastShadows(castShadows);
		if (receiveShadows != renderable->GetReceiveShadows())	renderable->SetReceiveShadows(receiveShadows);
		//====================================================================================================
	}
	ComponentProperty::End();
}

void Widget_Properties::ShowRigidBody(shared_ptr<RigidBody>& rigidBody)
{
	if (!rigidBody)
		return;

	if (ComponentProperty::Begin("RigidBody", Icon_Component_RigidBody, rigidBody))
	{
		//= REFLECT ===================================================
		float mass				= rigidBody->GetMass();
		float friction			= rigidBody->GetFriction();
		float frictionRolling	= rigidBody->GetFrictionRolling();
		float restitution		= rigidBody->GetRestitution();
		bool useGravity			= rigidBody->GetUseGravity();
		bool isKinematic		= rigidBody->GetIsKinematic();
		bool freezePosX			= (bool)rigidBody->GetPositionLock().x;
		bool freezePosY			= (bool)rigidBody->GetPositionLock().y;
		bool freezePosZ			= (bool)rigidBody->GetPositionLock().z;
		bool freezeRotX			= (bool)rigidBody->GetRotationLock().x;
		bool freezeRotY			= (bool)rigidBody->GetRotationLock().y;
		bool freezeRotZ			= (bool)rigidBody->GetRotationLock().z;
		//=============================================================

		auto inputTextFlags		= ImGuiInputTextFlags_CharsDecimal;
		float itemWidth			= 120.0f;
		float step				= 0.1f;
		float step_fast			= 0.1f;
		const char* precision	= "%.3f";

		// Mass
		ImGui::Text("Mass");
		ImGui::SameLine(ComponentProperty::g_column); ImGui::PushItemWidth(itemWidth); ImGui::InputFloat("##RigidBodyMass", &mass, step, step_fast, precision, inputTextFlags); ImGui::PopItemWidth();

		// Friction
		ImGui::Text("Friction");
		ImGui::SameLine(ComponentProperty::g_column); ImGui::PushItemWidth(itemWidth); ImGui::InputFloat("##RigidBodyFriction", &friction, step, step_fast, precision, inputTextFlags); ImGui::PopItemWidth();

		// Rolling Friction
		ImGui::Text("Rolling Friction");
		ImGui::SameLine(ComponentProperty::g_column); ImGui::PushItemWidth(itemWidth); ImGui::InputFloat("##RigidBodyRollingFriction", &frictionRolling, step, step_fast, precision, inputTextFlags); ImGui::PopItemWidth();

		// Restitution
		ImGui::Text("Restitution");
		ImGui::SameLine(ComponentProperty::g_column); ImGui::PushItemWidth(itemWidth); ImGui::InputFloat("##RigidBodyRestitution", &restitution, step, step_fast, precision, inputTextFlags); ImGui::PopItemWidth();

		// Use Gravity
		ImGui::Text("Use Gravity");
		ImGui::SameLine(ComponentProperty::g_column); ImGui::Checkbox("##RigidBodyUseGravity", &useGravity);

		// Is Kinematic
		ImGui::Text("Is Kinematic");
		ImGui::SameLine(ComponentProperty::g_column); ImGui::Checkbox("##RigidBodyKinematic", &isKinematic);

		// Freeze Position
		ImGui::Text("Freeze Position");
		ImGui::SameLine(ComponentProperty::g_column); ImGui::Text("X");
		ImGui::SameLine(); ImGui::Checkbox("##RigidFreezePosX", &freezePosX);
		ImGui::SameLine(); ImGui::Text("Y");
		ImGui::SameLine(); ImGui::Checkbox("##RigidFreezePosY", &freezePosY);
		ImGui::SameLine(); ImGui::Text("Z");
		ImGui::SameLine(); ImGui::Checkbox("##RigidFreezePosZ", &freezePosZ);

		// Freeze Rotation
		ImGui::Text("Freeze Rotation");
		ImGui::SameLine(ComponentProperty::g_column); ImGui::Text("X");
		ImGui::SameLine(); ImGui::Checkbox("##RigidFreezeRotX", &freezeRotX);
		ImGui::SameLine(); ImGui::Text("Y");
		ImGui::SameLine(); ImGui::Checkbox("##RigidFreezeRotY", &freezeRotY);
		ImGui::SameLine(); ImGui::Text("Z");
		ImGui::SameLine(); ImGui::Checkbox("##RigidFreezeRotZ", &freezeRotZ);

		//= MAP =============================================================================================================================================
		if (mass != rigidBody->GetMass())						rigidBody->SetMass(mass);
		if (friction != rigidBody->GetFriction())				rigidBody->SetFriction(friction);
		if (frictionRolling != rigidBody->GetFrictionRolling())	rigidBody->SetFrictionRolling(frictionRolling);
		if (restitution != rigidBody->GetRestitution())			rigidBody->SetRestitution(restitution);
		if (useGravity != rigidBody->GetUseGravity())			rigidBody->SetUseGravity(useGravity);
		if (isKinematic != rigidBody->GetIsKinematic())			rigidBody->SetIsKinematic(isKinematic);
		if (freezePosX != (bool)rigidBody->GetPositionLock().x)	rigidBody->SetPositionLock(Vector3((float)freezePosX, (float)freezePosY, (float)freezePosZ));
		if (freezePosY != (bool)rigidBody->GetPositionLock().y)	rigidBody->SetPositionLock(Vector3((float)freezePosX, (float)freezePosY, (float)freezePosZ));
		if (freezePosZ != (bool)rigidBody->GetPositionLock().z)	rigidBody->SetPositionLock(Vector3((float)freezePosX, (float)freezePosY, (float)freezePosZ));
		if (freezeRotX != (bool)rigidBody->GetRotationLock().x)	rigidBody->SetRotationLock(Vector3((float)freezeRotX, (float)freezeRotY, (float)freezeRotZ));
		if (freezeRotY != (bool)rigidBody->GetRotationLock().y)	rigidBody->SetRotationLock(Vector3((float)freezeRotX, (float)freezeRotY, (float)freezeRotZ));
		if (freezeRotZ != (bool)rigidBody->GetRotationLock().z)	rigidBody->SetRotationLock(Vector3((float)freezeRotX, (float)freezeRotY, (float)freezeRotZ));
		//===================================================================================================================================================
	}
	ComponentProperty::End();
}

void Widget_Properties::ShowCollider(shared_ptr<Collider>& collider)
{
	if (!collider)
		return;

	if (ComponentProperty::Begin("Collider", Icon_Component_Collider, collider))
	{
		//= REFLECT ======================================================
		static vector<char*> type = {
			"Box",
			"Sphere",
			"Static Plane",
			"Cylinder",
			"Capsule",
			"Cone",
			"Mesh"
		};
		const char* shapeCharPtr	= type[(int)collider->GetShapeType()];
		bool optimize				= collider->GetOptimize();
		Vector3 colliderCenter		= collider->GetCenter();
		Vector3 colliderBoundingBox = collider->GetBoundingBox();
		//================================================================

		auto inputTextFlags		= ImGuiInputTextFlags_CharsDecimal;
		float step				= 0.1f;
		float step_fast			= 0.1f;
		const char* precision	= "%.3f";

		// Type
		ImGui::Text("Type");
		ImGui::PushItemWidth(110);
		ImGui::SameLine(ComponentProperty::g_column); if (ImGui::BeginCombo("##colliderType", shapeCharPtr))
		{
			for (unsigned int i = 0; i < (unsigned int)type.size(); i++)
			{
				bool is_selected = (shapeCharPtr == type[i]);
				if (ImGui::Selectable(type[i], is_selected))
				{
					shapeCharPtr = type[i];
					collider->SetShapeType((ColliderShape)i);
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
		ImGui::SameLine(ComponentProperty::g_column);	ImGui::PushID("colCenterX"); ImGui::InputFloat("X", &colliderCenter.x, step, step_fast, precision, inputTextFlags); ImGui::PopID();
		ImGui::SameLine();								ImGui::PushID("colCenterY"); ImGui::InputFloat("Y", &colliderCenter.y, step, step_fast, precision, inputTextFlags); ImGui::PopID();
		ImGui::SameLine();								ImGui::PushID("colCenterZ"); ImGui::InputFloat("Z", &colliderCenter.z, step, step_fast, precision, inputTextFlags); ImGui::PopID();

		// Size
		ImGui::Text("Size");
		ImGui::SameLine(ComponentProperty::g_column);	ImGui::PushID("colSizeX"); ImGui::InputFloat("X", &colliderBoundingBox.x, step, step_fast, precision, inputTextFlags); ImGui::PopID();
		ImGui::SameLine();								ImGui::PushID("colSizeY"); ImGui::InputFloat("Y", &colliderBoundingBox.y, step, step_fast, precision, inputTextFlags); ImGui::PopID();
		ImGui::SameLine();								ImGui::PushID("colSizeZ"); ImGui::InputFloat("Z", &colliderBoundingBox.z, step, step_fast, precision, inputTextFlags); ImGui::PopID();

		// Optimize
		if (collider->GetShapeType() == ColliderShape_Mesh)
		{
			ImGui::Text("Optimize");
			ImGui::SameLine(ComponentProperty::g_column); ImGui::Checkbox("##colliderOptimize", &optimize);
		}

		//= MAP ==================================================================================================
		if (colliderCenter != collider->GetCenter())				collider->SetCenter(colliderCenter);
		if (colliderBoundingBox != collider->GetBoundingBox())		collider->SetBoundingBox(colliderBoundingBox);
		if (optimize != collider->GetOptimize())					collider->SetOptimize(optimize);
		//========================================================================================================
	}
	ComponentProperty::End();
}

void Widget_Properties::ShowConstraint(shared_ptr<Constraint>& constraint)
{
	if (!constraint)
		return;

	if (ComponentProperty::Begin("Constraint", Icon_Component_AudioSource, constraint))
	{
		//= REFLECT ============================================================================
		static vector<char*> types	= {"Point", "Hinge", "Slider", "ConeTwist" };
		const char* typeStr			= types[(int)constraint->GetConstraintType()];
		weak_ptr<Entity>	otherBody	= constraint->GetBodyOther();
		bool otherBodyDirty			= false;
		Vector3 position			= constraint->GetPosition();
		Vector3 rotation			= constraint->GetRotation().ToEulerAngles();
		Vector2 highLimit			= constraint->GetHighLimit();
		Vector2 lowLimit			= constraint->GetLowLimit();
		string otherBodyName		= otherBody.expired() ? "N/A" : otherBody.lock()->GetName();
		//======================================================================================

		auto inputTextFlags		= ImGuiInputTextFlags_CharsDecimal;
		float step				= 0.1f;
		float step_fast			= 0.1f;
		const char* precision	= "%.3f";

		// Type
		ImGui::Text("Type");
		ImGui::SameLine(ComponentProperty::g_column); if (ImGui::BeginCombo("##constraintType", typeStr))
		{
			for (unsigned int i = 0; i < (unsigned int)types.size(); i++)
			{
				bool is_selected = (typeStr == types[i]);
				if (ImGui::Selectable(types[i], is_selected))
				{
					typeStr = types[i];
					constraint->SetConstraintType((ConstraintType)i);
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
		ImGui::InputText("", &otherBodyName, ImGuiInputTextFlags_ReadOnly);
		if (auto payload = DragDrop::Get().GetPayload(DragPayload_entity))
		{
			auto entityID	= get<unsigned int>(payload->data);
			otherBody		= _Widget_Properties::scene->Entity_GetByID(entityID);
			otherBodyDirty	= true;
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
		ImGui::SameLine(); ImGui::InputFloat("##ConsHighLimX", &highLimit.x, step, step_fast, precision, inputTextFlags);
		if (constraint->GetConstraintType() == ConstraintType_Slider)
		{
			ImGui::SameLine(); ImGui::Text("Y");
			ImGui::SameLine(); ImGui::InputFloat("##ConsHighLimY", &highLimit.y, step, step_fast, precision, inputTextFlags);
		}

		// Low Limit
		ImGui::Text("Low Limit");
		ImGui::SameLine(ComponentProperty::g_column); ImGui::Text("X");
		ImGui::SameLine(); ImGui::InputFloat("##ConsLowLimX", &lowLimit.x, step, step_fast, precision, inputTextFlags);
		if (constraint->GetConstraintType() == ConstraintType_Slider)
		{
			ImGui::SameLine(); ImGui::Text("Y");
			ImGui::SameLine(); ImGui::InputFloat("##ConsLowLimY", &lowLimit.y, step, step_fast, precision, inputTextFlags);
		}

		//= MAP ========================================================================================================================
		if (otherBodyDirty)												{ constraint->SetBodyOther(otherBody); otherBodyDirty = false; }
		if (position != constraint->GetPosition())						constraint->SetPosition(position);
		if (rotation != constraint->GetRotation().ToEulerAngles())		constraint->SetRotation(Quaternion::FromEulerAngles(rotation));
		if (highLimit != constraint->GetHighLimit())					constraint->SetHighLimit(highLimit);
		if (lowLimit != constraint->GetLowLimit())						constraint->SetLowLimit(lowLimit);
		//==============================================================================================================================
	}
	ComponentProperty::End();
}

void Widget_Properties::ShowMaterial(shared_ptr<Material>& material)
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

		static const ImVec2 materialTextSize = ImVec2(80, 80);

		auto texAlbedo		= material->GetTextureSlotByType(TextureType_Albedo).ptr.get();
		auto texRoughness	= material->GetTextureSlotByType(TextureType_Roughness).ptr.get();
		auto texMetallic	= material->GetTextureSlotByType(TextureType_Metallic).ptr.get();
		auto texNormal		= material->GetTextureSlotByType(TextureType_Normal).ptr.get();
		auto texHeight		= material->GetTextureSlotByType(TextureType_Height).ptr.get();
		auto texOcclusion	= material->GetTextureSlotByType(TextureType_Occlusion).ptr.get();
		auto texEmission	= material->GetTextureSlotByType(TextureType_Emission).ptr.get();
		auto texMask		= material->GetTextureSlotByType(TextureType_Mask).ptr.get();

		// Name
		ImGui::Text("Name");
		ImGui::SameLine(ComponentProperty::g_column); ImGui::Text(material->GetResourceName().c_str());

		if (material->IsEditable())
		{
			auto DisplayTextureSlot = [&material](RHI_Texture* texture, const char* textureName, TextureType textureType)
			{
				ImGui::Text(textureName);
				ImGui::SameLine(ComponentProperty::g_column); ImGui::Image(
					texture ? texture->GetShaderResource() : nullptr,
					materialTextSize,
					ImVec2(0, 0),
					ImVec2(1, 1),
					ImColor(255, 255, 255, 255),
					ImColor(255, 255, 255, 128)
				);

				if (auto payload = DragDrop::Get().GetPayload(DragPayload_Texture))
				{
					try
					{
						if (auto texture = _Widget_Properties::resourceCache->Load<RHI_Texture>(get<const char*>(payload->data)))
						{
							material->SetTextureSlot(textureType, texture);
						}
					}
					catch (const std::bad_variant_access& e) { LOGF_ERROR("Widget_Properties::ShowMaterial: %s", e.what()); }
				}
			};

			// Albedo
			DisplayTextureSlot(texAlbedo, "Albedo", TextureType_Albedo);
			ImGui::SameLine(); m_colorPicker_material->Update();

			// Roughness
			DisplayTextureSlot(texRoughness, "Roughness", TextureType_Roughness);
			roughness = material->GetRoughnessMultiplier();
			ImGui::SameLine(); ImGui::SliderFloat("##matRoughness", &roughness, 0.0f, 1.0f);

			// Metallic
			DisplayTextureSlot(texMetallic, "Metallic", TextureType_Metallic);
			metallic = material->GetMetallicMultiplier();
			ImGui::SameLine(); ImGui::SliderFloat("##matMetallic", &metallic, 0.0f, 1.0f);

			// Normal
			DisplayTextureSlot(texNormal, "Normal", TextureType_Normal);
			normal = material->GetNormalMultiplier();
			ImGui::SameLine(); ImGui::SliderFloat("##matNormal", &normal, 0.0f, 1.0f);

			// Height
			DisplayTextureSlot(texHeight, "Height", TextureType_Height);
			height = material->GetHeightMultiplier();
			ImGui::SameLine(); ImGui::SliderFloat("##matHeight", &height, 0.0f, 1.0f);

			// Occlusion
			DisplayTextureSlot(texOcclusion, "Occlusion", TextureType_Occlusion);

			// Emission
			DisplayTextureSlot(texEmission, "Emission", TextureType_Emission);

			// Mask
			DisplayTextureSlot(texMask, "Mask", TextureType_Mask);

			// Tiling
			ImGui::Text("Tiling");
			ImGui::SameLine(ComponentProperty::g_column); ImGui::Text("X"); ImGui::SameLine(); ImGui::InputFloat("##matTilingX", &tiling.x, ImGuiInputTextFlags_CharsDecimal);
			ImGui::SameLine(); ImGui::Text("Y"); ImGui::SameLine(); ImGui::InputFloat("##matTilingY", &tiling.y, ImGuiInputTextFlags_CharsDecimal);

			// Offset
			ImGui::Text("Offset");
			ImGui::SameLine(ComponentProperty::g_column); ImGui::Text("X"); ImGui::SameLine(); ImGui::InputFloat("##matOffsetX", &offset.x, ImGuiInputTextFlags_CharsDecimal);
			ImGui::SameLine(); ImGui::Text("Y"); ImGui::SameLine(); ImGui::InputFloat("##matOffsetY", &offset.y, ImGuiInputTextFlags_CharsDecimal);
		}

		//= MAP ====================================================================================================================================
		if (roughness != material->GetRoughnessMultiplier())						material->SetRoughnessMultiplier(roughness);
		if (metallic != material->GetMetallicMultiplier())							material->SetMetallicMultiplier(metallic);
		if (normal != material->GetNormalMultiplier())								material->SetNormalMultiplier(normal);
		if (height != material->GetHeightMultiplier())								material->SetHeightMultiplier(height);
		if (tiling != material->GetTiling())										material->SetTiling(tiling);
		if (offset != material->GetOffset())										material->SetOffset(offset);
		if (m_colorPicker_material->GetColor() != material->GetColorAlbedo())	material->SetColorAlbedo(m_colorPicker_material->GetColor());
		//==========================================================================================================================================
	}
	ComponentProperty::End();
}

void Widget_Properties::ShowCamera(shared_ptr<Camera>& camera)
{
	if (!camera)
		return;

	if (ComponentProperty::Begin("Camera", Icon_Component_Camera, camera))
	{
		//= REFLECT ================================================================================
		static const char* projectionTypes[]	= { "Perspective", "Orthographic" };
		const char* projectionCharPtr			= projectionTypes[(int)camera->GetProjectionType()];
		float fov								= camera->GetFOV_Horizontal_Deg();
		float nearPlane							= camera->GetNearPlane();
		float farPlane							= camera->GetFarPlane();
		m_colorPicker_camera->SetColor(camera->GetClearColor());
		//==========================================================================================

		auto inputTextFlags = ImGuiInputTextFlags_CharsDecimal;

		// Background
		ImGui::Text("Background");
		ImGui::SameLine(ComponentProperty::g_column); m_colorPicker_camera->Update();

		// Projection
		ImGui::Text("Projection");
		ImGui::SameLine(ComponentProperty::g_column);
		ImGui::PushItemWidth(110.0f);
		if (ImGui::BeginCombo("##cameraProjection", projectionCharPtr))
		{
			for (int i = 0; i < IM_ARRAYSIZE(projectionTypes); i++)
			{
				bool is_selected = (projectionCharPtr == projectionTypes[i]);
				if (ImGui::Selectable(projectionTypes[i], is_selected))
				{
					projectionCharPtr = projectionTypes[i];
					camera->SetProjection((ProjectionType)i);
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
		ImGui::SameLine(ComponentProperty::g_column);		ImGui::PushItemWidth(130); ImGui::InputFloat("Near", &nearPlane, 0.1f, 0.1f, "%.3f", inputTextFlags); ImGui::PopItemWidth();
		ImGui::SetCursorPosX(ComponentProperty::g_column);	ImGui::PushItemWidth(130); ImGui::InputFloat("Far", &farPlane, 0.1f, 0.1f, "%.3f", inputTextFlags); ImGui::PopItemWidth();

		//= MAP ====================================================================================================================
		if (fov != camera->GetFOV_Horizontal_Deg())							camera->SetFOV_Horizontal_Deg(fov);
		if (nearPlane != camera->GetNearPlane())							camera->SetNearPlane(nearPlane);
		if (farPlane != camera->GetFarPlane())								camera->SetFarPlane(farPlane);
		if (m_colorPicker_camera->GetColor() != camera->GetClearColor())	camera->SetClearColor(m_colorPicker_camera->GetColor());
		//==========================================================================================================================
	}
	ComponentProperty::End();
}

void Widget_Properties::ShowAudioSource(shared_ptr<AudioSource>& audioSource)
{
	if (!audioSource)
		return;

	if (ComponentProperty::Begin("Audio Source", Icon_Component_AudioSource, audioSource))
	{
		//= REFLECT ==============================================
		string audioClipName	= audioSource->GetAudioClipName();
		bool mute				= audioSource->GetMute();
		bool playOnStart		= audioSource->GetPlayOnStart();
		bool loop				= audioSource->GetLoop();
		int priority			= audioSource->GetPriority();
		float volume			= audioSource->GetVolume();
		float pitch				= audioSource->GetPitch();
		float pan				= audioSource->GetPan();
		//========================================================

		// Audio clip
		ImGui::Text("Audio Clip");
		ImGui::SameLine(ComponentProperty::g_column); ImGui::PushItemWidth(250.0f);
		ImGui::InputText("##audioSourceAudioClip", &audioClipName, ImGuiInputTextFlags_ReadOnly);
		ImGui::PopItemWidth();
		if (auto payload = DragDrop::Get().GetPayload(DragPayload_Audio))
		{
			audioClipName	= FileSystem::GetFileNameFromFilePath(get<const char*>(payload->data));
			auto audioClip	= _Widget_Properties::resourceCache->Load<AudioClip>(get<const char*>(payload->data));
			audioSource->SetAudioClip(audioClip);
		}

		// Mute
		ImGui::Text("Mute");
		ImGui::SameLine(ComponentProperty::g_column); ImGui::Checkbox("##audioSourceMute", &mute);

		// Play on start
		ImGui::Text("Play on Start");
		ImGui::SameLine(ComponentProperty::g_column); ImGui::Checkbox("##audioSourcePlayOnStart", &playOnStart);

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

		//= MAP =====================================================================================
		if (mute != audioSource->GetMute())					audioSource->SetMute(mute);
		if (playOnStart != audioSource->GetPlayOnStart())	audioSource->SetPlayOnStart(playOnStart);
		if (loop != audioSource->GetLoop())					audioSource->SetLoop(loop);
		if (priority != audioSource->GetPriority())			audioSource->SetPriority(priority);
		if (volume != audioSource->GetVolume())				audioSource->SetVolume(volume);
		if (pitch != audioSource->GetPitch())				audioSource->SetPitch(pitch);
		if (pan != audioSource->GetPan())					audioSource->SetPan(pan);
		//===========================================================================================
	}
	ComponentProperty::End();
}

void Widget_Properties::ShowAudioListener(shared_ptr<AudioListener>& audioListener)
{
	if (!audioListener)
		return;

	if (ComponentProperty::Begin("Audio Listener", Icon_Component_AudioListener, audioListener))
	{

	}
	ComponentProperty::End();
}

void Widget_Properties::ShowScript(shared_ptr<Script>& script)
{
	if (!script)
		return;

	if (ComponentProperty::Begin(script->GetName(), Icon_Component_Script, script))
	{
		//= REFLECT ==========================
		string scriptName = script->GetName();
		//====================================

		ImGui::Text("Script");
		ImGui::SameLine();
		ImGui::PushID("##ScriptNameTemp");
		ImGui::PushItemWidth(200.0f);
		ImGui::InputText("", &scriptName, ImGuiInputTextFlags_ReadOnly);
		ImGui::PopItemWidth();
		ImGui::PopID();
	}
	ComponentProperty::End();
}

void Widget_Properties::ShowAddComponentButton()
{
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5);
	ImGui::SetCursorPosX(ImGui::GetWindowWidth() * 0.5f - 50);
	if (ImGui::Button("Add Component"))
	{
		ImGui::OpenPopup("##ComponentContextMenu_Add");
	}
	ComponentContextMenu_Add();
}

void Widget_Properties::ComponentContextMenu_Add()
{
	if (ImGui::BeginPopup("##ComponentContextMenu_Add"))
	{
		if (auto entity = m_inspectedentity.lock())
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
		}

		ImGui::EndPopup();
	}
}

void Widget_Properties::Drop_AutoAddComponents()
{
	if (auto payload = DragDrop::Get().GetPayload(DragPayload_Script))
	{
		if (auto scriptComponent = m_inspectedentity.lock()->AddComponent<Script>())
		{
			scriptComponent->SetScript(get<const char*>(payload->data));
		}
	}
}
