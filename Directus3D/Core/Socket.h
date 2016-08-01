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

//= INCLUDES ========================
#include "Scene.h"
#include "Timer.h"
#include "../Input/Input.h"
#include "../Physics/PhysicsWorld.h"
#include "../Loading/ImageLoader.h"
#include "Engine.h"
//===================================

class ILogger;
class PhysicsDebugDraw;

class __declspec(dllexport) Socket
{
public:
	Socket(Engine* engine, Scene* scene, Renderer* renderer, Input* input, Timer* timer, ModelLoader* modelLoader, PhysicsWorld* physics, TexturePool* texturePool, GraphicsDevice* graphicsDevice);
	~Socket();

	//= IO =========================
	void SetLogger(ILogger* logger);
	void LoadModel(std::string path);
	ImageLoader* GetImageLoader();
	//==============================

	//= GRAPHICS =================================
	void SetViewport(int width, int height) const;
	//============================================

	//= MISC =======================================================================
	void Update();
	void SetPhysicsDebugDraw(bool enable);
	PhysicsDebugDraw* GetPhysicsDebugDraw();
	//==============================================================================

	//= GAMEOBJECTS ================================================================
	std::vector<GameObject*> GetAllGameObjects();
	std::vector<GameObject*> GetRootGameObjects();
	GameObject* GetGameObjectByID(std::string gameObjectID);
	int GetGameObjectCount();
	void DestroyGameObject(GameObject* gameObject);
	bool GameObjectExists(GameObject* gameObject);
	//==============================================================================

	//= SCENE ======================================================================
	bool SaveSceneToFile(std::string path);
	bool LoadSceneFromFile(std::string path);
	void ClearScene();
	//==============================================================================

	//= STATS ======================================================================
	float GetFPS() const;
	int GetRenderedMeshesCount() const;
	float GetDeltaTime() const;
	float GetUpdateTime() const;
	float GetRenderTime() const;
	//==============================================================================

	void SetMaterialTexture(GameObject* gameObject, TextureType type, std::string texturePath);
private:
	Engine* m_engine;
	Scene* m_scene;
	Renderer* m_renderer;
	GraphicsDevice* m_graphicsDevice;
	Timer* m_timer;
	Input* m_input;
	TexturePool* m_texturePool;
	ModelLoader* m_modelLoader;
	PhysicsWorld* m_physics;
};
