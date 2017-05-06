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

//= INCLUDES ===================================
#include "IComponent.h"
#include "../Math/Vector3.h"
#include <vector>
#include "../Graphics/Vertex.h"
#include "../Graphics/Mesh.h"
#include <memory>
#include "../Graphics/D3D11/D3D11VertexBuffer.h"
#include "../Graphics/D3D11/D3D11IndexBuffer.h"
//==============================================

namespace Directus
{
	class DllExport MeshFilter : public IComponent
	{
	public:
		enum MeshType { Imported, Cube, Quad };

		MeshFilter();
		~MeshFilter();

		// Resource Interface
		virtual void Reset();
		virtual void Start();
		virtual void OnDisable();
		virtual void Remove();
		virtual void Update();
		virtual void Serialize();
		virtual void Deserialize();

		// Sets a mesh from memory
		void SetMesh(std::weak_ptr<Mesh> mesh);
		// Sets a default mesh (cube, quad)
		void SetMesh(MeshType defaultMesh);
		// Creates a mesh from raw vertex/index data and sets it
		void CreateAndSet(const std::string& name, const std::string& rootGameObjectID, const std::vector<VertexPosTexNorTan>& vertices, const std::vector<unsigned int>& indices);
		// Sets the meshe's buffers
		bool SetBuffers();

		// Normalizes the scale of the entire model's hierarchy
		void NormalizeModelScale();
		// Returns all meshes that belong to the model the mesh filter is part of
		std::vector<std::weak_ptr<Mesh>> GetAllModelMeshes();
		// Returns a value that can be used (by multiplying against the original scale)
		// to normalize the scale of a transform
		float GetNormalizedModelScale();
		// Sets the scale for the entire model's hierarchy
		void SetModelScale(float scale);
		// Returns the mesh with the largest bounding box
		static std::weak_ptr<Mesh> GetLargestBoundingBox(std::vector<std::weak_ptr<Mesh>> meshes);

		// Properties
		Directus::Math::Vector3 GetCenter();
		Directus::Math::Vector3 GetBoundingBox();
		std::weak_ptr<Mesh> GetMesh();
		bool HasMesh();
		std::string GetMeshName();

	private:
		bool CreateBuffers();
		void CreateCube(std::vector<VertexPosTexNorTan>& vertices, std::vector<unsigned int>& indices);
		void CreateQuad(std::vector<VertexPosTexNorTan>& vertices, std::vector<unsigned int>& indices);

		std::shared_ptr<D3D11VertexBuffer> m_vertexBuffer;
		std::shared_ptr<D3D11IndexBuffer> m_indexBuffer;
		std::weak_ptr<Mesh> m_mesh;
		MeshType m_meshType;
	};
}