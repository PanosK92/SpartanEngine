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

//= INCLUDES =================
#include "../Core/Subsystem.h"
#include <memory>
#include <vector>
//============================

namespace Directus
{
	class ILogger;
	class PhysicsDebugDraw;
	class Engine;
	class GameObject;
	class ImageImporter;

	class DLL_API Socket : public Subsystem
	{
	public:
		Socket(Context* context);
		~Socket();

		//= Subsystem ============
		virtual bool Initialize();
		//========================

		//= UPDATE ======
		void Start();
		void OnDisable();
		void Update();
		//===============

		//= RESOURCE IO =========================================
		void LoadModel(const std::string& filePath);
		void LoadModelAsync(const std::string& filePath);
		void SaveSceneToFileAsync(const std::string& filePath);
		void LoadSceneFromFileAsync(const std::string& filePath);
		bool SaveSceneToFile(const std::string& filePath);
		bool LoadSceneFromFile(const std::string& filePath);
		//=======================================================

		//= GRAPHICS ===============================
		void SetViewport(float width, float height);
		void SetResolution(int width, int height);
		//==========================================

		//= MISC =======================================
		void SetPhysicsDebugDraw(bool enable);
		PhysicsDebugDraw* GetPhysicsDebugDraw();
		void ClearScene();
		std::weak_ptr<ImageImporter> GetImageImporter();
		void SetLogger(std::weak_ptr<ILogger> logger);
		Context* GetContext() { return m_context; }
		//==============================================

		//= GAMEOBJECTS ======================================================
		std::vector<std::weak_ptr<GameObject>> GetAllGameObjects();
		std::vector<std::weak_ptr<GameObject>> GetRootGameObjects();
		std::weak_ptr<GameObject> GetGameObjectByID(std::string gameObjectID);
		int GetGameObjectCount();
		void DestroyGameObject(std::weak_ptr<GameObject> gameObject);
		bool GameObjectExists(std::weak_ptr<GameObject> gameObject);
		//====================================================================

		//= STATS ==============================================
		float GetFPS();
		int GetRenderTime();
		int GetRenderedMeshesCount();
		float GetDeltaTime();
		//======================================================

	private:
		Engine* m_engine;
	};
}