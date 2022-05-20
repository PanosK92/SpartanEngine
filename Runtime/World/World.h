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

//= INCLUDES ==========================
#include <vector>
#include <memory>
#include <string>
#include "../Core/Subsystem.h"
#include "../Core/SpartanDefinitions.h"
//=====================================

namespace Spartan
{
    //= FWD DECLARATIONS =
    class Entity;
    class Light;
    class Input;
    class Profiler;
    class TransformHandle;
    //====================

    class SPARTAN_CLASS World : public Subsystem
    {
    public:
        World(Context* context);
        ~World();

        //= ISubsystem =========================
        bool OnInitialize() override;
        void OnPreTick() override;
        void OnTick(double delta_time) override;
        //======================================
        
        void New();
        bool SaveToFile(const std::string& filePath);
        bool LoadFromFile(const std::string& file_path);
        void Resolve() { m_resolve = true; }
        bool IsLoading();
        const std::string GetName()      const { return m_name; }
        const std::string& GetFilePath() const { return m_file_path; }

        //= Entities ===========================================================
        std::shared_ptr<Entity> EntityCreate(bool is_active = true);
        bool EntityExists(const std::shared_ptr<Entity>& entity);
        void EntityRemove(const std::shared_ptr<Entity>& entity);
        std::vector<std::shared_ptr<Entity>> EntityGetRoots();
        const std::shared_ptr<Entity>& EntityGetByName(const std::string& name);
        const std::shared_ptr<Entity>& EntityGetById(uint64_t id);
        const auto& EntityGetAll() const { return m_entities; }
        //======================================================================

        // Transform handle
        std::shared_ptr<TransformHandle> GetTransformHandle() { return m_transform_handle; }
        float m_gizmo_transform_size  = 0.015f;

    private:
        void Clear();
        void _EntityRemove(const std::shared_ptr<Entity>& entity);
        void CreateDefaultWorldEntities();
        
        //= COMMON ENTITY CREATION ======================
        std::shared_ptr<Entity> CreateEnvironment();
        std::shared_ptr<Entity> CreateCamera();
        std::shared_ptr<Entity> CreateDirectionalLight();
        //===============================================

        std::string m_name;
        std::string m_file_path;
        bool m_was_in_editor_mode = false;
        bool m_resolve            = true;
        Input* m_input            = nullptr;
        Profiler* m_profiler      = nullptr;

        std::shared_ptr<TransformHandle> m_transform_handle;
        std::vector<std::shared_ptr<Entity>> m_entities;
    };
}
