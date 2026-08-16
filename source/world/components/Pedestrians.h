/*
Copyright(c) 2015-2026 Panos Karabelas

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

#include "Component.h"
#include "../../math/Vector3.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace spartan
{
    class Animator;
    class Entity;
    class Mesh;
    class Ragdoll;

    class Pedestrians : public Component
    {
    public:
        Pedestrians(Entity* entity);
        ~Pedestrians() override;

        void Start() override;
        void Stop() override;
        void Tick() override;
        void Save(pugi::xml_node& node) override;
        void Load(pugi::xml_node& node) override;

    private:
        struct Walker
        {
            Entity* entity = nullptr;
            Animator* animator = nullptr;
            Ragdoll* ragdoll = nullptr;
            std::shared_ptr<Mesh> mesh;
            math::Vector3 heading = math::Vector3(0.0f, 0.0f, -1.0f);
            float speed = 2.5f;
            float ground_y = 0.0f;
            float height_offset = 0.0f;
            float turn_timer = 0.0f;
            float blocked_timer = 0.0f;
            float ground_sample_timer = 0.0f;
            bool animating = false;
            bool dead = false;
        };

        struct PreloadState
        {
            std::atomic<bool> cancelled = false;
            std::atomic<bool> completed = false;
            std::atomic<bool> succeeded = false;
        };

        void BeginSpawn();
        void BeginPreload();
        void CancelPreload();
        bool FinishPreloadOnMainThread();
        void AdoptTemplateRoot();
        void SpawnNext();
        bool SpawnWalker(uint32_t index);
        bool FindSpawnPosition(uint32_t index, math::Vector3& position, math::Vector3& heading);
        bool SampleGround(const math::Vector3& position, math::Vector3& ground_position) const;
        bool IsPathClear(const math::Vector3& position, const math::Vector3& direction, float distance) const;
        bool IsInsideBounds(const math::Vector3& position, float margin) const;
        void UpdateWalker(Walker& walker, float delta_time);
        void UpdateWalkerFar(Walker& walker, float delta_time);
        void UpdateAnimationLod();
        float NextFloat();
        uint32_t NextUInt();

        std::vector<Walker> m_walkers;
        std::shared_ptr<Mesh> m_source_mesh;
        math::Vector3 m_bounds_min = math::Vector3(454.5f, -10.0f, -793.9f);
        math::Vector3 m_bounds_max = math::Vector3(1414.5f, 80.0f, 166.1f);
        std::string m_model_file = "project/models/mannequiny/mannequiny.glb";
        uint32_t m_count = 100;
        uint32_t m_max_animated = 32;
        float m_animation_radius = 120.0f;
        float m_walk_speed = 2.5f;
        float m_lod_timer = 0.0f;
        uint32_t m_random_state = 0x6d2b79f5;
        uint32_t m_next_spawn_index = 0;
        bool m_spawn_ready = false;
        bool m_physics_ready = false;
        std::shared_ptr<PreloadState> m_preload_state;
    };
}
