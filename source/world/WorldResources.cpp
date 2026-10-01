/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "pch.h"
#include "WorldResources.h"
#include "../io/pugixml.hpp"
#include <filesystem>
namespace spartan::world_resources
{
    bool IsOwnedFileName(const std::string& name)
    {
        if (name.empty() || name == "." || name == ".." || name.find_first_of("/\\:") != std::string::npos)
            return false;
        return FileSystem::IsEngineMeshFile(name) || FileSystem::IsEngineTextureFile(name) ||
               FileSystem::IsEngineMaterialFile(name);
    }

    std::set<std::string> ReadOwnedFiles(const std::string& world_path)
    {
        std::set<std::string> files;
        pugi::xml_document document;
        if (!document.load_file(world_path.c_str())) return files;
        for (const auto& resource : document.child("World").child("OwnedResources").children("Resource"))
        {
            const std::string name = resource.attribute("file").as_string();
            if (IsOwnedFileName(name)) files.insert(name);
        }
        return files;
    }

    void WriteOwnedFiles(pugi::xml_node& world, const std::set<std::string>& files)
    {
        auto resources = world.append_child("OwnedResources");
        for (const auto& name : files)
        {
            SP_ASSERT(IsOwnedFileName(name));
            resources.append_child("Resource").append_attribute("file") = name.c_str();
        }
    }
}
