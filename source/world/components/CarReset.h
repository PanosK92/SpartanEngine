/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

#include "Component.h"

namespace spartan
{
    class SpawnPoint;

    class CarReset : public Component
    {
    public:
        CarReset(Entity* entity);
        ~CarReset() = default;

        void Save(pugi::xml_node& node) override;
        void Load(pugi::xml_node& node) override;

        uint64_t GetSpawnPointEntityId() const
        {
            return m_spawn_point_entity_id;
        }

        void SetSpawnPointEntityId(uint64_t id);
        Entity* GetSpawnPointEntity();
        SpawnPoint* GetSpawnPoint();

    private:
        uint64_t m_spawn_point_entity_id = 0;
        Entity* m_spawn_point_entity = nullptr;
    };
}
