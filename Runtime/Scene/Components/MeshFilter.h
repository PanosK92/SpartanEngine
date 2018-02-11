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

//= INCLUDES ======================
#include "Component.h"
#include <vector>
#include <memory>
#include "../../Math/BoundingBox.h"
#include "../../Graphics/Vertex.h"
#include "../../Logging/Log.h"
//=================================

namespace Directus
{
	class Mesh;
	struct LoadVertices;

	namespace Math
	{
		class Vector3;
		class BoundingBox;
	}

	enum MeshType
	{
		MeshType_Custom,
		MeshType_Cube,
		MeshType_Quad
	};

	class ENGINE_CLASS MeshFilter : public Component
	{
	public:
		MeshFilter();
		~MeshFilter();

		//= ICOMPONENT ===============================
		void Serialize(FileStream* stream) override;
		void Deserialize(FileStream* stream) override;
		//============================================

		// Sets a mesh from memory
		void SetMesh(std::weak_ptr<Mesh> mesh){ m_mesh = mesh; }

		// Sets a default mesh (cube, quad)
		void SetMesh(MeshType defaultMesh);

		// Set vertex and index buffers (must be called before rendering)
		bool SetBuffers();

		//= BOUNDING BOX ===============================
		const Math::BoundingBox& GetBoundingBox() const;
		Math::BoundingBox GetBoundingBoxTransformed();
		//==============================================

		//= PROPERTIES ========================================
		MeshType GetMeshType() { return m_meshType; }
		std::string GetMeshName();
		const std::weak_ptr<Mesh>& GetMesh() { return m_mesh; }
		bool HasMesh() { return !m_mesh.expired(); }
		//=====================================================

	private:
		static void CreateCube(std::vector<VertexPosTexTBN>& vertices, std::vector<unsigned int>& indices);
		static void CreateQuad(std::vector<VertexPosTexTBN>& vertices, std::vector<unsigned int>& indices);

		// A weak reference to the mesh
		std::weak_ptr<Mesh> m_mesh;
		// Type of mesh
		MeshType m_meshType;
	};
}