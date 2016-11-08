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
#include "../IO/Serializer.h"
#include "../Core/GameObject.h"
#include "../Pools/MeshPool.h"
#include "../Logging/Log.h"
//=============================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

MeshFilter::MeshFilter()
{
	m_vertexBuffer = nullptr;
	m_indexBuffer = nullptr;
}

MeshFilter::~MeshFilter()
{
	m_vertexBuffer.reset();
	m_indexBuffer.reset();
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
	Serializer::WriteSTR(!m_mesh.expired() ? m_mesh.lock()->GetID() : (string)DATA_NOT_ASSIGNED);
}

void MeshFilter::Deserialize()
{
	m_mesh = g_context->GetSubsystem<MeshPool>()->GetMeshByID(Serializer::ReadSTR());
	CreateBuffers();
}

// Use this to set a default engine mesh a cube, a sphere and so on.
void MeshFilter::SetDefaultMesh(DefaultMesh defaultMesh)
{
	switch (defaultMesh)
	{
	case Cube:
		m_mesh = g_context->GetSubsystem<MeshPool>()->GetDefaultCube();
		break;
	case Quad:
		m_mesh = g_context->GetSubsystem<MeshPool>()->GetDefaultQuad();
		break;
	default:
		m_mesh = weak_ptr<Mesh>();
		break;
	}

	CreateBuffers();
}

// Use this create a new mesh, this is what you might wanna call after loading a 3d model
void MeshFilter::Set(const string& name, const string& rootGameObjectID, const vector<VertexPositionTextureNormalTangent>& vertices, const vector<unsigned int>& indices)
{
	// Add the mesh data to the pool so it gets initialized properly
	m_mesh = g_context->GetSubsystem<MeshPool>()->Add(name, rootGameObjectID, vertices, indices);

	// Make the mesh re-create the buffers whenever it updates.
	m_mesh.lock()->OnUpdate(std::bind(&MeshFilter::CreateBuffers, this));
}

// Set the buffers to active in the input assembler so they can be rendered.
bool MeshFilter::SetBuffers() const
{
	if (!m_vertexBuffer)
	{
		LOG_WARNING("Can't set vertex buffer. GameObject \"" + g_gameObject->GetName() + "\" doesn't have an initialized vertex buffer.");
		return false;
	}

	if (!m_indexBuffer)
	{
		LOG_WARNING("Can't set index buffer. GameObject \"" + g_gameObject->GetName() + "\" doesn't have an initialized index buffer.");
		return false;
	}

	m_vertexBuffer->SetIA();
	m_indexBuffer->SetIA();

	// Set the type of primitive that should be rendered from this vertex buffer
	g_context->GetSubsystem<Graphics>()->GetDeviceContext()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	return true;
}

Vector3 MeshFilter::GetCenter() const
{
	return !m_mesh.expired() ? m_mesh.lock()->GetCenter() * g_transform->GetTransformMatrix() : Vector3::Zero;
}

Vector3 MeshFilter::GetBoundingBox() const
{
	return !m_mesh.expired() ? m_mesh.lock()->GetBoundingBox() * g_transform->GetTransformMatrix() : Vector3::One;
}

weak_ptr<Mesh> MeshFilter::GetMesh() const
{
	return m_mesh;
}

string MeshFilter::GetMeshName()
{
	return !m_mesh.expired() ? m_mesh.lock()->GetName() : DATA_NOT_ASSIGNED;
}

void MeshFilter::CreateBuffers()
{
	m_vertexBuffer.reset();
	m_indexBuffer.reset();

	if (m_mesh.expired())
		return;

	m_vertexBuffer = make_shared<D3D11Buffer>();
	m_vertexBuffer->Initialize(g_context->GetSubsystem<Graphics>());
	m_vertexBuffer->CreateVertexBuffer(m_mesh.lock()->GetVertices());

	m_indexBuffer = make_shared<D3D11Buffer>();
	m_indexBuffer->Initialize(g_context->GetSubsystem<Graphics>());
	m_indexBuffer->CreateIndexBuffer(m_mesh.lock()->GetIndices());
}
