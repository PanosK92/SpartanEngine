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

//= INCLUDES ================================
#include <vector>
#include "../Math/Vector3.h"
#include "../Multithreading/Multithreading.h"
//===========================================

namespace Directus
{
	class GameObject;
	typedef std::weak_ptr<GameObject> weakGameObj;
	typedef std::shared_ptr<GameObject> sharedGameObj;

	class DllExport Scene : public Subsystem
	{
	public:
		Scene(Context* context);
		~Scene();

		//= Subsystem ============
		virtual bool Initialize();
		//========================

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
		weakGameObj CreateGameObject();
		int GetGameObjectCount() { return (int)m_gameObjects.size(); }
		std::vector<weakGameObj> GetAllGameObjects();
		std::vector<weakGameObj> GetRootGameObjects();
		weakGameObj GetGameObjectRoot(weakGameObj gameObject);
		weakGameObj GetGameObjectByName(const std::string& name);
		weakGameObj GetGameObjectByID(const std::string& ID);
		bool GameObjectExists(weakGameObj gameObject);
		void RemoveGameObject(weakGameObj gameObject);
		void RemoveSingleGameObject(weakGameObj gameObject);

		//= SCENE RESOLUTION  =========================================================
		void Resolve();
		std::vector<weakGameObj> GetRenderables() { return m_renderables; }
		std::vector<weakGameObj> GetLightsDirectional() { return m_lightsDirectional; }
		std::vector<weakGameObj> GetLightsPoint() { return m_lightsPoint; }
		weakGameObj GetSkybox() { return m_skybox; }
		weakGameObj GetMainCamera() { return m_mainCamera; }

		//= MISC ======================================================================
		void SetAmbientLight(float x, float y, float z);
		Math::Vector3 GetAmbientLight();

		//= CALLED BY GAMEOBJECTS ==============
		void AddGameObject(GameObject* gameObj);
		//======================================

		//= STATS ======================	
		float GetFPS() { return m_fps; }
		//==============================

	private:
		//= COMMON GAMEOBJECT CREATION ======
		weakGameObj CreateSkybox();
		weakGameObj CreateCamera();
		weakGameObj CreateDirectionalLight();
		//===================================

		std::vector<sharedGameObj> m_gameObjects;
		std::vector<weakGameObj> m_renderables;
		std::vector<weakGameObj> m_lightsDirectional;
		std::vector<weakGameObj> m_lightsPoint;

		weakGameObj m_mainCamera;
		weakGameObj m_skybox;
		Math::Vector3 m_ambientLight;

		//= STATS =========
		float m_fps;
		float m_timePassed;
		int m_frameCount;
		//=================

		//= HELPER FUNCTIONS =
		void CalculateFPS();
		//====================
	};
}