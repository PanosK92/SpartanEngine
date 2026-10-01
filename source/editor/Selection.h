/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once
#include "../world/components/Camera.h"
#include "../rhi/RHI_Vertex.h"
namespace spartan
{
    class Selection
    {
    public:
        static void Initialize();
        static void Publish();
        static const math::Ray& ComputePickingRay(Camera& camera);
        static Entity* FindEntityUnderCursor(Camera& camera);
        static Entity* FindIconUnderCursor(Camera& camera);
        static void Pick(Camera& camera);
        static void SetSelectedEntity(Entity* entity);
        static Entity* GetSelectedEntity();
        static void AddToSelection(Entity* entity);
        static void RemoveFromSelection(Entity* entity);
        static void ToggleSelection(Entity* entity);
        static void ClearSelection();
        static bool IsSelected(Entity* entity);
        static const std::vector<Entity*>& GetSelectedEntities() { return m_selected_entities; }
        static uint32_t GetSelectedEntityCount() { return static_cast<uint32_t>(m_selected_entities.size()); }
        static int GetSelectedInstance() { return m_selected_instance; }
    private:
        static std::vector<Entity*> m_selected_entities;
        static int m_selected_instance;
    };
}
