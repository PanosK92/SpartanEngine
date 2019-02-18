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

//= INCLUDES ==================
#include <vector>
#include <memory>
#include "../Core/EngineDefs.h"
#include "../Core/ISubsystem.h"
//=============================

namespace Directus
{
	class Entity;
	class Light;
	class Input;
	class Profiler;

	enum Scene_State
	{
		Ticking,
		Idle,
		Request_Loading,
		Loading
	};

	class ENGINE_CLASS World : public ISubsystem
	{
	public:
		World(Context* context);
		~World();

		//= ISubsystem ============
		bool Initialize() override;
		void Tick() override;
		//=========================
		
		void Unload();

		//= IO ========================================
		bool SaveToFile(const std::string& filePath);
		bool LoadFromFile(const std::string& filePath);
		//=============================================

		//= entity HELPER FUNCTIONS ===============================================================
		std::shared_ptr<Entity>& Entity_Create();
		std::shared_ptr<Entity>& Entity_Add(const std::shared_ptr<Entity>& entity);
		bool entity_Exists(const std::shared_ptr<Entity>& entity);
		void Entity_Remove(const std::shared_ptr<Entity>& entity);
		const std::vector<std::shared_ptr<Entity>>& Entities_GetAll() { return m_entitiesPrimary; }
		std::vector<std::shared_ptr<Entity>> Entities_GetRoots();
		const std::shared_ptr<Entity>& Entity_GetByName(const std::string& name);
		const std::shared_ptr<Entity>& Entity_GetByID(unsigned int ID);
		int Entity_GetCount() { return (int)m_entitiesPrimary.size(); }
		//=========================================================================================

		//= SELECTED ENTITY ===================================================================
		std::shared_ptr<Entity> GetSelectedentity()				{ return m_entity_selected; }
		void SetSelectedentity(std::shared_ptr<Entity> entity)	{ m_entity_selected = entity; }
		//=====================================================================================

		// Picks the closest entity under the mouse cursor
		void Pickentity();

	private:
		//= COMMON entity CREATION =======================
		std::shared_ptr<Entity>& CreateSkybox();
		std::shared_ptr<Entity> CreateCamera();
		std::shared_ptr<Entity>& CreateDirectionalLight();
		//===============================================

		// Double-buffered entities
		std::vector<std::shared_ptr<Entity>> m_entitiesPrimary;
		std::vector<std::shared_ptr<Entity>> m_entitiesSecondary;

		std::shared_ptr<Entity> m_entity_empty;
		std::shared_ptr<Entity> m_entity_selected;
		Input* m_input;
		Profiler* m_profiler;
		bool m_wasInEditorMode;
		bool m_isDirty;
		Scene_State m_state;
	};
}