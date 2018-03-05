/*
Copyright(c) 2016-2018 Panos Karabelas

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

//= INCLUDES ========================================
#include "Properties.h"
#include "../../ImGui/imgui.h"
#include "Hierarchy.h"
#include "Scene/GameObject.h"
#include "Graphics/Material.h"
#include "Graphics/Mesh.h"
#include "Scene/Components/Transform.h"
#include "Scene/Components/MeshFilter.h"
#include "Scene/Components/MeshRenderer.h"
#include "Scene/Components/RigidBody.h"
#include "Scene/Components/Collider.h"
#include "Scene/Components/Constraint.h"
#include "Scene/Components/Light.h"
#include "Scene/Components/AudioSource.h"
#include "Scene/Components/AudioListener.h"
#include "Scene/Components/Camera.h"
#include "Scene/Components/Script.h"
#include "../ThumbnailProvider.h"
#include "../EditorHelper.h"
#include "../DragDrop.h"
#include "../ButtonColorPicker.h"
#include "Graphics/DeferredShaders/ShaderVariation.h"
//===================================================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
using namespace Math;
//=======================

weak_ptr<GameObject> Properties::m_gameObject;
static ResourceManager* g_resourceManager = nullptr;
static const char* g_contexMenuID;
static const float g_maxWidth = 100.0f;

//= COLOR PICKERS ===============================================
static unique_ptr<ButtonColorPicker> g_materialButtonColorPicker;
static unique_ptr<ButtonColorPicker> g_lightButtonColorPicker;
static unique_ptr<ButtonColorPicker> g_cameraButtonColorPicker;
//===============================================================

//= WRITE UNAVOIDABLE UGLY CODE ONCE AS A MACRO =============================
#define COMPONENT_BEGIN(name, icon_enum, componentInstance)					\
	THUMBNAIL_IMAGE_BY_ENUM(icon_enum, 15);									\
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 1.5f);					\
	ImGui::SameLine(425);													\
	if (THUMBNAIL_BUTTON_TYPE_UNIQUE_ID(##name, Icon_Component_Options, 15))\
	{																		\
		g_contexMenuID = ##name;											\
		ImGui::OpenPopup(g_contexMenuID);									\
	}																		\
	if (g_contexMenuID == ##name)											\
		ComponentContextMenu_Options(g_contexMenuID, componentInstance);	\
	ImGui::SameLine(25);													\
	if (ImGui::TreeNodeEx(name, ImGuiTreeNodeFlags_DefaultOpen))			\
	{																		\
		
#define COMPONENT_BEGIN_NO_OPTIONS(name, icon_enum)					\
	THUMBNAIL_IMAGE_BY_ENUM(icon_enum, 15);							\
	ImGui::SameLine(25);											\
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 1.5f);			\
	if (ImGui::TreeNodeEx(name, ImGuiTreeNodeFlags_DefaultOpen))	\
	{																\

#define COMPONENT_END ImGui::TreePop(); \
	}									\
	ImGui::Separator()					\

#define DROP_TARGET_TEXTURE(textureType)								\
{																		\
	if (auto payload = DragDrop::Get().GetPayload(DragPayload_Texture))	\
	{																	\
		auto texture = g_resourceManager->Load<Texture>(payload->data);	\
		if (!texture.expired())											\
		{																\
			texture.lock()->SetType(textureType);						\
			material->SetTexture(texture);								\
		}																\
	}																	\
}																		\
//=======================================================================

Properties::Properties()
{
	m_title = "Properties";
	g_lightButtonColorPicker	= make_unique<ButtonColorPicker>("Light Color Picker");
	g_materialButtonColorPicker = make_unique<ButtonColorPicker>("Material Color Picker");
	g_cameraButtonColorPicker	= make_unique<ButtonColorPicker>("Camera Color Picker");
}

void Properties::Initialize(Context* context)
{
	Widget::Initialize(context);
	g_resourceManager = context->GetSubsystem<ResourceManager>();
}

void Properties::Update()
{
	if (m_gameObject.expired())
		return;
	
	HandleDropPayloads();
	auto gameObjectPtr = m_gameObject.lock().get();

	auto transform		= gameObjectPtr->GetTransform();
	auto light			= gameObjectPtr->GetComponent<Light>().lock().get();
	auto camera			= gameObjectPtr->GetComponent<Camera>().lock().get();
	auto audioSource	= gameObjectPtr->GetComponent<AudioSource>().lock().get();
	auto audioListener	= gameObjectPtr->GetComponent<AudioListener>().lock().get();
	auto meshFilter		= gameObjectPtr->GetComponent<MeshFilter>().lock().get();
	auto meshRenderer	= gameObjectPtr->GetComponent<MeshRenderer>().lock().get();
	auto material		= meshRenderer ? meshRenderer->GetMaterial().lock().get() : nullptr;
	auto rigidBody		= gameObjectPtr->GetComponent<RigidBody>().lock().get();
	auto collider		= gameObjectPtr->GetComponent<Collider>().lock().get();
	auto constraint		= gameObjectPtr->GetComponent<Constraint>().lock().get();
	auto script			= gameObjectPtr->GetComponent<Script>().lock().get();

	ImGui::PushItemWidth(g_maxWidth);

	ShowTransform(transform);
	ShowLight(light);
	ShowCamera(camera);
	ShowAudioSource(audioSource);
	ShowAudioListener(audioListener);
	ShowMeshFilter(meshFilter);
	ShowMeshRenderer(meshRenderer);
	ShowMaterial(material);
	ShowRigidBody(rigidBody);
	ShowCollider(collider);
	ShowConstraint(constraint);
	ShowScript(script);

	ShowAddComponentButton();

	ImGui::PopItemWidth();
}

void Properties::Inspect(weak_ptr<GameObject> gameObject)
{
	m_gameObject = gameObject;
}

void Properties::ShowTransform(Transform* transform)
{
	//= REFLECT ==================================================
	Vector3 position	= transform->GetPosition();
	Quaternion rotation	= transform->GetRotation();
	Vector3 scale		= transform->GetScale();

	static char g_transPosX[BUFFER_TEXT_DEFAULT];
	static char g_transPosY[BUFFER_TEXT_DEFAULT];
	static char g_transPosZ[BUFFER_TEXT_DEFAULT];
	static char g_transRotX[BUFFER_TEXT_DEFAULT];
	static char g_transRotY[BUFFER_TEXT_DEFAULT];
	static char g_transRotZ[BUFFER_TEXT_DEFAULT];
	static char g_transScaX[BUFFER_TEXT_DEFAULT];
	static char g_transScaY[BUFFER_TEXT_DEFAULT];
	static char g_transScaZ[BUFFER_TEXT_DEFAULT];
		
	EditorHelper::SetCharArray(&g_transPosX[0], position.x);
	EditorHelper::SetCharArray(&g_transPosY[0], position.y);
	EditorHelper::SetCharArray(&g_transPosZ[0], position.z);
	EditorHelper::SetCharArray(&g_transRotX[0], rotation.Pitch());
	EditorHelper::SetCharArray(&g_transRotY[0], rotation.Yaw());
	EditorHelper::SetCharArray(&g_transRotZ[0], rotation.Roll());
	EditorHelper::SetCharArray(&g_transScaX[0], scale.x);
	EditorHelper::SetCharArray(&g_transScaY[0], scale.y);
	EditorHelper::SetCharArray(&g_transScaZ[0], scale.z);
	//============================================================
			
	COMPONENT_BEGIN_NO_OPTIONS("Transform", Icon_Component_Transform);
	{
		float posX = 90.0f;
		auto inputTextFlags = ImGuiInputTextFlags_CharsDecimal;

		// Position
		ImGui::Text("Position");
		ImGui::SameLine(posX); ImGui::Text("X");
		ImGui::SameLine(); ImGui::InputText("##TransPosX", g_transPosX, BUFFER_TEXT_DEFAULT, inputTextFlags);
		ImGui::SameLine(); ImGui::Text("Y");
		ImGui::SameLine(); ImGui::InputText("##TransPosY", g_transPosY, BUFFER_TEXT_DEFAULT, inputTextFlags);
		ImGui::SameLine(); ImGui::Text("Z");
		ImGui::SameLine(); ImGui::InputText("##TransPosZ", g_transPosZ, BUFFER_TEXT_DEFAULT, inputTextFlags);

		// Rotation
		ImGui::Text("Rotation");
		ImGui::SameLine(posX); ImGui::Text("X");
		ImGui::SameLine(); ImGui::InputText("##TransRotX", g_transRotX, BUFFER_TEXT_DEFAULT, inputTextFlags);
		ImGui::SameLine(); ImGui::Text("Y");
		ImGui::SameLine(); ImGui::InputText("##TransRotY", g_transRotY, BUFFER_TEXT_DEFAULT, inputTextFlags);
		ImGui::SameLine(); ImGui::Text("Z");
		ImGui::SameLine(); ImGui::InputText("##TransRotZ", g_transRotZ, BUFFER_TEXT_DEFAULT, inputTextFlags);

		// Scale
		ImGui::Text("Scale");
		ImGui::SameLine(posX); ImGui::Text("X");
		ImGui::SameLine(); ImGui::InputText("##TransScaX", g_transScaX, BUFFER_TEXT_DEFAULT, inputTextFlags);
		ImGui::SameLine(); ImGui::Text("Y");
		ImGui::SameLine(); ImGui::InputText("##TransScaY", g_transScaY, BUFFER_TEXT_DEFAULT, inputTextFlags);
		ImGui::SameLine(); ImGui::Text("Z");
		ImGui::SameLine(); ImGui::InputText("##TransScaZ", g_transScaZ, BUFFER_TEXT_DEFAULT, inputTextFlags);
	}
	COMPONENT_END;

	//= MAP =========================================================================
	position = Vector3(
		(float)atof(&g_transPosX[0]),
		(float)atof(&g_transPosY[0]),
		(float)atof(&g_transPosZ[0])
	);

	rotation = Quaternion::FromEulerAngles(
		(float)atof(&g_transRotX[0]),
		(float)atof(&g_transRotY[0]),
		(float)atof(&g_transRotZ[0])
	);

	scale = Vector3(
		(float)atof(&g_transScaX[0]),
		(float)atof(&g_transScaY[0]),
		(float)atof(&g_transScaZ[0])
	);

	if (position	!= transform->GetPosition())	transform->SetPosition(position);
	if (rotation	!= transform->GetRotation())	transform->SetRotation(rotation);
	if (scale		!= transform->GetScale())		transform->SetScale(scale);
	//===============================================================================
}

void Properties::ShowLight(Light* light)
{
	if (!light)
		return;

	//= REFLECT =================================================================
	static const char* types[]				= { "Directional", "Point", "Spot" };
	static int typeInt				= (int)light->GetLightType();
	static const char* typeCharPtr	= types[typeInt];	
	static float intensity			= light->GetIntensity();
	static float angle				= light->GetAngle();
	static bool castsShadows		= light->GetCastShadows();
	static float range				= light->GetRange();

	static char g_lightRange[BUFFER_TEXT_DEFAULT];
	EditorHelper::SetCharArray(&g_lightRange[0], range);
	g_lightButtonColorPicker->SetColor(light->GetColor());
	//===========================================================================
	
	COMPONENT_BEGIN("Light", Icon_Component_Light, light);
	{
		static float posX = 105.0f;

		// Type
		ImGui::Text("Type");
		ImGui::SameLine(posX); if (ImGui::BeginCombo("##LightType", typeCharPtr))
		{
			for (int i = 0; i < IM_ARRAYSIZE(types); i++)
			{
				bool is_selected = (typeCharPtr == types[i]);
				if (ImGui::Selectable(types[i], is_selected))
				{
					typeCharPtr = types[i];
					typeInt = i;
				}
				if (is_selected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}

		// Color
		ImGui::Text("Color");
		ImGui::SameLine(posX); g_lightButtonColorPicker->Update();

		// Intensity
		ImGui::Text("Intensity");
		ImGui::SameLine(posX); ImGui::SliderFloat("##lightIntensity", &intensity, 0.0f, 10.0f);

		// Cast shadows
		ImGui::Text("Shadows");
		ImGui::SameLine(posX); ImGui::Checkbox("##lightShadows", &castsShadows);

		// Range
		if (typeInt != (int)LightType_Directional)
		{
			ImGui::Text("Range");
			ImGui::SameLine(posX); ImGui::InputText("##lightRange", g_lightRange, BUFFER_TEXT_DEFAULT, ImGuiInputTextFlags_CharsDecimal);
		}

		// Angle
		if (typeInt == (int)LightType_Spot)
		{
			ImGui::Text("Angle");
			ImGui::SameLine(posX); ImGui::SliderFloat("##lightAngle", &angle, 1.0f, 179.0f);
		}
	}
	COMPONENT_END;

	//= MAP =============================================================================================================
	range = (float)atof(&g_lightRange[0]);

	if ((LightType)typeInt	!= light->GetLightType())	light->SetLightType((LightType)typeInt);
	if (intensity			!= light->GetIntensity())	light->SetIntensity(intensity);
	if (castsShadows		!= light->GetCastShadows()) light->SetCastShadows(castsShadows);
	if (angle				!= light->GetAngle())		light->SetAngle(angle);
	if (range				!= light->GetRange())		light->SetRange(range);
	if (g_lightButtonColorPicker->GetColor() != light->GetColor()) light->SetColor(g_lightButtonColorPicker->GetColor());
	//===================================================================================================================
}

void Properties::ShowMeshFilter(MeshFilter* meshFilter)
{
	if (!meshFilter)
		return;

	auto mesh = meshFilter->GetMesh().lock();
	string meshName = mesh ? mesh->GetResourceName() : NOT_ASSIGNED;

	COMPONENT_BEGIN("Mesh Filter", Icon_Component_MeshFilter, meshFilter);
	{
		// Mesh
		ImGui::Text("Mesh");
		ImGui::SameLine(); ImGui::Text(meshName.c_str());
	}
	COMPONENT_END;
}

void Properties::ShowMeshRenderer(MeshRenderer* meshRenderer)
{
	if (!meshRenderer)
		return;

	auto material = meshRenderer->GetMaterial().lock();
	string materialName = material ? material->GetResourceName() : NOT_ASSIGNED;

	//= REFLECT ====================================================
	static bool castShadows		= meshRenderer->GetCastShadows();
	static bool receiveShadows	= meshRenderer->GetReceiveShadows();
	//==============================================================
	
	COMPONENT_BEGIN("Mesh Renderer", Icon_Component_MeshRenderer, meshRenderer);
	{
		static float posX = 150.0f;

		// Cast shadows
		ImGui::Text("Cast Shadows");
		ImGui::SameLine(posX); ImGui::Checkbox("##MeshRendererCast", &castShadows);

		// Receive shadows
		ImGui::Text("Receive Shadows");
		ImGui::SameLine(posX); ImGui::Checkbox("##MeshRendererReceived", &receiveShadows);

		// Material
		ImGui::Text("Material");
		ImGui::SameLine(posX); ImGui::Text(materialName.c_str());
	}
	COMPONENT_END;

	//= MAP ====================================================================================================
	if (castShadows		!= meshRenderer->GetCastShadows())		meshRenderer->SetCastShadows(castShadows);
	if (receiveShadows	!= meshRenderer->GetReceiveShadows())	meshRenderer->SetReceiveShadows(receiveShadows);
	//==========================================================================================================
}

void Properties::ShowRigidBody(RigidBody* rigidBody)
{
	if (!rigidBody)
		return;

	//= REFLECT ==============================================================
	static float mass				= rigidBody->GetMass();
	static float friction			= rigidBody->GetFriction();
	static float frictionRolling	= rigidBody->GetFrictionRolling();
	static float restitution		= rigidBody->GetRestitution();
	static bool useGravity			= rigidBody->GetUseGravity();
	static bool isKinematic			= rigidBody->GetIsKinematic();
	static bool freezePosX			= (bool)rigidBody->GetPositionLock().x;
	static bool freezePosY			= (bool)rigidBody->GetPositionLock().y;
	static bool freezePosZ			= (bool)rigidBody->GetPositionLock().z;
	static bool freezeRotX			= (bool)rigidBody->GetRotationLock().x;
	static bool freezeRotY			= (bool)rigidBody->GetRotationLock().y;
	static bool freezeRotZ			= (bool)rigidBody->GetRotationLock().z;

	static char massCharArray[BUFFER_TEXT_DEFAULT]				= "0";
	static char frictionCharArray[BUFFER_TEXT_DEFAULT]			= "0";
	static char frictionRollingCharArray[BUFFER_TEXT_DEFAULT]	= "0";
	static char restitutionCharArray[BUFFER_TEXT_DEFAULT]		= "0";

	EditorHelper::SetCharArray(&massCharArray[0], mass);
	EditorHelper::SetCharArray(&frictionCharArray[0], friction);
	EditorHelper::SetCharArray(&frictionRollingCharArray[0], frictionRolling);
	EditorHelper::SetCharArray(&restitutionCharArray[0], restitution);
	//========================================================================

	COMPONENT_BEGIN("RigidBody", Icon_Component_RigidBody, rigidBody);
	{
		static float posX = 150.0f;
		auto inputTextFlags = ImGuiInputTextFlags_CharsDecimal;

		// Mass
		ImGui::Text("Mass");
		ImGui::SameLine(posX); ImGui::InputText("##RigidBodyMass", massCharArray, BUFFER_TEXT_DEFAULT, inputTextFlags);

		// Friction
		ImGui::Text("Friction");
		ImGui::SameLine(posX); ImGui::InputText("##RigidBodyFriction", frictionCharArray, BUFFER_TEXT_DEFAULT, inputTextFlags);

		// Rolling Friction
		ImGui::Text("Rolling Friction");
		ImGui::SameLine(posX); ImGui::InputText("##RigidBodyRollingFriction", frictionRollingCharArray, BUFFER_TEXT_DEFAULT, inputTextFlags);

		// Restitution
		ImGui::Text("Restitution");
		ImGui::SameLine(posX); ImGui::InputText("##RigidBodyRestitution", restitutionCharArray, BUFFER_TEXT_DEFAULT, inputTextFlags);

		// Use Gravity
		ImGui::Text("Use Gravity");
		ImGui::SameLine(posX); ImGui::Checkbox("##RigidBodyUseGravity", &useGravity);

		// Is Kinematic
		ImGui::Text("Is Kinematic");
		ImGui::SameLine(posX); ImGui::Checkbox("##RigidBodyRestitution", &isKinematic);

		// Freeze Position
		ImGui::Text("Freeze Position");
		ImGui::SameLine(posX); ImGui::Text("X");
		ImGui::SameLine(); ImGui::Checkbox("##RigidFreezePosX", &freezePosX);
		ImGui::SameLine(); ImGui::Text("Y");
		ImGui::SameLine(); ImGui::Checkbox("##RigidFreezePosY", &freezePosY);
		ImGui::SameLine(); ImGui::Text("Z");
		ImGui::SameLine(); ImGui::Checkbox("##RigidFreezePosZ", &freezePosZ);

		// Freeze Rotation
		ImGui::Text("Freeze Rotation");
		ImGui::SameLine(posX); ImGui::Text("X");
		ImGui::SameLine(); ImGui::Checkbox("##RigidFreezeRotX", &freezeRotX);
		ImGui::SameLine(); ImGui::Text("Y");
		ImGui::SameLine(); ImGui::Checkbox("##RigidFreezeRotY", &freezeRotY);
		ImGui::SameLine(); ImGui::Text("Z");
		ImGui::SameLine(); ImGui::Checkbox("##RigidFreezeRotZ", &freezeRotZ);
	}
	COMPONENT_END;

	//= MAP =====================================================================================================================================================
	mass			= (float)atof(&massCharArray[0]);
	friction		= (float)atof(&frictionCharArray[0]);
	frictionRolling = (float)atof(&frictionRollingCharArray[0]);
	restitution		= (float)atof(&restitutionCharArray[0]);
	rigidBody->SetPositionLock(Vector3(
			(float)freezePosX,
			(float)freezePosY,
			(float)freezePosZ
		));
		rigidBody->SetRotationLock(Vector3(
			(float)freezeRotX,
			(float)freezeRotY,
			(float)freezeRotZ
		));

	if (mass			!= rigidBody->GetMass())					rigidBody->SetMass(mass);
	if (friction		!= rigidBody->GetFriction())				rigidBody->SetFriction(friction);
	if (frictionRolling != rigidBody->GetFrictionRolling())			rigidBody->SetFrictionRolling(frictionRolling);
	if (restitution		!= rigidBody->GetRestitution())				rigidBody->SetRestitution(restitution);
	if (useGravity		!= rigidBody->GetUseGravity())				rigidBody->SetUseGravity(useGravity);
	if (isKinematic		!= rigidBody->GetIsKinematic())				rigidBody->SetIsKinematic(isKinematic);
	if (freezePosX		!= (bool)rigidBody->GetPositionLock().x)	rigidBody->SetPositionLock(Vector3((float)freezePosX, (float)freezePosY, (float)freezePosZ));
	if (freezePosY		!= (bool)rigidBody->GetPositionLock().y)	rigidBody->SetPositionLock(Vector3((float)freezePosX, (float)freezePosY, (float)freezePosZ));
	if (freezePosZ		!= (bool)rigidBody->GetPositionLock().z)	rigidBody->SetPositionLock(Vector3((float)freezePosX, (float)freezePosY, (float)freezePosZ));
	if (freezeRotX		!= (bool)rigidBody->GetRotationLock().x)	rigidBody->SetRotationLock(Vector3((float)freezeRotX, (float)freezeRotY, (float)freezeRotZ));
	if (freezeRotY		!= (bool)rigidBody->GetRotationLock().y)	rigidBody->SetRotationLock(Vector3((float)freezeRotX, (float)freezeRotY, (float)freezeRotZ));
	if (freezeRotZ		!= (bool)rigidBody->GetRotationLock().z)	rigidBody->SetRotationLock(Vector3((float)freezeRotX, (float)freezeRotY, (float)freezeRotZ));
	//===========================================================================================================================================================
}

void Properties::ShowCollider(Collider* collider)
{
	if (!collider)
		return;
	
	//= REFLECT ==========================================================
	static const char* g_colShapes[] = {
		"Box",
		"Sphere",
		"Static Plane",
		"Cylinder",
		"Capsule",
		"Cone",
		"Mesh"
	};	
	static int shapeInt = (int)collider->GetShapeType();
	static const char* shapeCharPtr = g_colShapes[shapeInt];
	static bool optimize = collider->GetOptimize();
	static Vector3 colliderCenter = collider->GetCenter();
	static Vector3 colliderBoundingBox = collider->GetBoundingBox();

	static char centerXCharArray[BUFFER_TEXT_DEFAULT]	= "0";
	static char centerYCharArray[BUFFER_TEXT_DEFAULT]	= "0";
	static char centerZCharArray[BUFFER_TEXT_DEFAULT]	= "0";
	static char sizeXCharArray[BUFFER_TEXT_DEFAULT]		= "0";
	static char sizeYCharArray[BUFFER_TEXT_DEFAULT]		= "0";
	static char sizeZCharArray[BUFFER_TEXT_DEFAULT]		= "0";

	EditorHelper::SetCharArray(&centerXCharArray[0], colliderCenter.x);
	EditorHelper::SetCharArray(&centerYCharArray[0], colliderCenter.y);
	EditorHelper::SetCharArray(&centerZCharArray[0], colliderCenter.z);
	EditorHelper::SetCharArray(&sizeXCharArray[0], colliderBoundingBox.x);
	EditorHelper::SetCharArray(&sizeYCharArray[0], colliderBoundingBox.y);
	EditorHelper::SetCharArray(&sizeZCharArray[0], colliderBoundingBox.z);
	//====================================================================

	COMPONENT_BEGIN("Collider", Icon_Component_Collider, collider);
	{
		static float posX = 90.0f;
		auto inputTextFlags = ImGuiInputTextFlags_CharsDecimal;

		// Type
		ImGui::Text("Type");
		ImGui::SameLine(posX); if (ImGui::BeginCombo("##colliderType", shapeCharPtr))
		{
			for (int i = 0; i < IM_ARRAYSIZE(g_colShapes); i++)
			{
				bool is_selected = (shapeCharPtr == g_colShapes[i]);
				if (ImGui::Selectable(g_colShapes[i], is_selected))
				{
					shapeCharPtr = g_colShapes[i];
					shapeInt = i;
				}
				if (is_selected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}

		// Center
		ImGui::Text("Center");
		ImGui::SameLine(posX); ImGui::Text("X");
		ImGui::SameLine(); ImGui::InputText("##colliderCenterX", centerXCharArray, BUFFER_TEXT_DEFAULT, inputTextFlags);
		ImGui::SameLine(); ImGui::Text("Y");
		ImGui::SameLine(); ImGui::InputText("##colliderCenterY", centerYCharArray, BUFFER_TEXT_DEFAULT, inputTextFlags);
		ImGui::SameLine(); ImGui::Text("Z");
		ImGui::SameLine(); ImGui::InputText("##colliderCenterZ", centerZCharArray, BUFFER_TEXT_DEFAULT, inputTextFlags);

		// Size
		ImGui::Text("Size");
		ImGui::SameLine(posX); ImGui::Text("X");
		ImGui::SameLine(); ImGui::InputText("##colliderSizeX", sizeXCharArray, BUFFER_TEXT_DEFAULT, inputTextFlags);
		ImGui::SameLine(); ImGui::Text("Y");
		ImGui::SameLine(); ImGui::InputText("##colliderSizeY", sizeYCharArray, BUFFER_TEXT_DEFAULT, inputTextFlags);
		ImGui::SameLine(); ImGui::Text("Z");
		ImGui::SameLine(); ImGui::InputText("##colliderSizeZ", sizeZCharArray, BUFFER_TEXT_DEFAULT, inputTextFlags);

		// Optimize
		if (shapeInt == (int)ColliderShape_Mesh)
		{
			ImGui::Text("Optimize");
			ImGui::SameLine(posX); ImGui::Checkbox("##colliderOptimize", &optimize);
		}
	}
	COMPONENT_END;

	//= MAP ====================================================================================================
	colliderCenter.x		= (float)atof(&centerXCharArray[0]);
	colliderCenter.y		= (float)atof(&centerYCharArray[0]);
	colliderCenter.z		= (float)atof(&centerZCharArray[0]);
	colliderBoundingBox.x	= (float)atof(&sizeXCharArray[0]);
	colliderBoundingBox.y	= (float)atof(&sizeYCharArray[0]);
	colliderBoundingBox.z	= (float)atof(&sizeZCharArray[0]);

	if ((ColliderShape)shapeInt != collider->GetShapeType())	collider->SetShapeType((ColliderShape)shapeInt);
	if (colliderCenter			!= collider->GetCenter())		collider->SetCenter(colliderCenter);
	if (colliderBoundingBox		!= collider->GetBoundingBox())	collider->SetBoundingBox(colliderBoundingBox);
	if (optimize				!= collider->GetOptimize())		collider->SetOptimize(optimize);	
	//==========================================================================================================
}

void Properties::ShowConstraint(Constraint* collider)
{
	if (!collider)
		return;

	COMPONENT_BEGIN("Constraint", Icon_Component_AudioSource, collider);
	{

	}
	COMPONENT_END;
}

void Properties::ShowMaterial(Material* material)
{
	if (!material)
		return;

	//= REFLECT ======================================================
	static float roughness	= material->GetRoughnessMultiplier();
	static float metallic	= material->GetMetallicMultiplier();
	static float normal		= material->GetNormalMultiplier();
	static float height		= material->GetHeightMultiplier();
	static Vector2 tiling	=  material->GetTiling();
	static Vector2 offset	=  material->GetOffset();
	g_materialButtonColorPicker->SetColor(material->GetColorAlbedo());

	static char tilingXCharArray[BUFFER_TEXT_DEFAULT];
	static char tilingYCharArray[BUFFER_TEXT_DEFAULT];
	static char offsetXCharArray[BUFFER_TEXT_DEFAULT];
	static char offsetYCharArray[BUFFER_TEXT_DEFAULT];	

	EditorHelper::SetCharArray(&tilingXCharArray[0], tiling.x);
	EditorHelper::SetCharArray(&tilingYCharArray[0], tiling.y);
	EditorHelper::SetCharArray(&offsetXCharArray[0], offset.x);
	EditorHelper::SetCharArray(&offsetYCharArray[0], offset.y);
	//================================================================

	COMPONENT_BEGIN_NO_OPTIONS("Material", Icon_Component_Material);
	{
		static float posX = 100.0f;
		static const ImVec2 materialTextSize = ImVec2(80, 80);

		auto texAlbedo		= material->GetTextureByType(TextureType_Albedo).lock();
		auto texRoughness	= material->GetTextureByType(TextureType_Roughness).lock();
		auto texMetallic	= material->GetTextureByType(TextureType_Metallic).lock();
		auto texNormal		= material->GetTextureByType(TextureType_Normal).lock();
		auto texHeight		= material->GetTextureByType(TextureType_Height).lock();
		auto texOcclusion	= material->GetTextureByType(TextureType_Occlusion).lock();
		auto texMask		= material->GetTextureByType(TextureType_Mask).lock();

		// Name
		ImGui::Text("Name");
		ImGui::SameLine(posX); ImGui::Text(material->GetResourceName().c_str());

		// Shader
		ImGui::Text("Shader");
		ImGui::SameLine(posX); ImGui::Text(!material->GetShader().expired() ? material->GetShader().lock()->GetResourceName().c_str() : NOT_ASSIGNED);

#define MAT_TEX(tex, texName, texEnum)					\
		ImGui::Text(texName);							\
		ImGui::SameLine(posX); ImGui::Image(			\
			tex ? tex->GetShaderResource() : nullptr,	\
			materialTextSize,							\
			ImVec2(0, 0),								\
			ImVec2(1, 1),								\
			ImColor(255, 255, 255, 255),				\
			ImColor(255, 255, 255, 128)					\
		);												\
		DROP_TARGET_TEXTURE(texEnum);					\

		if (material->IsEditable())
		{
			// Albedo
			MAT_TEX(texAlbedo, "Albedo", TextureType_Albedo); 
			ImGui::SameLine(); g_materialButtonColorPicker->Update();

			// Roughness
			MAT_TEX(texRoughness, "Roughness", TextureType_Roughness);
			roughness = material->GetRoughnessMultiplier();
			ImGui::SameLine(); ImGui::SliderFloat("##matRoughness", &roughness, 0.0f, 1.0f);

			// Metallic
			MAT_TEX(texMetallic, "Metallic", TextureType_Metallic); 
			metallic = material->GetMetallicMultiplier();
			ImGui::SameLine(); ImGui::SliderFloat("##matMetallic", &metallic, 0.0f, 1.0f);

			// Normal
			MAT_TEX(texNormal, "Normal", TextureType_Normal);
			normal = material->GetNormalMultiplier();
			ImGui::SameLine(); ImGui::SliderFloat("##matNormal", &normal, 0.0f, 1.0f);

			// Height
			MAT_TEX(texHeight, "Height", TextureType_Height); 
			height = material->GetHeightMultiplier();
			ImGui::SameLine(); ImGui::SliderFloat("##matHeight", &height, 0.0f, 1.0f);

			// Occlusion
			MAT_TEX(texOcclusion, "Occlusion", TextureType_Occlusion);

			// Mask
			MAT_TEX(texMask, "Mask", TextureType_Mask);

			// Tiling
			ImGui::Text("Tiling");
			ImGui::SameLine(posX); ImGui::Text("X"); ImGui::SameLine(); ImGui::InputText("##matTilingX", tilingXCharArray, BUFFER_TEXT_DEFAULT, ImGuiInputTextFlags_CharsDecimal);
			ImGui::SameLine(); ImGui::Text("Y"); ImGui::SameLine(); ImGui::InputText("##matTilingY", tilingYCharArray, BUFFER_TEXT_DEFAULT, ImGuiInputTextFlags_CharsDecimal);

			// Offset
			ImGui::Text("Offset");
			ImGui::SameLine(posX); ImGui::Text("X"); ImGui::SameLine(); ImGui::InputText("##matOffsetX", offsetXCharArray, BUFFER_TEXT_DEFAULT, ImGuiInputTextFlags_CharsDecimal);
			ImGui::SameLine(); ImGui::Text("Y"); ImGui::SameLine(); ImGui::InputText("##matOffsetY", offsetYCharArray, BUFFER_TEXT_DEFAULT, ImGuiInputTextFlags_CharsDecimal);
		}
	}
	COMPONENT_END;

	//= MAP =====================================================================================================================================
	tiling.x = (float)atof(&tilingXCharArray[0]);
	tiling.y = (float)atof(&tilingYCharArray[0]);
	offset.x = (float)atof(&offsetXCharArray[0]);
	offset.y = (float)atof(&offsetYCharArray[0]);

	if (roughness	!= material->GetRoughnessMultiplier())	material->SetRoughnessMultiplier(roughness);
	if (metallic	!= material->GetMetallicMultiplier())	material->SetMetallicMultiplier(metallic);
	if (normal		!= material->GetNormalMultiplier())		material->SetNormalMultiplier(normal);
	if (height		!= material->GetHeightMultiplier())		material->SetHeightMultiplier(height);
	if (tiling		!= material->GetTiling())				material->SetTiling(tiling);
	if (offset		!= material->GetOffset())				material->SetOffset(offset);
	if (g_materialButtonColorPicker->GetColor()	!= material->GetColorAlbedo()) material->SetColorAlbedo(g_materialButtonColorPicker->GetColor());
	//===========================================================================================================================================
}

void Properties::ShowCamera(Camera* camera)
{
	if (!camera)
		return;

	//= REFLECT ===============================================================
	static const char* projectionTypes[]	= { "Pespective", "Orthographic" };
	static int projectionInt				= (int)camera->GetProjection();
	static const char* projectionCharPtr	= projectionTypes[projectionInt];
	static float fov						= camera->GetFOV_Horizontal_Deg();
	static float nearPlane					= camera->GetNearPlane();
	static float farPlane					= camera->GetFarPlane();

	static char nearPlaneCharArray[BUFFER_TEXT_DEFAULT];
	static char farPlaneCharArray[BUFFER_TEXT_DEFAULT];

	EditorHelper::SetCharArray(&nearPlaneCharArray[0], nearPlane);
	EditorHelper::SetCharArray(&farPlaneCharArray[0], farPlane);
	g_cameraButtonColorPicker->SetColor(camera->GetClearColor());
	//=========================================================================

	COMPONENT_BEGIN("Camera", Icon_Component_Camera, camera);
	{
		auto inputTextFlags = ImGuiInputTextFlags_CharsDecimal;
		static float posX = 150.0f;

		// Background
		ImGui::Text("Background");
		ImGui::SameLine(posX); g_cameraButtonColorPicker->Update();

		// Projection
		ImGui::Text("Projection");
		ImGui::SameLine(posX); if (ImGui::BeginCombo("##cameraProjection", projectionCharPtr))
		{
			for (int i = 0; i < IM_ARRAYSIZE(projectionTypes); i++)
			{
				bool is_selected = (projectionCharPtr == projectionTypes[i]);
				if (ImGui::Selectable(projectionTypes[i], is_selected))
				{
					projectionCharPtr = projectionTypes[i];
					projectionInt = i;
				}
				if (is_selected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}

		// Field of View
		ImGui::Text("Field of View");
		ImGui::SameLine(posX); ImGui::SliderFloat("##cameraFOV", &fov, 1.0f, 179.0f);

		// Clipping Planes
		ImGui::Text("Clipping Planes");
		ImGui::SameLine(posX);		ImGui::Text("Near");	ImGui::SameLine(); ImGui::InputText("##cameraNear", nearPlaneCharArray, BUFFER_TEXT_DEFAULT, inputTextFlags);
		ImGui::SetCursorPosX(posX); ImGui::Text("Far");		ImGui::SameLine(); ImGui::InputText("##cameraFar", farPlaneCharArray, BUFFER_TEXT_DEFAULT, inputTextFlags);
	}
	COMPONENT_END;

	//= MAP =====================================================================================================================================
	nearPlane = (float)atof(&nearPlaneCharArray[0]);
	farPlane = (float)atof(&farPlaneCharArray[0]);

	if ((ProjectionType)projectionInt			!= camera->GetProjection())			camera->SetProjection((ProjectionType)projectionInt);
	if (fov										!= camera->GetFOV_Horizontal_Deg()) camera->SetFOV_Horizontal_Deg(fov);
	if (nearPlane								!= camera->GetNearPlane())			camera->SetNearPlane(nearPlane);
	if (farPlane								!= camera->GetFarPlane())			camera->SetFarPlane(farPlane);
	if (g_cameraButtonColorPicker->GetColor()	!= camera->GetClearColor())			camera->SetClearColor(g_cameraButtonColorPicker->GetColor());
	//===========================================================================================================================================
}

void Properties::ShowAudioSource(AudioSource* audioSource)
{
	if (!audioSource)
		return;

	//= REFLECT ============================================
	static char audioClipCharArray[BUFFER_TEXT_DEFAULT];
	static bool mute		= audioSource->GetMute();
	static bool playOnStart = audioSource->GetPlayOnStart();
	static bool loop		= audioSource->GetLoop();
	static int priority		= audioSource->GetPriority();
	static float volume		= audioSource->GetVolume();
	static float pitch		= audioSource->GetPitch();
	static float pan		= audioSource->GetPan();
	//======================================================

	COMPONENT_BEGIN("Audio Source", Icon_Component_AudioSource, audioSource);
	{
		static float posX = 120;

		// Audio clip
		ImGui::Text("Audio Clip");
		ImGui::SameLine(posX); ImGui::PushItemWidth(250.0f);
		ImGui::InputText("##audioSourceAudioClip", audioClipCharArray, BUFFER_TEXT_DEFAULT, ImGuiInputTextFlags_ReadOnly);
		ImGui::PopItemWidth();
		if (auto payload = DragDrop::Get().GetPayload(DragPayload_Audio))													
		{		
			EditorHelper::SetCharArray(&audioClipCharArray[0], FileSystem::GetFileNameFromFilePath(payload->data));
			auto audioClip = g_resourceManager->Load<AudioClip>(payload->data);	
			audioSource->SetAudioClip(audioClip, false);											
		}																	

		// Mute
		ImGui::Text("Mute");
		ImGui::SameLine(posX); ImGui::Checkbox("##audioSourceMute", &mute);

		// Play on start
		ImGui::Text("Play on Start");
		ImGui::SameLine(posX); ImGui::Checkbox("##audioSourcePlayOnStart", &playOnStart);

		// Loop
		ImGui::Text("Loop");
		ImGui::SameLine(posX); ImGui::Checkbox("##audioSourceLoop", &loop);

		// Priority
		ImGui::Text("Priority");
		ImGui::SameLine(posX); ImGui::SliderInt("##audioSourcePriority", &priority, 0, 255);

		// Volume
		ImGui::Text("Volume");
		ImGui::SameLine(posX); ImGui::SliderFloat("##audioSourceVolume", &volume, 0.0f, 1.0f);

		// Pitch
		ImGui::Text("Pitch");
		ImGui::SameLine(posX); ImGui::SliderFloat("##audioSourcePitch", &pitch, 0.0f, 3.0f);

		// Pan
		ImGui::Text("Pan");
		ImGui::SameLine(posX); ImGui::SliderFloat("##audioSourcePan", &pan, -1.0f, 1.0f);
	}
	COMPONENT_END;

	//= MAP =====================================================================================
	if (mute		!= audioSource->GetMute())			audioSource->SetMute(mute);
	if (playOnStart != audioSource->GetPlayOnStart())	audioSource->SetPlayOnStart(playOnStart);
	if (loop		!= audioSource->GetLoop())			audioSource->SetLoop(loop);
	if (priority	!= audioSource->GetPriority())		audioSource->SetPriority(priority);
	if (volume		!= audioSource->GetVolume())		audioSource->SetVolume(volume);
	if (pitch		!= audioSource->GetPitch())			audioSource->SetPitch(pitch);
	if (pan			!= audioSource->GetPan())			audioSource->SetPan(pan);
	//===========================================================================================
}

void Properties::ShowAudioListener(AudioListener* audioListener)
{
	if (!audioListener)
		return;

	COMPONENT_BEGIN("Audio Listener", Icon_Component_AudioListener, audioListener);
	{
		
	}
	COMPONENT_END;
}

void Properties::ShowScript(Script* script)
{
	if (!script)
		return;

	COMPONENT_BEGIN("Script", Icon_Component_Script, script)
	{
		// Name
		ImGui::Text("Name");
		ImGui::SameLine(105); ImGui::Text(script->GetName().c_str());
	}
	COMPONENT_END;
}

void Properties::ComponentContextMenu_Options(const char* id, IComponent* component)
{
	if (ImGui::BeginPopup(id))
	{
		if (ImGui::MenuItem("Remove"))
		{
			if (auto gameObject = Hierarchy::GetSelectedGameObject().lock())
			{
				if (component)
				{
					gameObject->RemoveComponentByID(component->GetID());
				}
			}
		}

		ImGui::EndPopup();
	}
}

void Properties::ShowAddComponentButton()
{
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5);
	ImGui::SetCursorPosX(ImGui::GetWindowWidth() * 0.5f - 50);
	if (ImGui::Button("Add Component"))
	{
		ImGui::OpenPopup("##ComponentContextMenu_Add");
	}
	ComponentContextMenu_Add();
}

void Properties::ComponentContextMenu_Add()
{
	if (ImGui::BeginPopup("##ComponentContextMenu_Add"))
	{
		if (auto gameObject = Hierarchy::GetSelectedGameObject().lock())
		{
			// CAMERA
			if (ImGui::MenuItem("Camera"))
			{
				gameObject->AddComponent<Camera>();
			}

			// LIGHT
			if (ImGui::BeginMenu("Light"))
			{
				if (ImGui::MenuItem("Directional"))
				{
					gameObject->AddComponent<Light>().lock()->SetLightType(LightType_Directional);
				}
				else if (ImGui::MenuItem("Point"))
				{
					gameObject->AddComponent<Light>().lock()->SetLightType(LightType_Point);
				}
				else if (ImGui::MenuItem("Spot"))
				{
					gameObject->AddComponent<Light>().lock()->SetLightType(LightType_Spot);
				}

				ImGui::EndMenu();
			}

			// PHYSICS
			if (ImGui::BeginMenu("Physics"))
			{
				if (ImGui::MenuItem("Rigid Body"))
				{
					gameObject->AddComponent<RigidBody>();
				}
				else if (ImGui::MenuItem("Collider"))
				{
					gameObject->AddComponent<Collider>();
				}
				else if (ImGui::MenuItem("Constraint"))
				{
					gameObject->AddComponent<Constraint>();
				}

				ImGui::EndMenu();
			}

			// AUDIO
			if (ImGui::BeginMenu("Audio"))
			{
				if (ImGui::MenuItem("Audio Source"))
				{
					gameObject->AddComponent<AudioSource>();
				}
				else if (ImGui::MenuItem("Audio Listener"))
				{
					gameObject->AddComponent<AudioListener>();
				}

				ImGui::EndMenu();
			}
		}

		ImGui::EndPopup();
	}
}

void Properties::HandleDropPayloads()
{
	auto gameObjectPtr = m_gameObject.lock().get();
	if (auto payload = DragDrop::Get().GetPayload(DragPayload_Script))
	{
		if (auto scriptComponent = gameObjectPtr->AddComponent<Script>().lock())
		{
			scriptComponent->SetScript(payload->data);
		}
	}
}
