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

//= INCLUDES ===========================
#include "MeshFilter.h"
#include "Transform.h"
#include "../IO/Serializer.h"
#include "../Core/GameObject.h"
#include "../Logging/Log.h"
#include "../FileSystem/FileSystem.h"
#include "../Resource/ResourceManager.h"
//======================================

//= NAMESPACES ================
using namespace std;
using namespace Directus;
using namespace Directus::Math;
//=============================

MeshFilter::MeshFilter()
{
	m_vertexBuffer = nullptr;
	m_indexBuffer = nullptr;
	m_meshType = Imported;
}

MeshFilter::~MeshFilter()
{
	m_vertexBuffer.reset();
	m_indexBuffer.reset();
}

void MeshFilter::Reset()
{
}

void MeshFilter::Start()
{

}

void MeshFilter::OnDisable()
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
	Serializer::WriteInt((int)m_meshType);
	Serializer::WriteSTR(!m_mesh.expired() ? m_mesh.lock()->GetResourceID() : (string)DATA_NOT_ASSIGNED);
}

void MeshFilter::Deserialize()
{
	m_meshType = (MeshType)Serializer::ReadInt();
	string meshID = Serializer::ReadSTR();

	if (m_meshType == Imported) // Get the already loaded mesh
	{
		auto mesh = g_context->GetSubsystem<ResourceManager>()->GetResourceByID<Mesh>(meshID);
		SetMesh(mesh);
	}
	else // Construct the mesh
		SetMesh(m_meshType);
}

// Sets a mesh from memory (all other set/create functions resolve to this one)
void MeshFilter::SetMesh(weak_ptr<Mesh> mesh)
{
	m_mesh = mesh;

	// Make the mesh re-create the buffers whenever it updates.
	if (!m_mesh.expired())
		m_mesh.lock()->OnUpdate(std::bind(&MeshFilter::CreateBuffers, this));

	CreateBuffers();
}

// Sets a default mesh (cube, quad)
void MeshFilter::SetMesh(MeshType defaultMesh)
{
	auto meshSharedPtr = make_shared<Mesh>(g_context);
	vector<VertexPositionTextureNormalTangent> vertices;
	vector<unsigned int> indices;

	switch (defaultMesh)
	{
	case Cube:
		CreateCube(vertices, indices);
		meshSharedPtr = make_shared<Mesh>(g_context);
		meshSharedPtr->SetResourceName("Cube");
		meshSharedPtr->SetVertices(vertices);
		meshSharedPtr->SetIndices(indices);
		meshSharedPtr->Update();
		m_meshType = Cube;
		break;

	case Quad:
		CreateQuad(vertices, indices);
		meshSharedPtr = make_shared<Mesh>(g_context);
		meshSharedPtr->SetResourceName("Quad");
		meshSharedPtr->SetVertices(vertices);
		meshSharedPtr->SetIndices(indices);
		meshSharedPtr->Update();
		m_meshType = Quad;
		break;

	default:
		m_mesh = weak_ptr<Mesh>();
		break;
	}

	auto meshWeakPtr = g_context->GetSubsystem<ResourceManager>()->Add(move(meshSharedPtr));
	SetMesh(meshWeakPtr);

	vertices.clear();
	indices.clear();
}

// Creates a mesh from raw vertex/index data and sets it
void MeshFilter::CreateAndSet(const string& name, const string& rootGameObjectID, const vector<VertexPositionTextureNormalTangent>& vertices, const vector<unsigned int>& indices)
{
	// Create a mesh
	auto mesh = make_shared<Mesh>(g_context);
	mesh->SetResourceName(name);
	mesh->SetRootGameObjectID(rootGameObjectID);
	mesh->SetVertices(vertices);
	mesh->SetIndices(indices);
	mesh->Update();

	// Save it and set it
	SetMesh(g_context->GetSubsystem<ResourceManager>()->Add(mesh));
}

// Set the buffers to active in the input assembler so they can be rendered.
bool MeshFilter::SetBuffers()
{
	if (!m_vertexBuffer)
		LOG_WARNING("Can't set vertex buffer. Mesh \"" + GetMeshName() + "\" doesn't have an initialized vertex buffer \"" + g_gameObject->GetName() + "\".");

	if (!m_indexBuffer)
		LOG_WARNING("Can't set index buffer. Mesh \"" + GetMeshName() + "\" doesn't have an initialized index buffer \"" + g_gameObject->GetName() + "\".");

	if (!m_vertexBuffer || !m_indexBuffer)
		return false;

	m_vertexBuffer->SetIA();
	m_indexBuffer->SetIA();

	// Set the type of primitive that should be rendered from this vertex buffer
	g_context->GetSubsystem<D3D11GraphicsDevice>()->SetPrimitiveTopology(TriangleList);

	return true;
}

