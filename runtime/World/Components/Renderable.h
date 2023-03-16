/*
Copyright(c) 2016-2023 Panos Karabelas

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

//= INCLUDES ======================
#include "IComponent.h"
#include <vector>
#include "../../Math/BoundingBox.h"
#include "../../Math/Matrix.h"
//=================================

namespace Spartan
{
    class Mesh;
    class Mesh;
    class Light;
    class Material;
    namespace Math
    {
        class Vector3;
    }

    enum class DefaultGeometry
    {
        Undefined,
        Cube,
        Quad,
        Sphere,
        Cylinder,
        Cone
    };

    class SP_CLASS Renderable : public IComponent
    {
    public:
        Renderable(Entity* entity, uint64_t id = 0);
        ~Renderable() = default;

        // IComponent
        void Serialize(FileStream* stream) override;
        void Deserialize(FileStream* stream) override;

        // Set geometry
        void SetGeometry(
            const std::string& name,
            uint32_t index_offset,
            uint32_t index_count,
            uint32_t vertex_offset,
            uint32_t vertex_count,
            const Math::BoundingBox& aabb, 
            Mesh* mesh
        );
        void SetGeometry(DefaultGeometry type);

        // Get geometry
        void GetGeometry(std::vector<uint32_t>* indices, std::vector<RHI_Vertex_PosTexNorTan>* vertices) const;

        // Properties
        uint32_t GetIndexOffset()                 const { return m_geometry_index_offset; }
        uint32_t GetIndexCount()                  const { return m_geometry_index_count; }
        uint32_t GetVertexOffset()                const { return m_geometry_vertex_offset; }
        uint32_t GetVertexCount()                 const { return m_geometry_vertex_count; }
        DefaultGeometry GetGeometryType()         const { return m_geometry_type; }
        const std::string& GetGeometryName()      const { return m_geometry_name; }
        Mesh* GetMesh()                           const { return m_mesh; }
        const Math::BoundingBox& GetBoundingBox() const { return m_bounding_box; }
        const Math::BoundingBox& GetAabb();
        void Clear();

        //= MATERIAL ====================================================================
        // Sets a material from memory (adds it to the resource cache by default)
        std::shared_ptr<Material> SetMaterial(const std::shared_ptr<Material>& material);

        // Loads a material and the sets it
        std::shared_ptr<Material> SetMaterial(const std::string& file_path);

        void SetDefaultMaterial();
        std::string GetMaterialName() const;
        Material* GetMaterial()       const { return m_material; }
        auto HasMaterial()            const { return m_material != nullptr; }
        //===============================================================================

        // Shadows
        void SetCastShadows(const bool cast_shadows) { m_cast_shadows = cast_shadows; }
        auto GetCastShadows() const                  { return m_cast_shadows; }

    private:
        uint32_t m_geometry_index_offset  = 0;
        uint32_t m_geometry_index_count   = 0;
        uint32_t m_geometry_vertex_offset = 0;
        uint32_t m_geometry_vertex_count  = 0;
        DefaultGeometry m_geometry_type   = DefaultGeometry::Undefined;
        Math::Matrix m_last_transform     = Math::Matrix::Identity;
        bool m_cast_shadows               = true;
        bool m_material_default           = false;
        Mesh* m_mesh                      = nullptr;
        Material* m_material              = nullptr;
        Math::BoundingBox m_bounding_box;
        Math::BoundingBox m_aabb;
        std::string m_geometry_name;
    };
}
