/*
Copyright(c) 2016-2018 Panos Karabelas

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
	class Actor;
	class Light;

	class ENGINE_CLASS Scene : public Subsystem
	{
	public:
		Scene(Context* context);
		~Scene();

		//= Subsystem =============
		bool Initialize() override;
		//=========================

		//= Actor events ===============================
		// Runs every time the simulation starts
		void Start();
		// Runs every time the simulation stops
		void Stop();
		// Runs every frame
		void Update();
		// Runs when all actors should be destroyed
		void Clear();
		//==============================================

		//= IO ========================================
		bool SaveToFile(const std::string& filePath);
		bool LoadFromFile(const std::string& filePath);
		//=============================================

		//= actor HELPER FUNCTIONS =============================================================
		std::weak_ptr<Actor> Actor_CreateAdd();
		void Actor_Add(std::shared_ptr<Actor> actor);
		bool Actor_Exists(const std::weak_ptr<Actor>& actor);
		void Actor_Remove(const std::weak_ptr<Actor>& actor);
		const std::vector<std::shared_ptr<Actor>>& GetAllActors() { return m_actors; }
		std::vector<std::weak_ptr<Actor>> GetRootActors();
		std::weak_ptr<Actor> GetActorRoot(std::weak_ptr<Actor> actor);
		std::weak_ptr<Actor> GetActorByName(const std::string& name);
		std::weak_ptr<Actor> GetActorByID(unsigned int ID);	
		int GetactorCount() { return (int)m_actors.size(); }
		//===========================================================================================

		//= SCENE RESOLUTION  ==============================
		void Resolve();
		const std::vector<std::weak_ptr<Actor>>& GetRenderables() { return m_renderables; }
		std::weak_ptr<Actor> GetMainCamera() { return m_mainCamera; }

		//= MISC =======================================
		void SetAmbientLight(float x, float y, float z);
		Math::Vector3 GetAmbientLight();

		//= STATS ======================
		float GetFPS() { return m_fps; }
		//==============================

	private:
		//= COMMON ACTOR CREATION ====================
		std::weak_ptr<Actor> CreateSkybox();
		std::weak_ptr<Actor> CreateCamera();
		std::weak_ptr<Actor> CreateDirectionalLight();
		//============================================

		void ComputeFPS(); // TODO: This doesn't belong here

		std::vector<std::shared_ptr<Actor>> m_actors;
		std::vector<std::weak_ptr<Actor>> m_renderables;

		std::weak_ptr<Actor> m_mainCamera;
		std::weak_ptr<Actor> m_skybox;
		Math::Vector3 m_ambientLight;

		//= STATS ============
		float m_fps;
		float m_timePassed;
		int m_frameCount;
		bool m_isInEditorMode;
		//====================
	};
}