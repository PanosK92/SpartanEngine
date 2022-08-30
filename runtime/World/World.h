/*
Copyright(c) 2016-2022 Panos Karabelas

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

//= INCLUDES ===================
#include <vector>
#include <memory>
#include <string>
#include "../Core/ISystem.h"
#include "../Core/Definitions.h"
#include "../Math/Vector3.h"
#include "../Rendering/Mesh.h"
//==============================

namespace Spartan
{
    //= FWD DECLARATIONS =
    class Entity;
    class Light;
    class Input;
    class Profiler;
    class TransformHandle;
    //====================

    class SP_CLASS World : public ISystem
    {
    public:
        World(Context* context);
        ~World();

        //= ISubsystem =========================
        void OnInitialise() override;
        void OnPostInitialise() override;
        void OnPreTick() override;
        void OnTick(double delta_time) override;
        //======================================
        
        void New();
        bool SaveToFile(const std::string& filePath);
        bool LoadFromFile(const std::string& file_path);
        void Resolve() { m_resolve = true; }
        void CreateDefaultWorld();
        const std::string GetName()      const { return m_name; }
        const std::string& GetFilePath() const { return m_file_path; }

        //= Entities ===========================================================
        std::shared_ptr<Entity> CreateEntity(bool is_active = true);
        bool EntityExists(Entity* entity);
        void RemoveEntity(Entity* entity);
        std::vector<std::shared_ptr<Entity>> GetRootEntities();
        const std::shared_ptr<Entity>& GetEntityByName(const std::string& name);
        const std::shared_ptr<Entity>& GetEntityById(uint64_t id);
        const auto& GetAllEntities() const { return m_entities; }
        void ActivateNewEntities();
        //======================================================================

        // Transform handle
        std::shared_ptr<TransformHandle> GetTransformHandle() { return m_transform_handle; }
        float m_gizmo_transform_size  = 0.015f;

    private:
        void Clear();
        void _EntityRemove(Entity* entity);

        std::vector<std::shared_ptr<Entity>> m_entities_to_add;
        std::vector<std::shared_ptr<Entity>> m_entities;
        std::string m_name;
        std::string m_file_path;
        bool m_was_in_editor_mode                             = false;
        bool m_resolve                                        = true;
        std::shared_ptr<Mesh> m_default_model_sponza          = nullptr;
        std::shared_ptr<Mesh> m_default_model_sponza_curtains = nullptr;
        std::shared_ptr<Mesh> m_default_model_car             = nullptr;
        std::shared_ptr<TransformHandle> m_transform_handle   = nullptr;
        Input* m_input                                        = nullptr;
        Profiler* m_profiler                                  = nullptr;

        // Sync primitives
        std::mutex m_entity_access_mutex;
    };
}
