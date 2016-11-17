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
#include "../Graphics/Vertex.h"
#include "../Graphics/D3D11/D3D11Buffer.h"
#include "../Graphics/Mesh.h"
#include <memory>
//========================================

enum DefaultMesh { Cube, Quad };

class __declspec(dllexport) MeshFilter : public IComponent
{
public:
	MeshFilter();
	~MeshFilter();

	virtual void Awake();
	virtual void Start();
	virtual void Remove();
	virtual void Update();
	virtual void Serialize();
	virtual void Deserialize();

	void SetDefaultMesh(DefaultMesh defaultMesh);
	void Set(const std::string& name, const std::string& rootGameObjectID, const std::vector<VertexPositionTextureNormalTangent>& vertices, const std::vector<unsigned int>& indices);
	bool SetBuffers() const;
	Directus::Math::Vector3 GetCenter() const;
	Directus::Math::Vector3 GetBoundingBox() const;
	std::weak_ptr<Mesh> GetMesh();
	bool HasMesh();
	std::string GetMeshName();

private:
	void CreateBuffers();

	std::shared_ptr<D3D11Buffer> m_vertexBuffer;
	std::shared_ptr<D3D11Buffer> m_indexBuffer;
	std::weak_ptr<Mesh> m_mesh;
};
