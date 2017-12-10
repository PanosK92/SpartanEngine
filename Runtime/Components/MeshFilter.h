/*
Copyright(c) 2016-2017 Panos Karabelas

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

//= INCLUDES ===================
#include "Component.h"
#include <vector>
#include <memory>
#include "../Math/BoundingBox.h"
#include "../Graphics/Vertex.h"
//==============================

namespace Directus
{
	class Mesh;
	struct LoadVertices;

	namespace Math
	{
		class Vector3;
		class BoundingBox;
	}

	class DLL_API MeshFilter : public Component
	{
	public:
		enum MeshType { Imported, Cube, Quad };

		MeshFilter();
		~MeshFilter();

		//= ICOMPONENT ============================
		void Initialize() override;
		void Start() override;
		void OnDisable() override;
		void Remove() override;
		void Update() override;
		void Serialize(StreamIO* stream) override;
		void Deserialize(StreamIO* stream) override;
		//=========================================

		// Sets a mesh from memory
		void SetMesh(std::weak_ptr<Mesh> mesh) { m_mesh = mesh; }

		// Sets a default mesh (cube, quad)
		void SetMesh(MeshType defaultMesh);

		// Set the buffers to active in the input assembler so they can be rendered.
		bool SetBuffers();

		//= BOUNDING BOX ===============================
		const Math::BoundingBox& GetBoundingBox() const;
		Math::BoundingBox GetBoundingBoxTransformed();
		//==============================================

		//= PROPERTIES ===========================================
		std::string GetMeshName();
		const std::weak_ptr<Mesh>& GetMesh() { return m_mesh; }
		bool HasMesh() { return m_mesh.expired() ? false : true; }
		//========================================================

	private:
		static void CreateCube(std::vector<VertexPosTexTBN>& vertices, std::vector<unsigned int>& indices);
		static void CreateQuad(std::vector<VertexPosTexTBN>& vertices, std::vector<unsigned int>& indices);
		std::string GetGameObjectName();

		std::weak_ptr<Mesh> m_mesh;
		MeshType m_meshType;
	};
}