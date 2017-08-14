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

//= INCLUDES =================================
#include "Socket.h"
#include "../Core/Scene.h"
#include "../Core/Engine.h"
#include "../Core/Timer.h"
#include "../Input/Input.h"
#include "../Physics/Physics.h"
#include "../Logging/Log.h"
#include "../Graphics/Renderer.h"
#include "../Resource/ResourceManager.h"
#include "../Resource/Import/ImageImporter.h"
#include "../Resource/Import/ModelImporter.h"
//===========================================

//= NAMESPACES =====
using namespace std;
//==================

class Model;

namespace Directus
{
	Socket::Socket(Context* context) : Subsystem(context)
	{
		m_engine = nullptr;
	}

	Socket::~Socket()
	{

	}

	bool Socket::Initialize()
	{
		m_engine = m_context->GetSubsystem<Engine>();
		return true;
	}

	//= STATE CONTROL ==============================================================
	void Socket::Start()
	{
		m_context->GetSubsystem<Scene>()->Start();
	}

	void Socket::OnDisable()
	{
		m_context->GetSubsystem<Scene>()->OnDisable();
	}

	void Socket::Update()
	{
		if (!m_engine)
			return;

		m_engine->Update();
	}
	//=============================================================================

	//= RESOURCE IO ===============================================================
	void Socket::LoadModel(const string& filePath)
	{
		auto resourceMng = m_context->GetSubsystem<ResourceManager>();
		resourceMng->Load<Model>(filePath);
	}

	void Socket::LoadModelAsync(const string& filePath)
	{
		ResourceManager* resourceMng = m_context->GetSubsystem<ResourceManager>();
		resourceMng->Load<Model>(filePath);
	}

	void Socket::SaveSceneToFileAsync(const string& filePath)
	{
		return m_context->GetSubsystem<Scene>()->SaveToFileAsync(filePath);
	}

	void Socket::LoadSceneFromFileAsync(const string& filePath)
	{
		return m_context->GetSubsystem<Scene>()->LoadFromFileAsync(filePath);
	}

	bool Socket::SaveSceneToFile(const string& filePath)
	{
		return m_context->GetSubsystem<Scene>()->SaveToFile(filePath);
	}

	bool Socket::LoadSceneFromFile(const string& filePath)
	{
		return m_context->GetSubsystem<Scene>()->LoadFromFile(filePath);
	}
	//==============================================================================

	//= GRAPHICS ===================================================================
	void Socket::SetViewport(float width, float height)
	{
		m_context->GetSubsystem<Renderer>()->SetViewport(width, height);
	}

	void Socket::SetResolution(int width, int height)
	{
		m_context->GetSubsystem<Renderer>()->SetResolution(width, height);
	}

	//==============================================================================

	//= MISC =======================================================================
	void Socket::SetPhysicsDebugDraw(bool enable)
	{
		//m_renderer->SetPhysicsDebugDraw(enable);
	}

	PhysicsDebugDraw* Socket::GetPhysicsDebugDraw()
	{
		return m_context->GetSubsystem<Physics>()->GetPhysicsDebugDraw();
	}

	void Socket::ClearScene()
	{
		m_context->GetSubsystem<Scene>()->Clear();
	}

	weak_ptr<ImageImporter> Socket::GetImageImporter()
	{
		ResourceManager* resourceMng = m_context->GetSubsystem<ResourceManager>();
		return resourceMng->GetImageImporter();
	}

	void Socket::SetLogger(weak_ptr<ILogger> logger)
	{
		Log::SetLogger(logger);
	}
	//==============================================================================

	//= GAMEOBJECTS ================================================================
	vector<sharedGameObj> Socket::GetAllGameObjects()
	{
		return m_context->GetSubsystem<Scene>()->GetAllGameObjects();
	}

	vector<weakGameObj> Socket::GetRootGameObjects()
	{
		return m_context->GetSubsystem<Scene>()->GetRootGameObjects();
	}

	weakGameObj Socket::GetGameObjectByID(string gameObjectID)
	{
		return m_context->GetSubsystem<Scene>()->GetGameObjectByID(gameObjectID);
	}

	int Socket::GetGameObjectCount()
	{
		return m_context->GetSubsystem<Scene>()->GetGameObjectCount();
	}

	void Socket::DestroyGameObject(weakGameObj gameObject)
	{
		if (gameObject.expired())
		{
			return;
		}

		m_context->GetSubsystem<Scene>()->RemoveGameObject(gameObject);
	}

	bool Socket::GameObjectExists(weakGameObj gameObject)
	{
		if (gameObject.expired())
		{
			return false;
		}

		return m_context->GetSubsystem<Scene>()->GameObjectExists(gameObject);
	}
	//==============================================================================

	//= STATS ======================================================================
	float Socket::GetFPS()
	{
		return m_context->GetSubsystem<Scene>()->GetFPS();
	}

	int Socket::GetRenderTime()
	{
		return m_context->GetSubsystem<Renderer>()->GetRenderTime();
	}

	int Socket::GetRenderedMeshesCount()
	{
		return m_context->GetSubsystem<Renderer>()->GetRenderedMeshesCount();
	}

	float Socket::GetDeltaTime()
	{
		return m_context->GetSubsystem<Timer>()->GetDeltaTime();
	}
	//==============================================================================
}