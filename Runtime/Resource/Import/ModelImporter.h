/*
Copyright(c) 2016-2020 Panos Karabelas

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

//= INCLUDES ==============================
#include <memory>
#include <string>
#include "../../Core/Spartan_Definitions.h"
//=========================================

struct aiNode;
struct aiScene;
struct aiMaterial;
struct aiMesh;

namespace Spartan
{
    class Context;
    class Material;
    class Entity;
    class Model;
    class World;

    struct ModelParams
    {
        uint32_t triangle_limit;
        uint32_t vertex_limit;
        float max_normal_smoothing_angle;
        float max_tangent_smoothing_angle;
        std::string file_path;
        std::string name;
        bool has_animation;
        Model* model            = nullptr;
        const aiScene* scene    = nullptr;
    };

    class SPARTAN_CLASS ModelImporter
    {
    public:
        ModelImporter(Context* context);
        ~ModelImporter() = default;

        bool Load(Model* model, const std::string& file_path);

    private:
        // Parsing
        void ParseNode(const aiNode* assimp_node, const ModelParams& params, Entity* parent_node = nullptr, Entity* new_entity = nullptr);
        void ParseNodeMeshes(const aiNode* assimp_node, Entity* new_entity, const ModelParams& params);
        void ParseAnimations(const ModelParams& params);

        // Loading
        void LoadMesh(aiMesh* assimp_mesh, Entity* entity_parent, const ModelParams& params);
        void LoadBones(const aiMesh* assimp_mesh, const ModelParams& params);
        std::shared_ptr<Material> LoadMaterial(aiMaterial* assimp_material, const ModelParams& params);

        // Dependencies
        Context* m_context;
        World* m_world;
    };
}
