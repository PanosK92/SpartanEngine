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
#include "../Core/Globals.h"
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
	m_meshData = nullptr;
	m_min = Vector3::Infinity;
	m_max = Vector3::InfinityNeg;
	m_extent = Vector3::One;
	m_center = Vector3::Zero;
}

MeshFilter::~MeshFilter()
{
	DirectusSafeDelete(m_vertexBuffer);
	DirectusSafeDelete(m_indexBuffer);
}

void MeshFilter::Initialize()
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
	Serializer::SaveSTR(m_meshData ? m_meshData->ID : "-1");
	Serializer::SaveVector3(m_min);
	Serializer::SaveVector3(m_max);
	Serializer::SaveVector3(m_extent);
	Serializer::SaveVector3(m_center);
}

void MeshFilter::Deserialize()
{
	string meshDataID = Serializer::LoadSTR();
	m_meshData = g_meshPool->GetMesh(meshDataID);
	m_min = Serializer::LoadVector3();
	m_max = Serializer::LoadVector3();
	m_extent = Serializer::LoadVector3();
	m_center = Serializer::LoadVector3();

	Refresh();
}

void MeshFilter::CreateCube()
{
	vector<VertexPositionTextureNormalTangent> vertices;

	// front
	vertices.push_back({Vector3(-0.5f, -0.5f, -0.5f), Vector2(0, 1), Vector3(0, 0, -1), Vector3(0, 1, 0)}); // 0
	vertices.push_back({Vector3(-0.5f, 0.5f, -0.5f), Vector2(0, 0), Vector3(0, 0, -1), Vector3(0, 1, 0)}); // 1
	vertices.push_back({Vector3(0.5f, -0.5f, -0.5f), Vector2(1, 1), Vector3(0, 0, -1), Vector3(0, 1, 0)}); // 2
	vertices.push_back({Vector3(0.5f, 0.5f, -0.5f), Vector2(1, 0), Vector3(0, 0, -1), Vector3(0, 1, 0)}); // 3

	// bottom
	vertices.push_back({Vector3(-0.5f, -0.5f, 0.5f), Vector2(0, 1), Vector3(0, -1, 0), Vector3(1, 0, 0)}); // 4
	vertices.push_back({Vector3(-0.5f, -0.5f, -0.5f), Vector2(0, 0), Vector3(0, -1, 0), Vector3(1, 0, 0)}); // 5
	vertices.push_back({Vector3(0.5f, -0.5f, 0.5f), Vector2(1, 1), Vector3(0, -1, 0), Vector3(1, 0, 0)}); // 6
	vertices.push_back({Vector3(0.5f, -0.5f, -0.5f), Vector2(1, 0), Vector3(0, -1, 0), Vector3(1, 0, 0)}); // 7

	// back
	vertices.push_back({Vector3(-0.5f, -0.5f, 0.5f), Vector2(1, 1), Vector3(0, 0, 1), Vector3(0, 1, 0)}); // 8
	vertices.push_back({Vector3(-0.5f, 0.5f, 0.5f), Vector2(1, 0), Vector3(0, 0, 1), Vector3(0, 1, 0)}); // 9
	vertices.push_back({Vector3(0.5f, -0.5f, 0.5f), Vector2(0, 1), Vector3(0, 0, 1), Vector3(0, 1, 0)}); // 10
	vertices.push_back({Vector3(0.5f, 0.5f, 0.5f), Vector2(0, 0), Vector3(0, 0, 1), Vector3(0, 1, 0)}); // 11

	// top
	vertices.push_back({Vector3(-0.5f, 0.5f, 0.5f), Vector2(0, 0), Vector3(0, 1, 0), Vector3(1, 0, 0)}); // 12
	vertices.push_back({Vector3(-0.5f, 0.5f, -0.5f), Vector2(0, 1), Vector3(0, 1, 0), Vector3(1, 0, 0)}); // 13
	vertices.push_back({Vector3(0.5f, 0.5f, 0.5f), Vector2(1, 0), Vector3(0, 1, 0), Vector3(1, 0, 0)}); // 14
	vertices.push_back({Vector3(0.5f, 0.5f, -0.5f), Vector2(1, 1), Vector3(0, 1, 0), Vector3(1, 0, 0)}); // 15

	// left
	vertices.push_back({Vector3(-0.5f, -0.5f, 0.5f), Vector2(0, 1), Vector3(-1, 0, 0), Vector3(0, 1, 0)}); // 16
	vertices.push_back({Vector3(-0.5f, 0.5f, 0.5f), Vector2(0, 0), Vector3(-1, 0, 0), Vector3(0, 1, 0)}); // 17
	vertices.push_back({Vector3(-0.5f, -0.5f, -0.5f), Vector2(1, 1), Vector3(-1, 0, 0), Vector3(0, 1, 0)}); // 18
	vertices.push_back({Vector3(-0.5f, 0.5f, -0.5f), Vector2(1, 0), Vector3(-1, 0, 0), Vector3(0, 1, 0)}); // 19

	// right
	vertices.push_back({Vector3(0.5f, -0.5f, 0.5f), Vector2(1, 1), Vector3(1, 0, 0), Vector3(0, 1, 0)}); // 20
	vertices.push_back({Vector3(0.5f, 0.5f, 0.5f), Vector2(1, 0), Vector3(1, 0, 0), Vector3(0, 1, 0)}); // 21
	vertices.push_back({Vector3(0.5f, -0.5f, -0.5f), Vector2(0, 1), Vector3(1, 0, 0), Vector3(0, 1, 0)}); // 22
	vertices.push_back({Vector3(0.5f, 0.5f, -0.5f), Vector2(0, 0), Vector3(1, 0, 0), Vector3(0, 1, 0)}); // 23

	vector<unsigned int> indices;

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

	Set("Cube", g_transform->GetRoot()->GetGameObject()->GetID(), vertices, indices);
}

