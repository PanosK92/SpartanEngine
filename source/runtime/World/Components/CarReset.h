/*
Copyright(c) 2015-2026 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
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
