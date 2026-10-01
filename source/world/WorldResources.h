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
    // Ownership is recorded in the same atomic commit as the world. Legacy saves
    // start with no deletion rights; saving adopts only referenced native assets.
    bool IsOwnedFileName(const std::string& name);
    std::set<std::string> ReadOwnedFiles(const std::string& world_path);
    void WriteOwnedFiles(pugi::xml_node& world, const std::set<std::string>& files);
}