void MeshFilter::CreateQuad()
{
	vector<VertexPositionTextureNormalTangent> vertices;
	vertices.push_back({Vector3(-0.5f, 0.0f, 0.5f),Vector2(0, 0), Vector3(0, 1, 0), Vector3(1, 0, 0)}); // 0 top-left
	vertices.push_back({Vector3(0.5f, 0.0f, 0.5f), Vector2(1, 0), Vector3(0, 1, 0), Vector3(1, 0, 0)}); // 1 top-right
	vertices.push_back({Vector3(-0.5f, 0.0f, -0.5f), Vector2(0, 1), Vector3(0, 1, 0), Vector3(1, 0, 0)}); // 2 bottom-left
	vertices.push_back({Vector3(0.5f, 0.0f, -0.5f),Vector2(1, 1), Vector3(0, 1, 0), Vector3(1, 0, 0)}); // 3 bottom-right

	vector<unsigned int> indices;
	indices.push_back(3);
	indices.push_back(2);
	indices.push_back(0);
	indices.push_back(3);
	indices.push_back(0);
	indices.push_back(1);

	Set("Quad", g_transform->GetRoot()->GetGameObject()->GetID(), vertices, indices);
}

void MeshFilter::Set(string name, string rootGameObjectID, vector<VertexPositionTextureNormalTangent> vertices, vector<unsigned int> indices)
{
	// Add the mesh data to the pool so it gets initialized properly
	m_meshData = g_meshPool->AddMesh(name, rootGameObjectID, g_gameObject->GetID(), vertices, indices);

	Refresh();
}

// Set the buffers to active in the input assembler so they can be rendered.
bool MeshFilter::SetBuffers()
{
	if (!m_vertexBuffer || !m_indexBuffer)
	{
		LOG("GameObject \"" + g_gameObject->GetName() + "\" mesh buffer haven't been initialized. They can't be set.");
		return false;
	}

	m_vertexBuffer->SetIA();
	m_indexBuffer->SetIA();

	// Set the type of primitive that should be rendered from this vertex buffer
	g_graphicsDevice->GetDeviceContext()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	return true;
}

// Calculates the min, max, extent, center and re-creates the buffers
void MeshFilter::Refresh()
{
	g_meshPool->GetMinMax(m_meshData, m_min, m_max);
	m_extent = g_meshPool->GetMeshExtent(m_min, m_max);
	m_center = g_meshPool->GetMeshCenter(m_min, m_max);

	CreateBuffers();
}

// Returns the bounding box of the mesh
Vector3 MeshFilter::GetExtent()
{
	return m_extent;
}

// Returns the center of the mesh
Vector3 MeshFilter::GetCenter()
{
	return m_center;
}

vector<VertexPositionTextureNormalTangent> MeshFilter::GetVertices()
{
	return m_meshData ? m_meshData->vertices : vector<VertexPositionTextureNormalTangent>();
}

vector<unsigned int> MeshFilter::GetIndices()
{
	return m_meshData ? m_meshData->indices : vector<unsigned int>();
}

unsigned int MeshFilter::GetVertexCount()
{
	return m_meshData ? m_meshData->vertexCount : 0;
}

unsigned int MeshFilter::GetIndexCount()
{
	return m_meshData ? m_meshData->indexCount : 0;
}

unsigned int MeshFilter::GetFaceCount()
{
	return m_meshData ? m_meshData->faceCount : 0;
}

Mesh* MeshFilter::GetMesh()
{
	return m_meshData;
}

void MeshFilter::CreateBuffers()
{
	DirectusSafeDelete(m_vertexBuffer);
	DirectusSafeDelete(m_indexBuffer);

	m_vertexBuffer = new D3D11Buffer();
	m_vertexBuffer->Initialize(g_graphicsDevice);
	m_vertexBuffer->CreateVertexBuffer(GetVertices());

	m_indexBuffer = new D3D11Buffer();
	m_indexBuffer->Initialize(g_graphicsDevice);
	m_indexBuffer->CreateIndexBuffer(GetIndices());
}
