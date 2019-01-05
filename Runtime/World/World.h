/*
Copyright(c) 2016-2019 Panos Karabelas

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
		Ticking,
		Idle,
		Request_Loading,
		Loading
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

		//= Actor HELPER FUNCTIONS ====================================================
		std::shared_ptr<Actor>& Actor_Create();
		std::shared_ptr<Actor>& Actor_Add(const std::shared_ptr<Actor>& actor);
		bool Actor_Exists(const std::weak_ptr<Actor>& actor);
		void Actor_Remove(const std::weak_ptr<Actor>& actor);
		const std::vector<std::shared_ptr<Actor>>& Actors_GetAll() { return m_actorsPrimary; }
		std::vector<std::shared_ptr<Actor>> Actors_GetRoots();
		const std::shared_ptr<Actor>& Actor_GetByName(const std::string& name);
		const std::shared_ptr<Actor>& Actor_GetByID(unsigned int ID);
		int Actor_GetCount() { return (int)m_actorsPrimary.size(); }
		//=============================================================================

	private:
		//= COMMON ACTOR CREATION =======================
		std::shared_ptr<Actor>& CreateSkybox();
		std::shared_ptr<Actor>& CreateCamera();
		std::shared_ptr<Actor>& CreateDirectionalLight();
		//===============================================

		// Double-buffered actors
		std::vector<std::shared_ptr<Actor>> m_actorsPrimary;
		std::vector<std::shared_ptr<Actor>> m_actorsSecondry;

		std::shared_ptr<Actor> m_actorEmpty;
		std::weak_ptr<Actor> m_skybox;
		bool m_wasInEditorMode;
		bool m_isDirty;
		Scene_State m_state;
	};
}