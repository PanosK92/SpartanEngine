/*
Copyright(c) 2016-2020 Panos Karabelas

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

//= INCLUDES ===========================
#include <vector>
#include <memory>
#include <string>
#include "../Core/ISubsystem.h"
#include "../Core/Spartan_Definitions.h"
//======================================

namespace Spartan
{
    class Entity;
    class Light;
    class Input;
    class Profiler;

    enum class WorldState
    {
        Ticking,
        Idle,
        RequestLoading,
        Loading
    };

    class SPARTAN_CLASS World : public ISubsystem
    {
    public:
        World(Context* context);
        ~World();

        //= ISubsystem ======================
        bool Initialize() override;
        void Tick(float delta_time) override;
        //===================================
        
        void Unload();
        bool SaveToFile(const std::string& filePath);
        bool LoadFromFile(const std::string& file_path);
        const auto& GetName() const { return m_name; }
        void MakeDirty() { m_is_dirty = true; }

        //= Entities ===========================================================================
        std::shared_ptr<Entity>& EntityCreate(bool is_active = true);
        std::shared_ptr<Entity>& EntityAdd(const std::shared_ptr<Entity>& entity);
        bool EntityExists(const std::shared_ptr<Entity>& entity);
        void EntityRemove(const std::shared_ptr<Entity>& entity);    
        std::vector<std::shared_ptr<Entity>> EntityGetRoots();
        const std::shared_ptr<Entity>& EntityGetByName(const std::string& name);
        const std::shared_ptr<Entity>& EntityGetById(uint32_t id);
        const auto& EntityGetAll() const    { return m_entities; }
        auto EntityGetCount() const         { return static_cast<uint32_t>(m_entities.size()); }
        //======================================================================================

    private:
        void _EntityRemove(const std::shared_ptr<Entity>& entity);

        //= COMMON ENTITY CREATION ========================
        std::shared_ptr<Entity>& CreateEnvironment();
        std::shared_ptr<Entity> CreateCamera();
        std::shared_ptr<Entity>& CreateDirectionalLight();
        //================================================

        std::string m_name;
        bool m_was_in_editor_mode   = false;
        bool m_is_dirty             = true;
        WorldState m_state          = WorldState::Ticking;
        Input* m_input              = nullptr;
        Profiler* m_profiler        = nullptr;

        std::vector<std::shared_ptr<Entity>> m_entities;
    };
}
