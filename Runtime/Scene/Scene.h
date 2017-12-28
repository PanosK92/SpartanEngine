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

#pragma once

//= INCLUDES ======================
#include <vector>
#include "../Math/Vector3.h"
#include "../Threading/Threading.h"
//=================================

namespace Directus
{
	class GameObject;
	class Light;

	class ENGINE_API Scene : public Subsystem
	{
	public:
		Scene(Context* context);
		~Scene();

		//= Subsystem ============
		bool Initialize() override;
		//========================

		void Start();
		void OnDisable();
		void Update();
		void Clear();

		//= IO =============================================
		void SaveToFileAsync(const std::string& filePath);
		void LoadFromFileAsync(const std::string& filePath);
		bool SaveToFile(const std::string& filePath);
		bool LoadFromFile(const std::string& filePath);

		//= GAMEOBJECT HELPER FUNCTIONS ===============================================
		std::weak_ptr<GameObject> CreateGameObject();
		int GetGameObjectCount() { return (int)m_gameObjects.size(); }
		const std::vector<std::shared_ptr<GameObject>>& GetAllGameObjects() { return m_gameObjects; }
		std::vector<std::weak_ptr<GameObject>> GetRootGameObjects();
		std::weak_ptr<GameObject> GetGameObjectRoot(std::weak_ptr<GameObject> gameObject);
		std::weak_ptr<GameObject> GetGameObjectByName(const std::string& name);
		std::weak_ptr<GameObject> GetGameObjectByID(unsigned int ID);
		bool GameObjectExists(std::weak_ptr<GameObject> gameObject);
		void RemoveGameObject(std::weak_ptr<GameObject> gameObject);
		void RemoveSingleGameObject(std::weak_ptr<GameObject> gameObject);
		std::weak_ptr<GameObject> GetWeakReferenceToGameObject(GameObject* gameObject);

		//= SCENE RESOLUTION  ==============================
		void Resolve();
		const std::vector<std::weak_ptr<GameObject>>& GetRenderables() { return m_renderables; }
		std::weak_ptr<GameObject> GetMainCamera() { return m_mainCamera; }

		//= MISC =======================================
		void SetAmbientLight(float x, float y, float z);
		Math::Vector3 GetAmbientLight();

		//= STATS ==============================================
		float GetFPS() { return m_fps; }
		const std::string& GetStatus() { return m_status; }
		float GetPercentage() { return m_jobsDone / m_jobsTotal; }
		bool IsLoading() { return m_isLoading; }

	private:
		//= COMMON GAMEOBJECT CREATION ====================
		std::weak_ptr<GameObject> CreateSkybox();
		std::weak_ptr<GameObject> CreateCamera();
		std::weak_ptr<GameObject> CreateDirectionalLight();
		//=================================================

		//= HELPER FUNCTIONS ====
		void ResetLoadingStats();
		void CalculateFPS();
		//=======================

		std::vector<std::shared_ptr<GameObject>> m_gameObjects;
		std::vector<std::weak_ptr<GameObject>> m_renderables;

		std::weak_ptr<GameObject> m_mainCamera;
		std::weak_ptr<GameObject> m_skybox;
		Math::Vector3 m_ambientLight;

		//= STATS =========
		float m_fps;
		float m_timePassed;
		int m_frameCount;
		std::string m_status;
		float m_jobsDone;
		float m_jobsTotal;
		bool m_isLoading;
		//=================
	};
}