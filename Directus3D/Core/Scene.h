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

#pragma once

//= INCLUDES ============================
#include <vector>
#include "GameObject.h"
#include "../Math/Vector2.h"
#include "../Math/Vector3.h"
#include "../Multithreading/ThreadPool.h"
//=======================================

class DllExport Scene : public Subsystem
{
public:
	Scene(Context* context);
	~Scene();

	void Initialize();
	void Start();
	void OnDisable();
	void Update();
	void Clear();

	//= IO ========================================================================
	void SaveToFileAsync(const std::string& filePath);
	void LoadFromFileAsync(const std::string& filePath);
	bool SaveToFile(const std::string& filePath);
	bool LoadFromFile(const std::string& filePath);

	//= GAMEOBJECT HELPER FUNCTIONS ===============================================
	GameObject* CreateGameObject();
	int GetGameObjectCount() { return (int)m_gameObjects.size(); }
	std::vector<GameObject*> GetAllGameObjects() { return m_gameObjects; }
	std::vector<GameObject*> GetRootGameObjects();
	GameObject* GetGameObjectRoot(GameObject* gameObject);
	GameObject* GetGameObjectByName(const std::string& name);
	GameObject* GetGameObjectByID(const std::string& ID);
	bool GameObjectExists(GameObject* gameObject);
	void RemoveGameObject(GameObject* gameObject);
	void RemoveSingleGameObject(GameObject* gameObject);

	//= SCENE RESOLUTION  =========================================================
	void Resolve();
	std::vector<GameObject*> GetRenderables() { return m_renderables; }
	std::vector<GameObject*> GetLightsDirectional() { return m_lightsDirectional; }
	std::vector<GameObject*> GetLightsPoint() { return m_lightsPoint; }
	GameObject* GetSkybox() { return m_skybox; }
	GameObject* GetMainCamera() { return m_mainCamera; }

	//= MISC ======================================================================
	void SetAmbientLight(float x, float y, float z);
	Directus::Math::Vector3 GetAmbientLight();
	GameObject* MousePick(Directus::Math::Vector2& mousePos);
	bool RaySphereIntersect(const Directus::Math::Vector3& rayOrigin, const Directus::Math::Vector3& rayDirection, float radius);

private:
	//= COMMON GAMEOBJECT CREATION ======
	GameObject* CreateSkybox();
	GameObject* CreateCamera();
	GameObject* CreateDirectionalLight();
	//===================================

	std::vector<GameObject*> m_gameObjects;
	std::vector<GameObject*> m_renderables;
	std::vector<GameObject*> m_lightsDirectional;
	std::vector<GameObject*> m_lightsPoint;
	GameObject* m_mainCamera;
	GameObject* m_skybox;
	Directus::Math::Vector3 m_ambientLight;
};