Vector3 MeshFilter::GetCenter()
{
	return !m_mesh.expired() ? m_mesh.lock()->GetCenter() * g_transform->GetWorldTransform() : Vector3::Zero;
}

Vector3 MeshFilter::GetBoundingBox()
{
	return !m_mesh.expired() ? m_mesh.lock()->GetBoundingBox() * g_transform->GetWorldTransform() : Vector3::One;
}

weak_ptr<Mesh> MeshFilter::GetMesh()
{
	return m_mesh;
}

bool MeshFilter::HasMesh()
{
	return m_mesh.expired() ? false : true;
}

string MeshFilter::GetMeshName()
{
	return !m_mesh.expired() ? m_mesh.lock()->GetResourceName() : DATA_NOT_ASSIGNED;
}

bool MeshFilter::CreateBuffers()
{
	m_vertexBuffer.reset();
	m_indexBuffer.reset();

	if (m_mesh.expired())
		return false;

	auto graphicsDevice = g_context->GetSubsystem<D3D11GraphicsDevice>();

	m_vertexBuffer = make_shared<D3D11VertexBuffer>(graphicsDevice);
	if (!m_vertexBuffer->Create(m_mesh.lock()->GetVertices()))
	{
		LOG_ERROR("Failed to create vertex buffer [" + g_gameObject->GetName() + "].");
		return false;
	}

	m_indexBuffer = make_shared<D3D11IndexBuffer>(graphicsDevice);
	if (!m_indexBuffer->Create(m_mesh.lock()->GetIndices()))
	{
		LOG_ERROR("Failed to create index buffer [" + g_gameObject->GetName() + "].");
		return false;
	}

	return true;
}

