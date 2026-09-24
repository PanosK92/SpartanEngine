/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ====
#include <cstdint>
#include <string>
//===============

struct aiNode;
struct aiScene;
struct aiMesh;

namespace spartan
{
    class Entity;
    class Mesh;

    // forward declaration for import context
    struct ImportContext;

    class ModelImporter
    {
    public:
        static void Load(Mesh* mesh, const std::string& file_path);

    private:
        static void ParseNode(ImportContext& ctx, const aiNode* node, Entity* parent_entity = nullptr);
        static void ParseNodeMeshes(ImportContext& ctx, const aiNode* node, Entity* new_entity);
        static void ParseNodeLight(ImportContext& ctx, const aiNode* node, Entity* new_entity);
        static void ParseMesh(ImportContext& ctx, aiMesh* mesh, const uint32_t sub_mesh_index);
        static void ParseSkeleton(ImportContext& ctx);
        static void ParseAnimations(ImportContext& ctx);
    };
}
