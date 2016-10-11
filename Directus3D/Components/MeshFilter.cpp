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

//= INCLUDES ==================
#include "MeshFilter.h"
#include "Transform.h"
#include "../Core/Helper.h"
#include "../IO/Serializer.h"
#include "../IO/Log.h"
#include "../Core/GameObject.h"
#include "../Pools/MeshPool.h"
//=============================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

MeshFilter::MeshFilter()
{
	m_vertexBuffer = nullptr;
	m_indexBuffer = nullptr;
	m_mesh = nullptr;
}

MeshFilter::~MeshFilter()
{
	SafeDelete(m_vertexBuffer);
	SafeDelete(m_indexBuffer);
}

void MeshFilter::Initialize()
{
}

void MeshFilter::Start()
{

}

void MeshFilter::Remove()
{

}

void MeshFilter::Update()
{

}

void MeshFilter::Serialize()
{
	Serializer::WriteSTR(m_mesh ? m_mesh->GetID() : DATA_NOT_ASSIGNED);
}

void MeshFilter::Deserialize()
{
	string meshID = Serializer::ReadSTR();
	m_mesh = g_meshPool->GetMeshByID(meshID);

	if (!m_mesh)
	{
		LOG_INFO(meshID);
	}

	CreateBuffers();
}

// Use this to set a default engine mesh a cube, a sphere and so on.
void MeshFilter::SetDefaultMesh(DefaultMesh defaultMesh)
{
	switch (defaultMesh)
	{
	case Cube:
		m_mesh = g_meshPool->GetDefaultCube();
		break;
	case Quad:
		m_mesh = g_meshPool->GetDefaultQuad();
		break;
	default:
		m_mesh = nullptr;
		break;
	}

	CreateBuffers();
}

// Use this create a new mesh, this is what you might wanna call after loading a 3d model
void MeshFilter::Set(const string& name, const string& rootGameObjectID, const vector<VertexPositionTextureNormalTangent>& vertices, const vector<unsigned int>& indices)
{
	// Add the mesh data to the pool so it gets initialized properly
	m_mesh = g_meshPool->Add(name, rootGameObjectID, vertices, indices);

	// Make the mesh re-create the buffers whenever it updates.
	m_mesh->OnUpdate(std::bind(&MeshFilter::CreateBuffers, this));
}

// Set the buffers to active in the input assembler so they can be rendered.
bool MeshFilter::SetBuffers() const
{
	if (!m_vertexBuffer || !m_indexBuffer)
	{
		if (!m_vertexBuffer)
			LOG_WARNING("Can't set vertex buffer. GameObject \"" + g_gameObject->GetName() + "\" doesn't have an initialized vertex buffer.");

		if (!m_indexBuffer)
			LOG_WARNING("Can't set index buffer. GameObject \"" + g_gameObject->GetName() + "\" doesn't have an initialized index buffer.");

		return false;
	}

	m_vertexBuffer->SetIA();
	m_indexBuffer->SetIA();

	// Set the type of primitive that should be rendered from this vertex buffer
	g_graphicsDevice->GetDeviceContext()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	return true;
}

Vector3 MeshFilter::GetCenter() const
{
	return m_mesh ? m_mesh->GetCenter() * g_transform->GetWorldTransform() : Vector3::Zero;
}

Vector3 MeshFilter::GetBoundingBox() const
{
	return m_mesh ? m_mesh->GetBoundingBox() * g_transform->GetWorldTransform() : Vector3::One;
}

shared_ptr<Mesh> MeshFilter::GetMesh() const
{
	return m_mesh;
}

string MeshFilter::GetMeshName()
{
	return m_mesh ? m_mesh->GetName() : DATA_NOT_ASSIGNED;
}

void MeshFilter::CreateBuffers()
{
	SafeDelete(m_vertexBuffer);
	SafeDelete(m_indexBuffer);

	if (!m_mesh)
		return;

	m_vertexBuffer = new D3D11Buffer();
	m_vertexBuffer->Initialize(g_graphicsDevice);
	m_vertexBuffer->CreateVertexBuffer(m_mesh->GetVertices());

	m_indexBuffer = new D3D11Buffer();
	m_indexBuffer->Initialize(g_graphicsDevice);
	m_indexBuffer->CreateIndexBuffer(m_mesh->GetIndices());
}
