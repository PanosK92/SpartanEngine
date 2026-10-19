/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once
#include <string>
namespace spartan::world_systems
{
    // Optional world systems, all off until a world script turns them on (WorldSystems table in lua).
    // Reset before every load so one world's setup never leaks into the next.
    struct Config
    {
        bool ambient_wildlife   = false;
        bool ambient_atmosphere = false;
        bool road_details       = false;
        std::string layered_roads_parent;       // splines under this entity get the layered asphalt/paint/shoulder finish
        std::string layered_roads_material_dir; // holds asphalt_track_* and gravel_road_* textures
        std::string layered_roads_material_prefix = "road"; // materials are <prefix>_asphalt, <prefix>_road_paint, <prefix>_road_shoulder
    };

    const Config& GetConfig();
    void Initialize();
}
