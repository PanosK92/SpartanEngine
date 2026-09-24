/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

#include "Component.h"
#include "../RoadTraffic.h"
#include "../RoadPopulation.h"
#include "../../math/Vector3.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

namespace spartan
{
    class Animator;
    class Entity;
    class Mesh;
    class Ragdoll;
    class NavigationWorld;

    class Pedestrians : public Component
    {
    public:
        Pedestrians(Entity* entity);
        ~Pedestrians() override;

        bool PrepareWorld();
        void Start() override;
        void Stop() override;
        void Tick() override;
        void Save(pugi::xml_node& node) override;
        void Load(pugi::xml_node& node) override;
        bool GetUseNavigation() const { return m_use_navigation; }
        void SetUseNavigation(bool enabled) { m_use_navigation = enabled; } // applies on next Start
        uint64_t GetNavigationEntityId() const { return m_navigation_entity_id; }
        void SetNavigationEntityId(uint64_t id) { m_navigation_entity_id = id; }

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
            size_t road_edge = road_traffic::invalid;
            road_traffic::Path road_path;
            float road_progress = 0.0f;
            uint32_t route_random = 1;
            int navigation_agent = -1;
            bool navigation_moving = false;
            float navigation_timer = 0.0f;
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
        void UpdatePopulation(float delta_time);
        bool SpawnWalker(uint32_t index);
        bool FindSpawnPosition(uint32_t index, math::Vector3& position, math::Vector3& heading);
        bool SampleGround(const math::Vector3& position, math::Vector3& ground_position) const;
        bool IsPathClear(const math::Vector3& position, const math::Vector3& direction, float distance) const;
        bool IsInsideBounds(const math::Vector3& position, float margin) const;
        void UpdateWalker(Walker& walker, float delta_time);
        void UpdateWalkerFar(Walker& walker, float delta_time);
        void UpdateAnimationLod();
        bool FindRoadSpawn(uint32_t index, Walker& walker, math::Vector3& position, math::Vector3& heading);
        void UpdateRoadWalker(Walker& walker, float delta_time);
        void UpdateNavigationWalker(Walker& walker, float delta_time);
        void ResolveNavigation();
        void ReleaseNavigation();
        float NextFloat();
        uint32_t NextUInt();

        std::vector<Walker> m_walkers;
        std::unordered_map<uint64_t, std::vector<Walker*>> m_walker_cells;
        road_traffic::Network m_road_network;
        road_traffic::LocalPopulation m_population;
        float m_population_timer = 0.0f;
        float m_recycle_timer = 0.0f;
        bool m_follow_roads = false;
        bool m_use_navigation = false;
        uint64_t m_navigation_entity_id = 0; // 0 chooses the first active Navigation component
        std::shared_ptr<NavigationWorld> m_navigation; // consumer reference, never ticks the provider
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
