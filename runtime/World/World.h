/*
Copyright(c) 2016-2023 Panos Karabelas

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

//= INCLUDES ===============
#include "Definitions.h"
#include "../Math/Vector3.h"
#include "Entity.h"
#include "Components/Transform.h"
#include "Components/Camera.h"
#include "Components/Light.h"
//==========================

namespace Spartan
{
    class SP_CLASS World
    {
    public:
        // system
        static void Initialize();
        static void Shutdown();
        static void PreTick();
        static void Tick();

        // io
        static bool SaveToFile(const std::string& filePath);
        static bool LoadFromFile(const std::string& file_path);

        // misc
        static void New();
        static void Resolve();
        static const std::string GetName();
        static const std::string& GetFilePath();

        // default worlds
        static void CreateDefaultWorldCube();
        static void CreateDefaultWorldHelmets();
        static void CreateDefaultWorldCar();
        static void CreateDefaultWorldForest();
        static void CreateDefaultWorldSponza();
        static void CreateDefaultWorldDoomE1M1();

        // entities
        static std::shared_ptr<Entity> CreateEntity();
        static bool EntityExists(Entity* entity);
        static void RemoveEntity(std::shared_ptr<Entity> entity);
        static std::vector<std::shared_ptr<Entity>> GetRootEntities();
        static const std::shared_ptr<Entity>& GetEntityByName(const std::string& name);
        static const std::shared_ptr<Entity>& GetEntityById(uint64_t id);
        static const std::vector<std::shared_ptr<Entity>>& GetAllEntities();

    protected:
        static inline std::vector<std::shared_ptr<Entity>> m_entities;
        static inline std::string m_name;
        static inline std::string m_file_path;
        static inline std::mutex m_entity_access_mutex;
        static inline bool m_resolve = false;
        static inline bool m_was_in_editor_mode = false;

        // default worlds resources
        static inline std::shared_ptr<Entity> m_default_terrain             = nullptr;
        static inline std::shared_ptr<Entity> m_default_cube                = nullptr;
        static inline std::shared_ptr<Entity> m_default_physics_body_camera = nullptr;
        static inline std::shared_ptr<Entity> m_default_environment         = nullptr;
        static inline std::shared_ptr<Entity> m_default_model_floor         = nullptr;
        static inline std::shared_ptr<Mesh> m_default_model_sponza          = nullptr;
        static inline std::shared_ptr<Mesh> m_default_model_sponza_curtains = nullptr;
        static inline std::shared_ptr<Mesh> m_default_model_car             = nullptr;
        static inline std::shared_ptr<Mesh> m_default_model_wheel           = nullptr;
        static inline std::shared_ptr<Mesh> m_default_model_helmet_flight   = nullptr;
        static inline std::shared_ptr<Mesh> m_default_model_helmet_damaged  = nullptr;

        static void create_default_world_common(
            const Math::Vector3& camera_position = Math::Vector3(0.0f, 2.0f, -10.0f),
            const Math::Vector3& camera_rotation = Math::Vector3(0.0f, 0.0f, 0.0f),
            const LightIntensity sun_intensity   = LightIntensity::sky_sunlight_noon,
            const char* soundtrack_file_path     = "project\\music\\jake_chudnow_shona.mp3",
            const bool shadows_enabled           = true,
            const bool load_floor                = true
        );

        static void create_default_cube(
            const Math::Vector3& position = Math::Vector3(0.0f, 4.0f, 0.0f),
            const Math::Vector3& scale    = Math::Vector3(1.0f, 1.0f, 1.0f)
        );

        static void create_default_car(const Math::Vector3& position = Math::Vector3(0.0f, 0.2f, 0.0f));

    private:
        static void Clear();
    };
}
