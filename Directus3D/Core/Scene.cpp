/*
Copyright(c) 2016 Panos Karabelas

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

//= INCLUDES ==========================
#include "Scene.h"
#include <complex>
#include "../IO/Serializer.h"
#include "../FileSystem/FileSystem.h"
#include "../Logging/Log.h"
#include "../Graphics//Renderer.h"
#include "../Components/Transform.h"
#include "../Components/MeshRenderer.h"
#include "../Components/Camera.h"
#include "../Components/LineRenderer.h"
#include "../Components/Skybox.h"
#include "../Components/Script.h"
#include "../Components/MeshFilter.h"
#include "../Physics/PhysicsWorld.h"
#include "../Events/EventHandler.h"
#include "../Core/Context.h"
#include "Settings.h"
#include "../Resource/ResourceCache.h"
//=====================================

//= NAMESPACES ====================
using namespace std;
using namespace Directus::Math;
using namespace Directus::Resource;
//=================================

Scene::Scene(Context* context) : Subsystem(context)
{
	m_ambientLight = Vector3::Zero;
	m_mainCamera = nullptr;
	m_skybox = nullptr;
}

Scene::~Scene()
{
	Clear();
}

void Scene::Initialize()
{
	m_mainCamera = CreateCamera();
	CreateSkybox();
	CreateDirectionalLight();
	Resolve();

	// Subcribe to render events
	SUBSCRIBE_TO_EVENT(RESOLVE_HIERARCHY, std::bind(&Scene::Resolve, this));
	SUBSCRIBE_TO_EVENT(EVENT_ENGINE_START, std::bind(&Scene::Start, this));
	SUBSCRIBE_TO_EVENT(RENDER_UPDATE, std::bind(&Scene::Update, this));
}

void Scene::Start()
{
	for (const auto& gameObject : m_gameObjects)
		gameObject->Start();
}

void Scene::Update()
{
	for (const auto& gameObject : m_gameObjects)
		gameObject->Update();
}

void Scene::Clear()
{
	for (auto& gameObject : m_gameObjects)
		delete gameObject;

	m_gameObjects.clear();
	m_gameObjects.shrink_to_fit();

	m_renderables.clear();
	m_renderables.shrink_to_fit();

	m_lightsDirectional.clear();
	m_lightsDirectional.shrink_to_fit();

	m_lightsPoint.clear();
	m_lightsPoint.shrink_to_fit();

	// dodge dangling pointers
	m_mainCamera = nullptr;
	m_skybox = nullptr;

	// Clear the resource cache
	g_context->GetSubsystem<ResourceCache>()->Clear();

	g_context->GetSubsystem<ScriptEngine>()->Reset();
	g_context->GetSubsystem<PhysicsWorld>()->Reset();
	g_context->GetSubsystem<Renderer>()->Clear();

	CreateSkybox();
}
//=========================================================================================================

//= I/O ===================================================================================================
void Scene::SaveToFileAsync(const string& filePath)
{
	g_context->GetSubsystem<ThreadPool>()->AddTask(std::bind(&Scene::SaveToFile, this, filePath));
}

void Scene::LoadFromFileAsync(const string& filePath)
{
	g_context->GetSubsystem<ThreadPool>()->AddTask(std::bind(&Scene::LoadFromFile, this, filePath));
}

bool Scene::SaveToFile(const string& filePathIn)
{
	string filePath = filePathIn;

	// Add scene file extension to the filepath if it's missing
	if (FileSystem::GetExtensionFromPath(filePath) != SCENE_EXTENSION)
		filePath += SCENE_EXTENSION;

	// Save any in-memory changes done to resources while running.
	g_context->GetSubsystem<ResourceCache>()->SaveResourceMetadata();

	Serializer::StartWriting(filePath);

	// Save the paths of all the currently loaded resources
	vector<string> resourcePaths = g_context->GetSubsystem<ResourceCache>()->GetResourceFilePaths();
	Serializer::WriteVectorSTR(resourcePaths);			// 1st - Resource paths

	//= Save GameObjects ===========================
	Serializer::WriteInt((int)m_gameObjects.size());	// 2nd - GameObject count
	
	for (const auto& gameObject : m_gameObjects)		// 3rd - GameObject IDs
		Serializer::WriteSTR(gameObject->GetID()); 

	for (const auto& gameObject : m_gameObjects)		// 4th - GameObjects
		gameObject->Serialize();
	//==============================================

	Serializer::StopWriting();

	return true;
}

bool Scene::LoadFromFile(const string& filePath)
{
	if (!FileSystem::FileExists(filePath))
	{
		LOG_ERROR(filePath + " was not found.");
		return false;
	}

	Clear();

	// Read all the resource file paths
	Serializer::StartReading(filePath);
	vector<string> resourcePaths = Serializer::ReadVectorSTR();
	Serializer::StopReading();

	// Load all all these resources
	for (const auto& resourcePath : resourcePaths)
	{
		if (FileSystem::IsSupportedMeshFile(resourcePath))
		{
			g_context->GetSubsystem<ResourceCache>()->LoadResource<Mesh>(resourcePath);
			continue;
		}

		if (FileSystem::IsSupportedMaterialFile(resourcePath))
		{
			g_context->GetSubsystem<ResourceCache>()->LoadResource<Material>(resourcePath);
			continue;
		}

		if (FileSystem::IsSupportedImageFile(resourcePath))
			g_context->GetSubsystem<ResourceCache>()->LoadResource<Texture>(resourcePath);
	}
		
	//= Load GameObjects ============================
	Serializer::StartReading(filePath);

	// 1st - Read the resource paths
	Serializer::ReadVectorSTR();

	// 2nd - GameObject count
	int gameObjectCount = Serializer::ReadInt();

	// 3rd - GameObject IDs
	for (int i = 0; i < gameObjectCount; i++)
	{
		GameObject* gameObject = CreateGameObject();
		gameObject->SetID(Serializer::ReadSTR());
	}

	// 4th - GameObjects
	for (const auto& gameObject : m_gameObjects)
		gameObject->Deserialize();

	Serializer::StopReading();
	//==============================================
	
	Resolve();

	return true;
}
//===================================================================================================

//= GAMEOBJECT HELPER FUNCTIONS  ====================================================================
GameObject* Scene::CreateGameObject()
{
	GameObject* gameObject = new GameObject(g_context);
	m_gameObjects.push_back(gameObject);
	gameObject->Initialize(); // also resolves scene

	return gameObject;
}

vector<GameObject*> Scene::GetRootGameObjects()
{
	vector<GameObject*> rootGameObjects;

	for (const auto& gameObject : m_gameObjects)
		if (gameObject->GetTransform()->IsRoot())
			rootGameObjects.push_back(gameObject);

	return rootGameObjects;
}

GameObject* Scene::GetGameObjectRoot(GameObject* gameObject)
{
	if (!gameObject)
		return nullptr;

	return gameObject->GetTransform()->GetRoot()->GetGameObject();
}

GameObject* Scene::GetGameObjectByName(const std::string& name)
{
	for (const auto& gameObject : m_gameObjects)
		if (gameObject->GetName() == name)
			return gameObject;

	return nullptr;
}

GameObject* Scene::GetGameObjectByID(const string& ID)
{
	for (const auto& gameObject : m_gameObjects)
		if (gameObject->GetID() == ID)
			return gameObject;

	return nullptr;
}

bool Scene::GameObjectExists(GameObject* gameObject)
{
	if (!gameObject)
		return false;

	return GetGameObjectByID(gameObject->GetID()) ? true : false;
}

// Removes a GameObject and all of it's children
void Scene::RemoveGameObject(GameObject* gameObject)
{
	if (!gameObject)
		return;

	// remove any descendants
	auto descendants = gameObject->GetTransform()->GetDescendants();
	for (const auto& descendant : descendants)
		RemoveSingleGameObject(descendant->GetGameObject());

	// remove this gameobject but keep it's parent
	Transform* parent = gameObject->GetTransform()->GetParent();
	RemoveSingleGameObject(gameObject);

	// if there is a parent, update it's children pool
	if (parent)
		parent->ResolveChildrenRecursively();
}

// Removes a GameObject but leaves the parent and the children as is
void Scene::RemoveSingleGameObject(GameObject* gameObject)
{
	if (!gameObject)
		return;

	for (auto it = m_gameObjects.begin(); it < m_gameObjects.end();)
	{
		GameObject* temp = *it;
		if (temp->GetID() == gameObject->GetID())
		{
			delete temp;
			it = m_gameObjects.erase(it);
			Resolve();
			return;
		}
		++it;
	}
}
//===================================================================================================

//= SCENE RESOLUTION  ===============================================================================
void Scene::Resolve()
{
	m_renderables.clear();
	m_renderables.shrink_to_fit();

	m_lightsDirectional.clear();
	m_lightsDirectional.shrink_to_fit();

	m_lightsPoint.clear();
	m_lightsPoint.shrink_to_fit();

	// It's necessery not to forget to set this to nullptr,
	// otherwise it can end up as a nice dangling pointer :-)
	m_mainCamera = nullptr;

	for (const auto& gameObject : m_gameObjects)
	{
		// Find camera
		if (gameObject->HasComponent<Camera>())
			m_mainCamera = gameObject;

		// Find skybox
		if (gameObject->HasComponent<Skybox>())
			m_skybox = gameObject;

		// Find renderables
		if (gameObject->HasComponent<MeshRenderer>() && gameObject->HasComponent<MeshFilter>())
			m_renderables.push_back(gameObject);

		// Find lights
		if (gameObject->HasComponent<Light>())
		{
			if (gameObject->GetComponent<Light>()->GetLightType() == Directional)
				m_lightsDirectional.push_back(gameObject);
			else if (gameObject->GetComponent<Light>()->GetLightType() == Point)
				m_lightsPoint.push_back(gameObject);
		}
	}
}
//===================================================================================================

//= MISC ============================================================================================
void Scene::SetAmbientLight(float x, float y, float z)
{
	m_ambientLight = Vector3(x, y, z);
}

Vector3 Scene::GetAmbientLight()
{
	return m_ambientLight;
}

GameObject* Scene::MousePick(Vector2& mousePos)
{
	Camera* camera = GetMainCamera()->GetComponent<Camera>();
	Matrix mViewProjectionInv = (camera->GetViewMatrix() * camera->GetProjectionMatrix()).Inverted();

	// Transform mouse coordinates from [0,1] to [-1,+1]
	mousePos.x = ((2.0f * mousePos.x) / (float)RESOLUTION_WIDTH - 1.0f);
	mousePos.y = (((2.0f * mousePos.y) / (float)RESOLUTION_HEIGHT) - 1.0f) * -1.0f;

	// Calculate the origin and the end of the ray
	Vector3 rayOrigin = Vector3(mousePos.x, mousePos.y, camera->GetNearPlane());
	Vector3 rayEnd = Vector3(mousePos.x, mousePos.y, camera->GetFarPlane());

	// Transform it from projection space to world space
	rayOrigin = Vector3::Transform(rayOrigin, mViewProjectionInv);
	rayEnd = Vector3::Transform(rayEnd, mViewProjectionInv);

	// Get the ray direction
	Vector3 rayDirection = (rayEnd - rayOrigin).Normalized();

	vector<GameObject*> intersectedGameObjects;
	//= Intersection test ===============================
	for (auto gameObject : m_renderables)
	{
		Vector3 extent = gameObject->GetComponent<MeshFilter>()->GetBoundingBox();

		float radius = max(abs(extent.x), abs(extent.y));
		radius = max(radius, abs(extent.z));

		if (RaySphereIntersect(rayOrigin, rayDirection, radius))
			intersectedGameObjects.push_back(gameObject);
	}
	//====================================================

	//= Find the gameobject closest to the camera ==
	float minDistance = 1000;
	GameObject* clostestGameObject = nullptr;
	for (auto gameObject : intersectedGameObjects)
	{
		Vector3 posA = gameObject->GetTransform()->GetPosition();
		Vector3 posB = camera->g_transform->GetPosition();

		float distance = sqrt(pow((posB.x - posA.x), 2.0f) + pow((posB.y - posA.y), 2.0f) + pow((posB.z - posA.z), 2.0f));

		if (distance < minDistance)
		{
			minDistance = distance;
			clostestGameObject = gameObject;
		}
	}
	//==============================================

	return clostestGameObject;
}

bool Scene::RaySphereIntersect(const Vector3& rayOrigin, const Vector3& rayDirection, float radius)
{
	// Calculate the a, b, and c coefficients.
	float a = (rayDirection.x * rayDirection.x) + (rayDirection.y * rayDirection.y) + (rayDirection.z * rayDirection.z);
	float b = ((rayDirection.x * rayOrigin.x) + (rayDirection.y * rayOrigin.y) + (rayDirection.z * rayOrigin.z)) * 2.0f;
	float c = ((rayOrigin.x * rayOrigin.x) + (rayOrigin.y * rayOrigin.y) + (rayOrigin.z * rayOrigin.z)) - (radius * radius);

	// Find the discriminant.
	float discriminant = pow(b, 2.0f) - (4 * a * c);

	// if discriminant is negative the picking ray 
	// missed the sphere, otherwise it intersected the sphere.
	return discriminant < 0.0f ? false : true;
}
//======================================================================================================

//= COMMON GAMEOBJECT CREATION =========================================================================
GameObject* Scene::CreateSkybox()
{
	GameObject* skybox = CreateGameObject();
	skybox->SetName("Skybox");
	skybox->AddComponent<LineRenderer>();
	skybox->AddComponent<Skybox>();
	//skybox->SetHierarchyVisibility(false);

	return skybox;
}

GameObject* Scene::CreateCamera()
{
	GameObject* camera = CreateGameObject();
	camera->SetName("Camera");
	camera->AddComponent<Camera>();
	camera->GetTransform()->SetPositionLocal(Vector3(0.0f, 1.0f, -5.0f));
	camera->AddComponent<Script>()->AddScript("Assets/Scripts/MouseLook.as");
	camera->AddComponent<Script>()->AddScript("Assets/Scripts/FirstPersonController.as");

	return camera;
}

GameObject* Scene::CreateDirectionalLight()
{
	GameObject* light = CreateGameObject();
	light->SetName("DirectionalLight");
	light->GetComponent<Transform>()->SetRotationLocal(Quaternion::FromEulerAngles(30.0f, 0.0, 0.0f));

	Light* lightComp = light->AddComponent<Light>();
	lightComp->SetLightType(Directional);
	lightComp->SetIntensity(4.0f);

	return light;
}
//======================================================================================================