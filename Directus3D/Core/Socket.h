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
#include "../Physics/PhysicsEngine.h"
#include "../Core/Settings.h"
#include "../Loading/ImageLoader.h"
//===================================

class PhysicsDebugDraw;

class __declspec(dllexport) Socket
{
public:
	Socket(Scene* scene, Renderer* renderer, Timer* timer, ModelLoader* modelLoader, PhysicsEngine* physics, TexturePool* texturePool, GraphicsDevice* graphicsDevice);
	~Socket();

	/*------------------------------------------------------------------------------
							[VIEWPORT]
	------------------------------------------------------------------------------*/
	void SetViewport(int width, int height) const;

	/*------------------------------------------------------------------------------
								[STATES]
	------------------------------------------------------------------------------*/
	EngineMode GetEngineMode();
	void SetEngineMode(EngineMode mode);

	void SetPhysicsDebugDraw(bool enable);
	PhysicsDebugDraw* GetPhysicsDebugDraw();

	/*------------------------------------------------------------------------------
									[GAMEOBJECT]
	------------------------------------------------------------------------------*/
	int GetGameObjectCount();
	GameObject* GetGameObjectByIndex(int index);
	GameObject* GetGameObjectByID(std::string gameObjectID);
	void CopyGameObject(GameObject* gameObject);
	void DestroyGameObject(GameObject* gameObject);
	bool GameObjectExists(GameObject* gameObject);

	/*------------------------------------------------------------------------------
							[ASSET LOADING]
	------------------------------------------------------------------------------*/
	void LoadModel(GameObject* gameObject, std::string path);
	ImageLoader* GetImageLoader();

	/*------------------------------------------------------------------------------
								[SCENE]
	------------------------------------------------------------------------------*/
	bool SaveSceneToFile(std::string path);
	bool LoadSceneFromFile(std::string path);
	void ClearScene();

	/*------------------------------------------------------------------------------
								[STATS]
	------------------------------------------------------------------------------*/
	float GetFPS();
	int GetRenderedMeshesCount();
	float GetDeltaTime();
	float GetRenderTime();

	void SetMaterialTexture(GameObject* gameObject, TextureType type, std::string texturePath);
private:
	Scene* m_scene;
	Renderer* m_renderer;
	GraphicsDevice* m_graphicsDevice;
	Timer* m_timer;
	TexturePool* m_texturePool;
	ModelLoader* m_modelLoader;
	PhysicsEngine* m_physics;
};