void MeshFilter::CreateCube(vector<VertexPositionTextureNormalTangent>& vertices, vector<unsigned int>& indices)
{
	// front
	vertices.push_back({ Vector3(-0.5f, -0.5f, -0.5f), Vector2(0, 1), Vector3(0, 0, -1), Vector3(0, 1, 0) }); // 0
	vertices.push_back({ Vector3(-0.5f, 0.5f, -0.5f), Vector2(0, 0), Vector3(0, 0, -1), Vector3(0, 1, 0) }); // 1
	vertices.push_back({ Vector3(0.5f, -0.5f, -0.5f), Vector2(1, 1), Vector3(0, 0, -1), Vector3(0, 1, 0) }); // 2
	vertices.push_back({ Vector3(0.5f, 0.5f, -0.5f), Vector2(1, 0), Vector3(0, 0, -1), Vector3(0, 1, 0) }); // 3

	// bottom
	vertices.push_back({ Vector3(-0.5f, -0.5f, 0.5f), Vector2(0, 1), Vector3(0, -1, 0), Vector3(1, 0, 0) }); // 4
	vertices.push_back({ Vector3(-0.5f, -0.5f, -0.5f), Vector2(0, 0), Vector3(0, -1, 0), Vector3(1, 0, 0) }); // 5
	vertices.push_back({ Vector3(0.5f, -0.5f, 0.5f), Vector2(1, 1), Vector3(0, -1, 0), Vector3(1, 0, 0) }); // 6
	vertices.push_back({ Vector3(0.5f, -0.5f, -0.5f), Vector2(1, 0), Vector3(0, -1, 0), Vector3(1, 0, 0) }); // 7

	// back
	vertices.push_back({ Vector3(-0.5f, -0.5f, 0.5f), Vector2(1, 1), Vector3(0, 0, 1), Vector3(0, 1, 0) }); // 8
	vertices.push_back({ Vector3(-0.5f, 0.5f, 0.5f), Vector2(1, 0), Vector3(0, 0, 1), Vector3(0, 1, 0) }); // 9
	vertices.push_back({ Vector3(0.5f, -0.5f, 0.5f), Vector2(0, 1), Vector3(0, 0, 1), Vector3(0, 1, 0) }); // 10
	vertices.push_back({ Vector3(0.5f, 0.5f, 0.5f), Vector2(0, 0), Vector3(0, 0, 1), Vector3(0, 1, 0) }); // 11

	// top
	vertices.push_back({ Vector3(-0.5f, 0.5f, 0.5f), Vector2(0, 0), Vector3(0, 1, 0), Vector3(1, 0, 0) }); // 12
	vertices.push_back({ Vector3(-0.5f, 0.5f, -0.5f), Vector2(0, 1), Vector3(0, 1, 0), Vector3(1, 0, 0) }); // 13
	vertices.push_back({ Vector3(0.5f, 0.5f, 0.5f), Vector2(1, 0), Vector3(0, 1, 0), Vector3(1, 0, 0) }); // 14
	vertices.push_back({ Vector3(0.5f, 0.5f, -0.5f), Vector2(1, 1), Vector3(0, 1, 0), Vector3(1, 0, 0) }); // 15

	// left
	vertices.push_back({ Vector3(-0.5f, -0.5f, 0.5f), Vector2(0, 1), Vector3(-1, 0, 0), Vector3(0, 1, 0) }); // 16
	vertices.push_back({ Vector3(-0.5f, 0.5f, 0.5f), Vector2(0, 0), Vector3(-1, 0, 0), Vector3(0, 1, 0) }); // 17
	vertices.push_back({ Vector3(-0.5f, -0.5f, -0.5f), Vector2(1, 1), Vector3(-1, 0, 0), Vector3(0, 1, 0) }); // 18
	vertices.push_back({ Vector3(-0.5f, 0.5f, -0.5f), Vector2(1, 0), Vector3(-1, 0, 0), Vector3(0, 1, 0) }); // 19

	// right
	vertices.push_back({ Vector3(0.5f, -0.5f, 0.5f), Vector2(1, 1), Vector3(1, 0, 0), Vector3(0, 1, 0) }); // 20
	vertices.push_back({ Vector3(0.5f, 0.5f, 0.5f), Vector2(1, 0), Vector3(1, 0, 0), Vector3(0, 1, 0) }); // 21
	vertices.push_back({ Vector3(0.5f, -0.5f, -0.5f), Vector2(0, 1), Vector3(1, 0, 0), Vector3(0, 1, 0) }); // 22
	vertices.push_back({ Vector3(0.5f, 0.5f, -0.5f), Vector2(0, 0), Vector3(1, 0, 0), Vector3(0, 1, 0) }); // 23

	// front
	indices.push_back(0);
	indices.push_back(1);
	indices.push_back(2);
	indices.push_back(2);
	indices.push_back(1);
	indices.push_back(3);

	// bottom
	indices.push_back(4);
	indices.push_back(5);
	indices.push_back(6);
	indices.push_back(6);
	indices.push_back(5);
	indices.push_back(7);

	// back
	indices.push_back(10);
	indices.push_back(9);
	indices.push_back(8);
	indices.push_back(11);
	indices.push_back(9);
	indices.push_back(10);

	// top
	indices.push_back(14);
	indices.push_back(13);
	indices.push_back(12);
	indices.push_back(15);
	indices.push_back(13);
	indices.push_back(14);

	// left
	indices.push_back(16);
	indices.push_back(17);
	indices.push_back(18);
	indices.push_back(18);
	indices.push_back(17);
	indices.push_back(19);

	// left
	indices.push_back(22);
	indices.push_back(21);
	indices.push_back(20);
	indices.push_back(23);
	indices.push_back(21);
	indices.push_back(22);
}

void MeshFilter::CreateQuad(vector<VertexPositionTextureNormalTangent>& vertices, vector<unsigned int>& indices)
{
	vertices.push_back({ Vector3(-0.5f, 0.0f, 0.5f),Vector2(0, 0), Vector3(0, 1, 0), Vector3(1, 0, 0) }); // 0 top-left
	vertices.push_back({ Vector3(0.5f, 0.0f, 0.5f), Vector2(1, 0), Vector3(0, 1, 0), Vector3(1, 0, 0) }); // 1 top-right
	vertices.push_back({ Vector3(-0.5f, 0.0f, -0.5f), Vector2(0, 1), Vector3(0, 1, 0), Vector3(1, 0, 0) }); // 2 bottom-left
	vertices.push_back({ Vector3(0.5f, 0.0f, -0.5f),Vector2(1, 1), Vector3(0, 1, 0), Vector3(1, 0, 0) }); // 3 bottom-right

	indices.push_back(3);
	indices.push_back(2);
	indices.push_back(0);
	indices.push_back(3);
	indices.push_back(0);
	indices.push_back(1);
}