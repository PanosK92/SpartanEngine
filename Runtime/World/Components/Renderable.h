/*
Copyright(c) 2016-2019 Panos Karabelas

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

//= INCLUDES ========================
#include "IComponent.h"
#include <vector>
#include "../../Math/BoundingBox.h"
//===================================

namespace Spartan
{
	class Model;
	class Mesh;
	class Light;
	class Material;
	namespace Math
	{
		class Vector3;
	}

	enum Geometry_Type
	{
		Geometry_Custom,
		Geometry_Default_Cube,
		Geometry_Default_Quad,
		Geometry_Default_Sphere,
		Geometry_Default_Cylinder,
		Geometry_Default_Cone
	};

	class SPARTAN_CLASS Renderable : public IComponent
	{
	public:
		Renderable(Context* context, Entity* entity, Transform* transform);
		~Renderable() = default;

		//= ICOMPONENT ===============================
		void Serialize(FileStream* stream) override;
		void Deserialize(FileStream* stream) override;
		//============================================

		//= GEOMETRY =============================================================================================
		void GeometrySet(
			const std::string& name,
			unsigned int index_offset,
			unsigned int index_count,
			unsigned int vertex_offset,
			unsigned int vertex_count,
			const Math::BoundingBox& aabb, 
			std::shared_ptr<Model>& model
		);
		void GeometryGet(std::vector<unsigned int>* indices, std::vector<RHI_Vertex_PosTexNorTan>* vertices) const;
		void GeometrySet(Geometry_Type type);
		unsigned int GeometryIndexOffset() const		{ return m_geometryIndexOffset; }
		unsigned int GeometryIndexCount() const			{ return m_geometryIndexCount; }		
		unsigned int GeometryVertexOffset() const		{ return m_geometryVertexOffset; }
		unsigned int GeometryVertexCount() const		{ return m_geometryVertexCount; }
		Geometry_Type GeometryType() const				{ return m_geometry_type; }
		const std::string& GeometryName() const			{ return m_geometryName; }
		std::shared_ptr<Model> GeometryModel() const	{ return m_model; }
		const Math::BoundingBox& GeometryAabb() const	{ return m_geometryAABB; }
		Math::BoundingBox GeometryAabb();
		//========================================================================================================

		//= MATERIAL ============================================================
		// Sets a material from memory (adds it to the resource cache by default)
		void MaterialSet(const std::shared_ptr<Material>& material);

		// Loads a material and the sets it
		std::shared_ptr<Material> MaterialSet(const std::string& file_path);

		void MaterialUseDefault();
		const std::string& MaterialName();
		auto MaterialPtr() const	{ return m_material; }
		bool MaterialExists() const { return m_material != nullptr; }
		//=======================================================================

		//= PROPERTIES ============================================================================
		void SetCastShadows(const bool cast_shadows)		{ m_castShadows = cast_shadows; }
		bool GetCastShadows() const							{ return m_castShadows; }
		void SetReceiveShadows(const bool receive_shadows)	{ m_receiveShadows = receive_shadows; }
		bool GetReceiveShadows() const						{ return m_receiveShadows; }
		//=========================================================================================

	private:
		//= GEOMETRY =======================
		std::string m_geometryName;
		unsigned int m_geometryIndexOffset;
		unsigned int m_geometryIndexCount;
		unsigned int m_geometryVertexOffset;
		unsigned int m_geometryVertexCount;
		Math::BoundingBox m_geometryAABB;
		std::shared_ptr<Model> m_model;
		Geometry_Type m_geometry_type;
		//==================================

		//= MATERIAL ========================
		std::shared_ptr<Material> m_material;
		//===================================

		// Misc
		bool m_castShadows;
		bool m_receiveShadows;
		bool m_materialDefault;
	};
}
