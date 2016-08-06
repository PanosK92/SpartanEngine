/*
Copyright(c) 2016 Panos Karabelas

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
#include "IComponent.h"
#include "../Math/Vector3.h"
#include <vector>
#include "../Core/Vertex.h"
#include "../Graphics/D3D11/D3D11Buffer.h"
#include "../Core/Mesh.h"
//========================================

class __declspec(dllexport) MeshFilter : public IComponent
{
public:
	MeshFilter();
	~MeshFilter();

	virtual void Initialize();
	virtual void Start();
	virtual void Remove();
	virtual void Update();
	virtual void Serialize();
	virtual void Deserialize();

	void CreateCube();
	void CreateQuad();
	void Set(std::string name, std::string rootGameObjectID, std::vector<VertexPositionTextureNormalTangent> vertices, std::vector<unsigned int> indices);
	bool SetBuffers() const;
	void Refresh();
	Directus::Math::Vector3 GetExtent() const;
	Directus::Math::Vector3 GetCenter() const;
	std::vector<VertexPositionTextureNormalTangent> GetVertices() const;
	std::vector<unsigned int> GetIndices() const;
	unsigned int GetVertexCount() const;
	unsigned int GetIndexCount() const;
	unsigned int GetTriangleCount() const;
	Mesh* GetMesh() const;

private:
	void CreateBuffers();

	D3D11Buffer* m_vertexBuffer;
	D3D11Buffer* m_indexBuffer;
	Mesh* m_meshData;
	Directus::Math::Vector3 m_min;
	Directus::Math::Vector3 m_max;
	Directus::Math::Vector3 m_extent;
	Directus::Math::Vector3 m_center;
};
