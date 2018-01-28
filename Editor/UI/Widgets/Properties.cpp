/*
Copyright(c) 2016-2017 Panos Karabelas

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

//= INCLUDES ========================
#include "Properties.h"
#include "../imgui/imgui.h"
#include "Hierarchy.h"
#include "Scene/GameObject.h"
#include "Graphics/Material.h"
#include "Graphics/Mesh.h"
#include "Components/Transform.h"
#include "Components/MeshFilter.h"
#include "Components/MeshRenderer.h"
#include "Components/RigidBody.h"
#include "Components/Collider.h"
#include "Components/Constraint.h"
#include "Components/Light.h"
#include "Components/AudioSource.h"
#include "Components/AudioListener.h"
#include "Components/Camera.h"
#include "Components/Script.h"
#include "../IconProvider.h"
#include "../EditorHelper.h"
#include "../DragDrop.h"
#include "../ButtonColorPicker.h"
//===================================

namespace Directus {
	class Constraint;
}

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
using namespace Math;
//=======================

//= SETTINGS ==========================
static const float g_maxWidth = 100.0f;
//=====================================

// Reflected properties

//= TRANSFORM ===============================
static char g_transPosX[BUFFER_TEXT_DEFAULT];
static char g_transPosY[BUFFER_TEXT_DEFAULT];
static char g_transPosZ[BUFFER_TEXT_DEFAULT];
static char g_transRotX[BUFFER_TEXT_DEFAULT];
static char g_transRotY[BUFFER_TEXT_DEFAULT];
static char g_transRotZ[BUFFER_TEXT_DEFAULT];
static char g_transScaX[BUFFER_TEXT_DEFAULT];
static char g_transScaY[BUFFER_TEXT_DEFAULT];
static char g_transScaZ[BUFFER_TEXT_DEFAULT];
//===========================================

//= MATERIAL ====================================================
static unique_ptr<ButtonColorPicker> g_materialButtonColorPicker;
static const ImVec2 g_materialTexSize = ImVec2(80, 80);
static float g_materialRoughness = 0.0f;
static float g_materialMetallic = 0.0f;
static float g_materialNormal = 0.0f;
static float g_materialHeight = 0.0f;
static char g_matTilingX[BUFFER_TEXT_DEFAULT];
static char g_matTilingY[BUFFER_TEXT_DEFAULT];
static char g_matOffsetX[BUFFER_TEXT_DEFAULT];
static char g_matOffsetY[BUFFER_TEXT_DEFAULT];
//===============================================================

//= MESH RENDERER ===============================
static bool g_meshRendererCastShadows = false;
static bool g_meshRendererReceiveShadows = false;
//===============================================

//= LIGHT ======================================================
const char* g_lightTypes[] = { "Directional", "Point", "Spot" };
static const char* g_lightType = nullptr;
static int g_lightTypeInt = -1;
static unique_ptr<ButtonColorPicker> g_lightButtonColorPicker;
static float g_lightIntensity = 0.0f;
static char g_lightRange[BUFFER_TEXT_DEFAULT];
static float g_lightAngle = 0.0f;
static bool g_lightShadows;
//==============================================================

//= CAMERA ========================================================
static unique_ptr<ButtonColorPicker> g_cameraButtonColorPicker;
const char* g_cameraProjections[] = { "Pespective", "Orthographic" };
static const char* g_cameraProjection = nullptr;
static int g_cameraProjectionInt = -1;
static float g_cameraFOV;
static char g_cameraNear[BUFFER_TEXT_DEFAULT];
static char g_cameraFar[BUFFER_TEXT_DEFAULT];
//=================================================================

//= RIGIBODY ===============================================
static char g_rigidBodyMass[BUFFER_TEXT_DEFAULT];
static char g_rigidBodyFriction[BUFFER_TEXT_DEFAULT];
static char g_rigidBodyFrictionRolling[BUFFER_TEXT_DEFAULT];
static char g_rigidBodyRestitution[BUFFER_TEXT_DEFAULT];
static bool g_rigidBodyUseGravity;
static bool g_rigidBodyIsKinematic;
static bool g_rigidBodyFreezePosX;
static bool g_rigidBodyFreezePosY;
static bool g_rigidBodyFreezePosZ;
static bool g_rigidBodyFreezeRotX;
static bool g_rigidBodyFreezeRotY;
static bool g_rigidBodyFreezeRotZ;
//==========================================================

//= COLLIDER =========================================
const char* g_colShapes[] = {
	"Box",
	"Sphere",
	"Static Plane",
	"Cylinder",
	"Capsule",
	"Cone",
	"Mesh"
};
static const char* g_colShape = nullptr;
static int g_colShapeInt = -1;
static char g_colCenterX[BUFFER_TEXT_DEFAULT]	= "0";
static char g_colCenterY[BUFFER_TEXT_DEFAULT]	= "0";
static char g_colCenterZ[BUFFER_TEXT_DEFAULT]	= "0";
static char g_colSizeX[BUFFER_TEXT_DEFAULT]		= "0";
static char g_colSizeY[BUFFER_TEXT_DEFAULT]		= "0";
static char g_colSizeZ[BUFFER_TEXT_DEFAULT]		= "0";
static bool g_colOptimize = false;
//====================================================

static ResourceManager* g_resourceManager = nullptr;

#define COMPONENT_BEGIN(name, icon_enum, componentInstance)				\
	ICON_PROVIDER_IMAGE(icon_enum, 15);									\
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 1.5f);				\
	ImGui::SameLine(420);												\
	if (ICON_PROVIDER_IMAGE_BUTTON(Icon_Component_Options, 15))			\
	{																	\
		ImGui::OpenPopup("##ComponentContextMenu");						\
	}																	\
	Component_ContextMenu(componentInstance);							\
	ImGui::SameLine(25);												\
	if (ImGui::TreeNodeEx(name, ImGuiTreeNodeFlags_DefaultOpen))		\
	{																	\
		
#define COMPONENT_BEGIN_NO_OPTIONS(name, icon_enum)					\
	ICON_PROVIDER_IMAGE(icon_enum, 15);								\
	ImGui::SameLine(25);											\
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 1.5f);			\
	if (ImGui::TreeNodeEx(name, ImGuiTreeNodeFlags_DefaultOpen))	\
	{																\

#define COMPONENT_END ImGui::TreePop(); \
	}									\
	ImGui::Separator()					\

#define DROP_TARGET_TEXTURE(textureType)								\
{																		\
	auto payload = DragDrop::GetPayload(g_dragDrop_Type_Texture); 		\
	if (payload.data)													\
	{																	\
		auto texture = g_resourceManager->Load<Texture>(payload.data);	\
		if (!texture.expired())											\
		{																\
			texture.lock()->SetType(textureType);						\
			material->SetTexture(texture);								\
		}																\
	}																	\
}																		\

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

void Properties::Clear()
{
	EditorHelper::SetCharArray(&g_transPosX[0], 0);
	EditorHelper::SetCharArray(&g_transPosY[0], 0);
	EditorHelper::SetCharArray(&g_transPosZ[0], 0);
	EditorHelper::SetCharArray(&g_transRotX[0], 0);
	EditorHelper::SetCharArray(&g_transRotY[0], 0);
	EditorHelper::SetCharArray(&g_transRotZ[0], 0);
	EditorHelper::SetCharArray(&g_transScaX[0], 0);
	EditorHelper::SetCharArray(&g_transScaY[0], 0);
	EditorHelper::SetCharArray(&g_transScaZ[0], 0);
	g_materialRoughness = 0.0f;
	g_materialMetallic = 0.0f;
	g_materialNormal = 0.0f;
	g_materialHeight = 0.0f;
}

void Properties::Update()
{
	auto gameObject = Hierarchy::GetSelectedGameObject();
	if (gameObject.expired())
		return;
	
	auto gameObjectPtr = gameObject.lock().get();

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
	ShowScript(script);

	ImGui::Button("Add Component");

	ImGui::PopItemWidth();
}

void Properties::ShowTransform(Transform* transform)
{
	// REFLECT
	EditorHelper::SetCharArray(&g_transPosX[0], transform->GetPosition().x);
	EditorHelper::SetCharArray(&g_transPosY[0], transform->GetPosition().y);
	EditorHelper::SetCharArray(&g_transPosZ[0], transform->GetPosition().z);
	EditorHelper::SetCharArray(&g_transRotX[0], transform->GetRotation().ToEulerAngles().x);
	EditorHelper::SetCharArray(&g_transRotY[0], transform->GetRotation().ToEulerAngles().y);
	EditorHelper::SetCharArray(&g_transRotZ[0], transform->GetRotation().ToEulerAngles().z);
	EditorHelper::SetCharArray(&g_transScaX[0], transform->GetScale().x);
	EditorHelper::SetCharArray(&g_transScaY[0], transform->GetScale().y);
	EditorHelper::SetCharArray(&g_transScaZ[0], transform->GetScale().z);

	auto inputTextFlags = ImGuiInputTextFlags_CharsDecimal;
		
	COMPONENT_BEGIN_NO_OPTIONS("Transform", Icon_Component_Transform);
	{
		float posX = 90.0f;

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

	// MAP
	if (!EditorHelper::GetEngineUpdate())
	{
		transform->SetPosition(Vector3(
			(float)atof(&g_transPosX[0]),
			(float)atof(&g_transPosY[0]),
			(float)atof(&g_transPosZ[0])
		));

		transform->SetRotation(Quaternion::FromEulerAngles(
			(float)atof(&g_transRotX[0]),
			(float)atof(&g_transRotY[0]),
			(float)atof(&g_transRotZ[0])
		));

		transform->SetScale(Vector3(
			(float)atof(&g_transScaX[0]),
			(float)atof(&g_transScaY[0]),
			(float)atof(&g_transScaZ[0])
		));
	}
}

void Properties::ShowLight(Light* light)
{
	if (!light)
		return;

	// REFLECT
	g_lightTypeInt = (int)light->GetLightType();
	g_lightType = g_lightTypes[g_lightTypeInt];
	g_lightIntensity = light->GetIntensity();
	g_lightShadows = light->GetShadowQuality() != No_Shadows;
	g_lightAngle = light->GetAngle();
	g_lightButtonColorPicker->SetColor(light->GetColor());
	EditorHelper::SetCharArray(&g_lightRange[0], light->GetRange());

	float posX = 105.0f;

	COMPONENT_BEGIN("Light", Icon_Component_Light, light);
	{
		// Type
		ImGui::Text("Type");
		ImGui::SameLine(posX); if (ImGui::BeginCombo("##LightType", g_lightType))
		{
			for (int i = 0; i < IM_ARRAYSIZE(g_lightTypes); i++)
			{
				bool is_selected = (g_lightType == g_lightTypes[i]);
				if (ImGui::Selectable(g_lightTypes[i], is_selected))
				{
					g_lightType = g_lightTypes[i];
					g_lightTypeInt = i;
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
		ImGui::SameLine(posX); ImGui::SliderFloat("##lightIntensity", &g_lightIntensity, 0.0f, 10.0f);

		// Cast shadows
		ImGui::Text("Shadows");
		ImGui::SameLine(posX); ImGui::Checkbox("##lightShadows", &g_lightShadows);

		// Range
		if (g_lightTypeInt != (int)LightType_Directional)
		{
			ImGui::Text("Range");
			ImGui::SameLine(posX); ImGui::InputText("##lightRange", g_lightRange, BUFFER_TEXT_DEFAULT, ImGuiInputTextFlags_CharsDecimal);
		}

		// Angle
		if (g_lightTypeInt == (int)LightType_Spot)
		{
			ImGui::Text("Angle");
			ImGui::SameLine(posX); ImGui::SliderFloat("##lightAngle", &g_lightAngle, 1.0f, 179.0f);
		}
	}
	COMPONENT_END;

	// MAP
	light->SetLightType((LightType)g_lightTypeInt);
	light->SetColor(g_lightButtonColorPicker->GetColor());
	light->SetIntensity(g_lightIntensity);
	light->SetShadowQuality(g_lightShadows ? Hard_Shadows : No_Shadows);
	light->SetRange((float)atof(&g_lightRange[0]));
	light->SetAngle(g_lightAngle);
}

void Properties::ShowMeshFilter(MeshFilter* meshFilter)
{
	if (!meshFilter)
		return;

	// REFLECT
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

	// MAP
	auto material = meshRenderer->GetMaterial().lock();
	g_meshRendererCastShadows = meshRenderer->GetCastShadows();
	g_meshRendererReceiveShadows = meshRenderer->GetReceiveShadows();
	string materialName = material ? material->GetResourceName() : NOT_ASSIGNED;

	float posX = 150.0f;

	COMPONENT_BEGIN("Mesh Renderer", Icon_Component_MeshRenderer, meshRenderer);
	{
		// Cast shadows
		ImGui::Text("Cast Shadows");
		ImGui::SameLine(posX); ImGui::Checkbox("##MeshRendererCast", &g_meshRendererCastShadows);

		// Receive shadows
		ImGui::Text("Receive Shadows");
		ImGui::SameLine(posX); ImGui::Checkbox("##MeshRendererReceived", &g_meshRendererReceiveShadows);

		// Material
		ImGui::Text("Material");
		ImGui::SameLine(posX); ImGui::Text(materialName.c_str());
	}
	COMPONENT_END;

	// REFLECT
	meshRenderer->SetCastShadows(g_meshRendererCastShadows);
	meshRenderer->SetReceiveShadows(g_meshRendererReceiveShadows);
}

void Properties::ShowRigidBody(RigidBody* rigidBody)
{
	if (!rigidBody)
		return;

	// REFLECT
	EditorHelper::SetCharArray(&g_rigidBodyMass[0], rigidBody->GetMass());
	EditorHelper::SetCharArray(&g_rigidBodyFriction[0], rigidBody->GetFriction());
	EditorHelper::SetCharArray(&g_rigidBodyFrictionRolling[0], rigidBody->GetFrictionRolling());
	EditorHelper::SetCharArray(&g_rigidBodyRestitution[0], rigidBody->GetRestitution());
	g_rigidBodyUseGravity = rigidBody->GetUseGravity();
	g_rigidBodyIsKinematic = rigidBody->GetIsKinematic();
	g_rigidBodyFreezePosX = (bool)rigidBody->GetPositionLock().x;
	g_rigidBodyFreezePosY = (bool)rigidBody->GetPositionLock().y;
	g_rigidBodyFreezePosZ = (bool)rigidBody->GetPositionLock().z;
	g_rigidBodyFreezeRotX = (bool)rigidBody->GetRotationLock().x;
	g_rigidBodyFreezeRotY = (bool)rigidBody->GetRotationLock().y;
	g_rigidBodyFreezeRotZ = (bool)rigidBody->GetRotationLock().z;

	float posX = 150.0f;
	auto inputTextFlags = ImGuiInputTextFlags_CharsDecimal;

	COMPONENT_BEGIN("RigidBody", Icon_Component_RigidBody, rigidBody);
	{
		// Mass
		ImGui::Text("Mass");
		ImGui::SameLine(posX); ImGui::InputText("##RigidBodyMass", g_rigidBodyMass, BUFFER_TEXT_DEFAULT, inputTextFlags);

		// Friction
		ImGui::Text("Friction");
		ImGui::SameLine(posX); ImGui::InputText("##RigidBodyFriction", g_rigidBodyFriction, BUFFER_TEXT_DEFAULT, inputTextFlags);

		// Rolling Friction
		ImGui::Text("Rolling Friction");
		ImGui::SameLine(posX); ImGui::InputText("##RigidBodyRollingFriction", g_rigidBodyFrictionRolling, BUFFER_TEXT_DEFAULT, inputTextFlags);

		// Restitution
		ImGui::Text("Restitution");
		ImGui::SameLine(posX); ImGui::InputText("##RigidBodyRestitution", g_rigidBodyRestitution, BUFFER_TEXT_DEFAULT, inputTextFlags);

		// Use Gravity
		ImGui::Text("Use Gravity");
		ImGui::SameLine(posX); ImGui::Checkbox("##RigidBodyUseGravity", &g_rigidBodyUseGravity);

		// Is Kinematic
		ImGui::Text("Is Kinematic");
		ImGui::SameLine(posX); ImGui::Checkbox("##RigidBodyRestitution", &g_rigidBodyIsKinematic);

		// Freeze Position
		ImGui::Text("Freeze Position");
		ImGui::SameLine(posX); ImGui::Text("X");
		ImGui::SameLine(); ImGui::Checkbox("##RigidFreezePosX", &g_rigidBodyFreezePosX);
		ImGui::SameLine(); ImGui::Text("Y");
		ImGui::SameLine(); ImGui::Checkbox("##RigidFreezePosY", &g_rigidBodyFreezePosY);
		ImGui::SameLine(); ImGui::Text("Z");
		ImGui::SameLine(); ImGui::Checkbox("##RigidFreezePosZ", &g_rigidBodyFreezePosZ);

		// Freeze Rotation
		ImGui::Text("Freeze Rotation");
		ImGui::SameLine(posX); ImGui::Text("X");
		ImGui::SameLine(); ImGui::Checkbox("##RigidFreezeRotX", &g_rigidBodyFreezeRotX);
		ImGui::SameLine(); ImGui::Text("Y");
		ImGui::SameLine(); ImGui::Checkbox("##RigidFreezeRotY", &g_rigidBodyFreezeRotY);
		ImGui::SameLine(); ImGui::Text("Z");
		ImGui::SameLine(); ImGui::Checkbox("##RigidFreezeRotZ", &g_rigidBodyFreezeRotZ);
	}
	COMPONENT_END;

	// MAP
	rigidBody->SetMass((float)atof(&g_rigidBodyMass[0]));
	rigidBody->SetFriction((float)atof(&g_rigidBodyFriction[0]));
	rigidBody->SetFrictionRolling((float)atof(&g_rigidBodyFrictionRolling[0]));
	rigidBody->SetRestitution((float)atof(&g_rigidBodyRestitution[0]));
	rigidBody->SetUseGravity(g_rigidBodyUseGravity);
	rigidBody->SetIsKinematic(g_rigidBodyIsKinematic);
	rigidBody->SetPositionLock(Vector3(
		(float)g_rigidBodyFreezePosX,
		(float)g_rigidBodyFreezePosY,
		(float)g_rigidBodyFreezePosZ
	));
	rigidBody->SetRotationLock(Vector3(
		(float)g_rigidBodyFreezeRotX,
		(float)g_rigidBodyFreezeRotY,
		(float)g_rigidBodyFreezeRotZ
	));
}

void Properties::ShowCollider(Collider* collider)
{
	if (!collider)
		return;

	// REFLECT
	g_colShapeInt = (int)collider->GetShapeType();
	g_colShape = g_colShapes[g_colShapeInt];
	EditorHelper::SetCharArray(&g_colCenterX[0], collider->GetCenter().x);
	EditorHelper::SetCharArray(&g_colCenterY[0], collider->GetCenter().y);
	EditorHelper::SetCharArray(&g_colCenterZ[0], collider->GetCenter().z);
	EditorHelper::SetCharArray(&g_colSizeX[0], collider->GetBoundingBox().x);
	EditorHelper::SetCharArray(&g_colSizeY[0], collider->GetBoundingBox().y);
	EditorHelper::SetCharArray(&g_colSizeZ[0], collider->GetBoundingBox().z);
	g_colOptimize = collider->GetOptimize();

	float posX = 90.0f;
	auto inputTextFlags = ImGuiInputTextFlags_CharsDecimal;

	COMPONENT_BEGIN("Collider", Icon_Component_Collider, collider);
	{
		// Type
		ImGui::Text("Type");
		ImGui::SameLine(posX); if (ImGui::BeginCombo("##colliderType", g_colShape))
		{
			for (int i = 0; i < IM_ARRAYSIZE(g_colShapes); i++)
			{
				bool is_selected = (g_colShape == g_colShapes[i]);
				if (ImGui::Selectable(g_colShapes[i], is_selected))
				{
					g_colShape = g_colShapes[i];
					g_colShapeInt = i;
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
		ImGui::SameLine(); ImGui::InputText("##colliderCenterX", g_colCenterX, BUFFER_TEXT_DEFAULT, inputTextFlags);
		ImGui::SameLine(); ImGui::Text("Y");
		ImGui::SameLine(); ImGui::InputText("##colliderCenterY", g_colCenterY, BUFFER_TEXT_DEFAULT, inputTextFlags);
		ImGui::SameLine(); ImGui::Text("Z");
		ImGui::SameLine(); ImGui::InputText("##colliderCenterZ", g_colCenterZ, BUFFER_TEXT_DEFAULT, inputTextFlags);

		// Size
		ImGui::Text("Size");
		ImGui::SameLine(posX); ImGui::Text("X");
		ImGui::SameLine(); ImGui::InputText("##colliderSizeX", g_colSizeX, BUFFER_TEXT_DEFAULT, inputTextFlags);
		ImGui::SameLine(); ImGui::Text("Y");
		ImGui::SameLine(); ImGui::InputText("##colliderSizeY", g_colSizeY, BUFFER_TEXT_DEFAULT, inputTextFlags);
		ImGui::SameLine(); ImGui::Text("Z");
		ImGui::SameLine(); ImGui::InputText("##colliderSizeZ", g_colSizeZ, BUFFER_TEXT_DEFAULT, inputTextFlags);

		// Optimize
		if (g_colShapeInt == (int)ColliderShape_Mesh)
		{
			ImGui::Text("Optimize");
			ImGui::SameLine(posX); ImGui::Checkbox("##colliderOptimize", &g_colOptimize);
		}
	}
	COMPONENT_END;

	// MAP
	collider->SetShapeType((ColliderShape)g_colShapeInt);
	collider->SetCenter(Vector3(
		(float)atof(&g_colCenterX[0]),
		(float)atof(&g_colCenterY[0]),
		(float)atof(&g_colCenterZ[0])
	));

	collider->SetBoundingBox(Vector3(
		(float)atof(&g_colSizeX[0]),
		(float)atof(&g_colSizeY[0]),
		(float)atof(&g_colSizeZ[0])
	));
	collider->SetOptimize(g_colOptimize);
}

void Properties::ShowMaterial(Material* material)
{
	if (!material)
		return;

	// REFLECT
	auto texAlbedo		= material->GetTextureByType(TextureType_Albedo).lock();
	auto texRoughness	= material->GetTextureByType(TextureType_Roughness).lock();
	auto texMetallic	= material->GetTextureByType(TextureType_Metallic).lock();
	auto texNormal		= material->GetTextureByType(TextureType_Normal).lock();
	auto texHeight		= material->GetTextureByType(TextureType_Height).lock();
	auto texOcclusion	= material->GetTextureByType(TextureType_Occlusion).lock();
	auto texMask		= material->GetTextureByType(TextureType_Mask).lock();
	g_materialRoughness = material->GetRoughnessMultiplier();
	g_materialMetallic	= material->GetMetallicMultiplier();
	g_materialNormal	= material->GetNormalMultiplier();
	g_materialHeight	= material->GetHeightMultiplier();
	g_materialButtonColorPicker->SetColor(material->GetColorAlbedo());
	EditorHelper::SetCharArray(&g_matTilingX[0], material->GetTiling().x);
	EditorHelper::SetCharArray(&g_matTilingY[0], material->GetTiling().y);
	EditorHelper::SetCharArray(&g_matOffsetX[0], material->GetOffset().x);
	EditorHelper::SetCharArray(&g_matOffsetY[0], material->GetOffset().y);

	float posX = 100.0f;

	COMPONENT_BEGIN_NO_OPTIONS("Material", Icon_Component_Material);
	{
		// Name
		ImGui::Text("Name");
		ImGui::SameLine(posX); ImGui::Text(material->GetResourceName().c_str());

		// Albedo
		ImGui::Text("Abedo");
		ImGui::SameLine(posX); ImGui::Image(
			texAlbedo ? texAlbedo->GetShaderResource() : nullptr,
			g_materialTexSize,
			ImVec2(0, 0),
			ImVec2(1, 1),
			ImColor(255, 255, 255, 255),
			ImColor(255, 255, 255, 128)
		);
		DROP_TARGET_TEXTURE(TextureType_Albedo);
		ImGui::SameLine(); 	g_materialButtonColorPicker->Update();

		// Roughness
		ImGui::Text("Roughness");
		ImGui::SameLine(posX); ImGui::Image(
			texRoughness ? texRoughness->GetShaderResource() : nullptr,
			g_materialTexSize,
			ImVec2(0, 0),
			ImVec2(1, 1),
			ImColor(255, 255, 255, 255),
			ImColor(255, 255, 255, 128)
		);
		DROP_TARGET_TEXTURE(TextureType_Roughness);
		ImGui::SameLine(); ImGui::SliderFloat("##matRoughness", &g_materialRoughness, 0.0f, 1.0f);

		// Metallic
		ImGui::Text("Metallic");
		ImGui::SameLine(posX); ImGui::Image(
			texMetallic ? texMetallic->GetShaderResource() : nullptr,
			g_materialTexSize,
			ImVec2(0, 0),
			ImVec2(1, 1),
			ImColor(255, 255, 255, 255),
			ImColor(255, 255, 255, 128)
		);
		DROP_TARGET_TEXTURE(TextureType_Metallic);
		ImGui::SameLine(); ImGui::SliderFloat("##matMetallic", &g_materialMetallic, 0.0f, 1.0f);

		// Normal
		ImGui::Text("Normal");
		ImGui::SameLine(posX); ImGui::Image(
			texNormal ? texNormal->GetShaderResource() : nullptr,
			g_materialTexSize,
			ImVec2(0, 0),
			ImVec2(1, 1),
			ImColor(255, 255, 255, 255),
			ImColor(255, 255, 255, 128)
		);
		DROP_TARGET_TEXTURE(TextureType_Normal);
		ImGui::SameLine(); ImGui::SliderFloat("##matNormal", &g_materialNormal, 0.0f, 1.0f);

		// Height
		ImGui::Text("Height");
		ImGui::SameLine(posX); ImGui::Image(
			texHeight ? texHeight->GetShaderResource() : nullptr,
			g_materialTexSize,
			ImVec2(0, 0),
			ImVec2(1, 1),
			ImColor(255, 255, 255, 255),
			ImColor(255, 255, 255, 128)
		);
		DROP_TARGET_TEXTURE(TextureType_Height);
		ImGui::SameLine(); ImGui::SliderFloat("##matHeight", &g_materialHeight, 0.0f, 1.0f);

		// Occlusion
		ImGui::Text("Occlusion");
		ImGui::SameLine(posX); ImGui::Image(
			texOcclusion ? texOcclusion->GetShaderResource() : nullptr,
			g_materialTexSize,
			ImVec2(0, 0),
			ImVec2(1, 1),
			ImColor(255, 255, 255, 255),
			ImColor(255, 255, 255, 128)
		);
		DROP_TARGET_TEXTURE(TextureType_Occlusion);

		// Mask
		ImGui::Text("Mask");
		ImGui::SameLine(posX); ImGui::Image(
			texMask ? texMask->GetShaderResource() : nullptr,
			g_materialTexSize,
			ImVec2(0, 0),
			ImVec2(1, 1),
			ImColor(255, 255, 255, 255),
			ImColor(255, 255, 255, 128)
		);
		DROP_TARGET_TEXTURE(TextureType_Mask);

		// Tiling
		ImGui::Text("Tiling");
		ImGui::SameLine(posX); ImGui::Text("X"); ImGui::SameLine(); ImGui::InputText("##matTilingX", g_matTilingX, BUFFER_TEXT_DEFAULT, ImGuiInputTextFlags_CharsDecimal);
		ImGui::SameLine(); ImGui::Text("Y"); ImGui::SameLine(); ImGui::InputText("##matTilingY", g_matTilingY, BUFFER_TEXT_DEFAULT, ImGuiInputTextFlags_CharsDecimal);

		// Offset
		ImGui::Text("Offset");
		ImGui::SameLine(posX); ImGui::Text("X"); ImGui::SameLine(); ImGui::InputText("##matOffsetX", g_matOffsetX, BUFFER_TEXT_DEFAULT, ImGuiInputTextFlags_CharsDecimal);
		ImGui::SameLine(); ImGui::Text("Y"); ImGui::SameLine(); ImGui::InputText("##matOffsetY", g_matOffsetY, BUFFER_TEXT_DEFAULT, ImGuiInputTextFlags_CharsDecimal);
	}
	COMPONENT_END;

	// MAP
	material->SetColorAlbedo(g_materialButtonColorPicker->GetColor());
	material->SetRoughnessMultiplier(g_materialRoughness);
	material->SetMetallicMultiplier(g_materialMetallic);
	material->SetNormalMultiplier(g_materialNormal);
	material->SetHeightMultiplier(g_materialHeight);
	material->SetTiling(Vector2((float)atof(&g_matTilingX[0]), (float)atof(&g_matTilingY[0])));
	material->SetOffset(Vector2((float)atof(&g_matOffsetX[0]), (float)atof(&g_matOffsetY[0])));
}

void Properties::ShowCamera(Camera* camera)
{
	if (!camera)
		return;

	// REFLECT
	g_cameraButtonColorPicker->SetColor(camera->GetClearColor());
	g_cameraProjectionInt	= (int)camera->GetProjection();
	g_cameraProjection		= g_cameraProjections[g_cameraProjectionInt];
	g_cameraFOV				= camera->GetFOV_Horizontal_Deg();
	EditorHelper::SetCharArray(&g_cameraNear[0], camera->GetNearPlane());
	EditorHelper::SetCharArray(&g_cameraFar[0], camera->GetFarPlane());

	auto inputTextFlags = ImGuiInputTextFlags_CharsDecimal;
	float posX = 150.0f;

	COMPONENT_BEGIN("Camera", Icon_Component_Camera, camera);
	{
		// Background
		ImGui::Text("Background");
		ImGui::SameLine(posX); g_cameraButtonColorPicker->Update();

		// Projection
		ImGui::Text("Projection");
		ImGui::SameLine(posX); if (ImGui::BeginCombo("##cameraProjection", g_cameraProjection))
		{
			for (int i = 0; i < IM_ARRAYSIZE(g_cameraProjections); i++)
			{
				bool is_selected = (g_cameraProjection == g_cameraProjections[i]);
				if (ImGui::Selectable(g_cameraProjections[i], is_selected))
				{
					g_cameraProjection = g_cameraProjections[i];
					g_cameraProjectionInt = i;
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
		ImGui::SameLine(posX); ImGui::SliderFloat("##cameraFOV", &g_cameraFOV, 1.0f, 179.0f);

		// Clipping Planes
		ImGui::Text("Clipping Planes");
		ImGui::SameLine(posX);		ImGui::Text("Near");	ImGui::SameLine(); ImGui::InputText("##cameraNear", g_cameraNear, BUFFER_TEXT_DEFAULT, inputTextFlags);
		ImGui::SetCursorPosX(posX); ImGui::Text("Far");		ImGui::SameLine(); ImGui::InputText("##cameraFar", g_cameraFar, BUFFER_TEXT_DEFAULT, inputTextFlags);
	}
	COMPONENT_END;

	// MAP
	camera->SetClearColor(g_cameraButtonColorPicker->GetColor());
	camera->SetProjection((ProjectionType)g_cameraProjectionInt);
	camera->SetFOV_Horizontal_Deg(g_cameraFOV);
	camera->SetNearPlane((float)atof(&g_cameraNear[0]));
	camera->SetFarPlane((float)atof(&g_cameraFar[0]));
}

void Properties::ShowAudioSource(AudioSource* audioSource)
{
	if (!audioSource)
		return;

	COMPONENT_BEGIN("Audio Source", Icon_Component_AudioSource, audioSource);
	{

	}
	COMPONENT_END;
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

void Properties::Component_ContextMenu(Component* component)
{
	if (!component)
		return;

	if (ImGui::BeginPopup("##ComponentContextMenu"))
	{
		if (ImGui::MenuItem("Remove"))
		{
			auto gameObject = Hierarchy::GetSelectedGameObject().lock();
			if (gameObject)
			{
				gameObject->RemoveComponentByID(component->GetID());
			}
		}

		ImGui::EndPopup();
	}
}