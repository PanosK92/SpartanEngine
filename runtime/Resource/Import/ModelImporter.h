/*
Copyright(c) 2016-2025 Panos Karabelas

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

//= INCLUDES ====
#include <string>
//===============

struct aiNode;
struct aiMesh;

namespace Spartan
{
    class Entity;
    class Mesh;

    class ModelImporter
    {
    public:
        static void Initialize();
        static bool Load(Mesh* mesh, const std::string& file_path);

    private:
        static void ParseNode(const aiNode* node, std::shared_ptr<Entity> parent_entity = nullptr);
        static void ParseNodeMeshes(const aiNode* node, std::shared_ptr<Entity> new_entity);
        static void ParseNodeLight(const aiNode* node, std::shared_ptr<Entity> new_entity);
        static void ParseAnimations();
        static void ParseMesh(aiMesh* mesh, std::shared_ptr<Entity> entity_parent);
        static void ParseNodes(const aiMesh* mesh);
    };
}
