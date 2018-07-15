/*
Copyright(c) 2016-2018 Panos Karabelas

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
#include "IComponent.h"
#include <vector>
#include "../../Rendering/RI/Backend_Def.h"
#include "../../Math/BoundingBox.h"
//=========================================

namespace Directus
{
	class Model;
	class Mesh;
	class Light;
	class Material;
	namespace Math
	{
		class Vector3;
	}

	enum GeometryType
	{
		Geometry_Custom,
		Geometry_Default_Cube,
		Geometry_Default_Quad,
		Geometry_Default_Sphere,
		Geometry_Default_Cylinder,
		Geometry_Default_Cone
	};

	class ENGINE_CLASS Renderable : public IComponent
	{
	public:
		Renderable(Context* context, Actor* actor, Transform* transform);
		~Renderable();

		//= ICOMPONENT ===============================
		void Serialize(FileStream* stream) override;
		void Deserialize(FileStream* stream) override;
		//============================================

		//= GEOMETRY ====================================================================================
		void Geometry_Set(
			const std::string& name,
			unsigned int indexOffset,
			unsigned int indexCount,
			unsigned int vertexOffset,
			unsigned int vertexCount,
			const Math::BoundingBox& AABB, 
			Model* model
		);
		void Geometry_Get(std::vector<unsigned int>* indices, std::vector<RI_Vertex_PosUVTBN>* vertices);
		void Geometry_Set(GeometryType type);
		unsigned int Geometry_IndexOffset()				{ return m_geometryIndexOffset; }
		unsigned int Geometry_IndexCount()				{ return m_geometryIndexCount; }		
		unsigned int Geometry_VertexOffset()			{ return m_geometryVertexOffset; }
		unsigned int Geometry_VertexCount()				{ return m_geometryVertexCount; }
		GeometryType Geometry_Type()					{ return m_geometryType; }
		const std::string& Geometry_Name()				{ return m_geometryName; }
		Model* Geometry_Model()							{ return m_model; }
		const Math::BoundingBox& Geometry_AABB() const	{ return m_geometryAABB; }
		Math::BoundingBox Geometry_BB();
		//===============================================================================================

		//= MATERIAL =========================================================================
		// Sets a material from memory (adds it to the resource cache by default)
		void Material_Set(const std::weak_ptr<Material>& materialWeak, bool autoCache = true);

		// Loads a material and the sets it
		std::weak_ptr<Material> Material_Set(const std::string& filePath);

		void Material_UseDefault();
		std::weak_ptr<Material> Material_RefWeak()	{ return m_materialRefWeak; }
		Material* Material_Ref()					{ return m_materialRef; }
		bool Material_Exists()						{ return !m_materialRefWeak.expired(); }
		std::string Material_Name();
		//====================================================================================

		//= PROPERTIES ===================================================================
		void SetCastShadows(bool castShadows)		{ m_castShadows = castShadows; }
		bool GetCastShadows()						{ return m_castShadows; }
		void SetReceiveShadows(bool receiveShadows) { m_receiveShadows = receiveShadows; }
		bool GetReceiveShadows()					{ return m_receiveShadows; }
		//================================================================================

	private:
		//= GEOMETRY =======================
		std::string m_geometryName;
		unsigned int m_geometryIndexOffset;
		unsigned int m_geometryIndexCount;
		unsigned int m_geometryVertexOffset;
		unsigned int m_geometryVertexCount;
		Math::BoundingBox m_geometryAABB;
		Model* m_model;
		GeometryType m_geometryType;
		//==================================

		//= MATERIAL =============================
		std::weak_ptr<Material> m_materialRefWeak;
		Material* m_materialRef;
		//========================================

		// Misc
		bool m_castShadows;
		bool m_receiveShadows;
		bool m_materialDefault;
	};
}
