/*
Copyright(c) 2016-2022 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#pragma once

//= INCLUDES =============================
#include <memory>
#include <string>
#include "../../Core/SpartanDefinitions.h"
//========================================

struct aiNode;
struct aiScene;
struct aiMaterial;
struct aiMesh;

namespace Spartan
{
    class Context;
    class Entity;
    class Mesh;
    class World;

    class SP_CLASS ModelImporter
    {
    public:
        ModelImporter(Context* context);
        ~ModelImporter() = default;

        bool Load(Mesh* mesh, const std::string& file_path);

    private:
        // Parsing
        void ParseNode(const aiNode* node, std::shared_ptr<Entity> parent_entity = nullptr);
        void PashMeshes(const aiNode* node, Entity* new_entity);
        void ParseAnimations();

        // Loading
        void ParseMesh(aiMesh* mesh, Entity* entity_parent);
        void LoadBones(const aiMesh* mesh);

        // Model
        std::string m_file_path;
        std::string m_name;
        bool m_has_animation   = false;
        bool m_is_gltf         = false;
        Mesh* m_mesh           = nullptr;
        const aiScene* m_scene = nullptr;

        // Dependencies
        Context* m_context;
        World* m_world;
    };
}
