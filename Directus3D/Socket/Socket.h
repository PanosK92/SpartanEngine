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
#include "../Core/Scene.h"
#include "../Input/Input.h"
#include "../Physics/PhysicsWorld.h"
#include "../FileSystem/ImageImporter.h"
#include "../Graphics/Texture.h"
//==========================================

class ILogger;
class PhysicsDebugDraw;

class __declspec(dllexport) Socket : public Object
{
public:
	Socket(Context* context);
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
	void SetLogger(std::weak_ptr<ILogger> logger);
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
};
