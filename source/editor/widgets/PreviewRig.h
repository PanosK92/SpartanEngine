/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES =================
#include <cstdint>
#include <string>
#include <vector>
#include "math/Vector3.h"
//============================

namespace spartan
{
    class Entity;
}

// the orbit camera and lights a tool window renders its secondary view through, the asset viewer and the
// tree creator both preview a transient hierarchy that is inactive in the main viewport
namespace preview_rig
{
    struct rig_ids
    {
        uint64_t rig    = 0;
        uint64_t camera = 0;
    };

    // camera, key and fill light parented to a rig under root, names are prefixed so each window's rig reads apart in the world
    rig_ids create(spartan::Entity* root, const spartan::math::Vector3& center, float radius, const std::string& prefix);

    // enclosing sphere of every render among the entities, false when none of them draws anything
    bool compute_bounds(const std::vector<spartan::Entity*>& entities, spartan::math::Vector3& center, float& radius);

    // puts the camera on the orbit sphere so the bounding sphere fits the tighter frustum axis
    void place_camera(spartan::Entity* camera_entity, const spartan::math::Vector3& center, float radius, float yaw, float pitch, float zoom, float aspect);
}
