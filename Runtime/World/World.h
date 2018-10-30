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

	enum Scene_State
	{
		Scene_Idle,
		Scene_Ticking,
		Scene_Saving,
		Scene_Loading
	};

	class ENGINE_CLASS World : public Subsystem
	{
	public:
		World(Context* context);
		~World();

		//= Subsystem =============
		bool Initialize() override;
		//=========================

		void Tick();
		void Unload();

		//= IO ========================================
		bool SaveToFile(const std::string& filePath);
		bool LoadFromFile(const std::string& filePath);
		//=============================================

		//= Actor HELPER FUNCTIONS ===================================================
		std::weak_ptr<Actor> Actor_CreateAdd();
		void Actor_Add(std::shared_ptr<Actor> actor);
		bool Actor_Exists(const std::weak_ptr<Actor>& actor);
		void Actor_Remove(const std::weak_ptr<Actor>& actor);
		const std::vector<std::shared_ptr<Actor>>& Actors_GetAll() { return m_actors; }
		std::vector<std::weak_ptr<Actor>> Actors_GetRoots();
		std::weak_ptr<Actor> Actor_GetRoot(std::weak_ptr<Actor> actor);
		std::weak_ptr<Actor> Actor_GetByName(const std::string& name);
		std::weak_ptr<Actor> Actor_GetByID(unsigned int ID);	
		int Actor_GetCount() { return (int)m_actors.size(); }
		//============================================================================

		//= MISC ====================================================
		std::weak_ptr<Actor> GetMainCamera() { return m_mainCamera; }
		void SetAmbientLight(float x, float y, float z);
		Math::Vector3 GetAmbientLight();
		//===========================================================

	private:
		//= COMMON ACTOR CREATION ====================
		std::weak_ptr<Actor> CreateSkybox();
		std::weak_ptr<Actor> CreateCamera();
		std::weak_ptr<Actor> CreateDirectionalLight();
		//============================================

		std::vector<std::shared_ptr<Actor>> m_actors;
		std::weak_ptr<Actor> m_mainCamera;
		std::weak_ptr<Actor> m_skybox;
		Math::Vector3 m_ambientLight;
		bool m_wasInEditorMode;
		bool m_isDirty;
		Scene_State m_state;
	};
}