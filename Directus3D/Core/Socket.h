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

//= INCLUDES ===============================
#include "Scene.h"
#include "Timer.h"
#include "../Input/Input.h"
#include "../Physics/PhysicsWorld.h"
#include "../AssetImporting/ImageImporter.h"
#include "Engine.h"
//==========================================

class ILogger;
class PhysicsDebugDraw;

class __declspec(dllexport) Socket
{
public:
	Socket(Engine* engine, std::shared_ptr<Scene> scene, std::shared_ptr<Renderer> renderer, std::shared_ptr<Input> input, std::shared_ptr<Timer> timer, std::shared_ptr<ModelImporter> modelLoader, std::shared_ptr<PhysicsWorld> physics, std::shared_ptr<TexturePool> texturePool, std::shared_ptr<Graphics> graphicsDevice);
	~Socket();

	//= STATE CONTROL ==============
	void StartEngine() const;
	static void StopEngine();
	void Update() const;
	//==============================

	//= IO ==========================================
	void LoadModel(const std::string& filePath);
	void LoadModelAsync(const std::string& filePath);
	void SaveSceneToFileAsync(const std::string& filePath);
	void LoadSceneFromFileAsync(const std::string& filePath);
	bool SaveSceneToFile(const std::string& filePath);
	bool LoadSceneFromFile(const std::string& filePath);
	//===============================================

	//= GRAPHICS =================================
	void SetViewport(int width, int height) const;
	//============================================

	//= MISC =======================================================================
	void SetPhysicsDebugDraw(bool enable);
	PhysicsDebugDraw* GetPhysicsDebugDraw();
	void ClearScene();
	ImageImporter* GetImageLoader();
	void SetLogger(ILogger* logger);
	//==============================================================================

	//= GAMEOBJECTS ================================================================
	std::vector<GameObject*> GetAllGameObjects();
	std::vector<GameObject*> GetRootGameObjects();
	GameObject* GetGameObjectByID(std::string gameObjectID);
	int GetGameObjectCount();
	void DestroyGameObject(GameObject* gameObject);
	bool GameObjectExists(GameObject* gameObject);
	//==============================================================================

	//= STATS ======================================================================
	float GetFPS() const;
	int GetRenderedMeshesCount() const;
	float GetDeltaTime() const;
	float GetRenderTime() const;
	//==============================================================================

	void SetMaterialTexture(GameObject* gameObject, TextureType type, std::string texturePath);
private:
	Engine* m_engine;
	std::shared_ptr<Scene> m_scene;
	std::shared_ptr<Renderer> m_renderer;
	std::shared_ptr<Graphics> m_graphics;
	std::shared_ptr<Timer> m_timer;
	std::shared_ptr<Input> m_input;
	std::shared_ptr<TexturePool> m_texturePool;
	std::shared_ptr<ModelImporter> m_modelLoader;
	std::shared_ptr<PhysicsWorld> m_physics;
};
