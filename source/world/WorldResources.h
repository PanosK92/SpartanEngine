/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once
#include <set>
#include <string>
namespace pugi { class xml_node; }
namespace spartan::world_resources
{
    // The folder the world saves a native resource into, relative to its resource
    // directory: meshes/, materials/ or textures/, empty for any other file.
    std::string GetTypeDirectory(const std::string& name);

    // Ownership is recorded in the same atomic commit as the world. Legacy saves
    // start with no deletion rights; saving adopts only referenced native assets.
    // An owned name is relative to the resource directory: a file in its type
    // folder (meshes/rock.mesh), or a bare file name from before the type folders.
    bool IsOwnedFileName(const std::string& name);
    std::set<std::string> ReadOwnedFiles(const std::string& world_path);
    void WriteOwnedFiles(pugi::xml_node& world, const std::set<std::string>& files);
}
