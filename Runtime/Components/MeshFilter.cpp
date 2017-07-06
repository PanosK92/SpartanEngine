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

//= INCLUDES ===================================
#include "MeshFilter.h"
#include "Transform.h"
#include "../IO/Serializer.h"
#include "../Core/GameObject.h"
#include "../Logging/Log.h"
#include "../FileSystem/FileSystem.h"
#include "../Resource/ResourceManager.h"
#include "../Math/Vector3.h"
#include "../Graphics/Model.h"
#include "../Graphics/Mesh.h"
#include "../Graphics/D3D11/D3D11VertexBuffer.h"
#include "../Graphics/D3D11/D3D11IndexBuffer.h"
//==============================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	MeshFilter::MeshFilter()
	{
		g_type = "MeshFilter";
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
		Serializer::WriteSTR(!m_mesh.expired() ? m_mesh._Get()->GetName() : (string)DATA_NOT_ASSIGNED);
		Serializer::WriteSTR(!m_mesh.expired() ? m_mesh._Get()->GetID() : (string)DATA_NOT_ASSIGNED);
		Serializer::WriteSTR(!m_mesh.expired() ? m_mesh._Get()->GetModelID() : (string)DATA_NOT_ASSIGNED);
	}

	void MeshFilter::Deserialize()
	{
		m_meshType = (MeshType)Serializer::ReadInt();
		string meshName = Serializer::ReadSTR();
		string meshID = Serializer::ReadSTR();
		string modelID = Serializer::ReadSTR();

		// If the mesh is a engine constructed primitive
		if (m_meshType != Imported)
		{
			// Construct it now
			SetMesh(m_meshType);
			return;
		}

		// ... else load the actual model and try to find the corresponding mesh
		weak_ptr<Model> model = g_context->GetSubsystem<ResourceManager>()->GetResourceByID<Model>(modelID);
		if (model.expired())
		{
			LOG_WARNING("Mesh \"" + meshName + "\" failed to load. Model is not loaded");
			return;
		}

		// Get the mesh
		weak_ptr<Mesh> mesh = model._Get()->GetMeshByID(meshID);
		if (mesh.expired())
		{
			LOG_WARNING("Mesh \"" + meshName + "\" failed to load. Model doesn't contain it");
			return;
		}

		SetMesh(mesh);
	}

	// Sets a mesh from memory
	bool MeshFilter::SetMesh(weak_ptr<Mesh> mesh)
	{
		if (mesh.expired())
		{
			LOG_WARNING("Can't create vertex and index buffers for an expired mesh");
			return false;
		}
		m_mesh = mesh;

		// Re-create the buffers whenever the mesh updates
		m_mesh._Get()->SubscribeToUpdate(bind(&MeshFilter::CreateBuffers, this));

		// Create buffers
		CreateBuffers();

		return true;
	}

	// Sets a default mesh (cube, quad)
	bool MeshFilter::SetMesh(MeshType defaultMesh)
	{
		shared_ptr<Model> model = make_shared<Model>(g_context);
		model->SetRootGameObject(g_gameObject);
		weak_ptr<Mesh> weakMesh;
		vector<VertexPosTexNorTan> vertices;
		vector<unsigned int> indices;

		switch (defaultMesh)
		{
		case Cube:
			CreateCube(vertices, indices);
			model->SetResourceName("Engine_Default_Cube");
			weakMesh = model->AddMesh(g_gameObject.lock()->GetID(), "Cube", vertices, indices);
			m_meshType = Cube;
			break;

		case Quad:
			CreateQuad(vertices, indices);
			model->SetResourceName("Engine_Default_Quad");
			weakMesh = model->AddMesh(g_gameObject.lock()->GetID(), "Quad", vertices, indices);
			m_meshType = Quad;
			break;

		default:
			m_mesh = weak_ptr<Mesh>();
			break;
		}

		// Add the model to the resource manager and set it to the mesh filter
		g_context->GetSubsystem<ResourceManager>()->Add<Model>(model);
		bool result = SetMesh(weakMesh);

		return result;
	}

	// Set the buffers to active in the input assembler so they can be rendered.
	bool MeshFilter::SetBuffers()
	{
		if (!m_vertexBuffer)
		{
			LOG_WARNING("Can't set vertex buffer. Mesh \"" + GetMeshName() + "\" doesn't have an initialized vertex buffer \"" + GetGameObjectName() + "\".");
		}

		if (!m_indexBuffer)
		{
			LOG_WARNING("Can't set index buffer. Mesh \"" + GetMeshName() + "\" doesn't have an initialized index buffer \"" + GetGameObjectName() + "\".");
		}

		if (!m_vertexBuffer || !m_indexBuffer)
			return false;

		m_vertexBuffer->SetIA();
		m_indexBuffer->SetIA();

		// Set the type of primitive that should be rendered from this vertex buffer
		g_context->GetSubsystem<Graphics>()->SetPrimitiveTopology(TriangleList);

		return true;
	}

	Vector3 MeshFilter::GetCenter()
	{
		return !m_mesh.expired() ? m_mesh._Get()->GetCenter() * g_transform->GetWorldTransform() : Vector3::Zero;
	}

	Vector3 MeshFilter::GetBoundingBox()
	{
		return !m_mesh.expired() ? m_mesh._Get()->GetBoundingBox() * g_transform->GetWorldTransform() : Vector3::One;
	}

	float MeshFilter::GetBoundingSphereRadius()
	{
		Vector3 extent = GetBoundingBox();
		return max(max(abs(extent.x), abs(extent.y)), abs(extent.z));
	}

	string MeshFilter::GetMeshName()
	{
		return !m_mesh.expired() ? m_mesh._Get()->GetName() : DATA_NOT_ASSIGNED;
	}

	bool MeshFilter::CreateBuffers()
	{
		auto graphicsDevice = g_context->GetSubsystem<Graphics>();
		if (!graphicsDevice->GetDevice())
		{
			LOG_ERROR("Aborting vertex buffer creation. Graphics device is not present.");
			return false;
		}

		if (m_mesh.expired())
		{
			LOG_ERROR("Aborting vertex buffer creation for \"" + GetGameObjectName() + "\". The mesh has expired.");
			return false;
		}

		m_vertexBuffer.reset();
		m_indexBuffer.reset();

		m_vertexBuffer = make_shared<D3D11VertexBuffer>(graphicsDevice);
		if (!m_vertexBuffer->Create(m_mesh._Get()->GetVertices()))
		{
			LOG_ERROR("Failed to create vertex buffer \"" + GetGameObjectName() + "\".");
			return false;
		}

		m_indexBuffer = make_shared<D3D11IndexBuffer>(graphicsDevice);
		if (!m_indexBuffer->Create(m_mesh._Get()->GetIndices()))
		{
			LOG_ERROR("Failed to create index buffer \"" + GetGameObjectName() + "\".");
			return false;
		}

		return true;
	}

	void MeshFilter::CreateCube(vector<VertexPosTexNorTan>& vertices, vector<unsigned int>& indices)
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
		indices.push_back(0); indices.push_back(1); indices.push_back(2);
		indices.push_back(2); indices.push_back(1); indices.push_back(3);

		// bottom
		indices.push_back(4); indices.push_back(5); indices.push_back(6);
		indices.push_back(6); indices.push_back(5); indices.push_back(7);

		// back
		indices.push_back(10); indices.push_back(9); indices.push_back(8);
		indices.push_back(11); indices.push_back(9); indices.push_back(10);

		// top
		indices.push_back(14); indices.push_back(13); indices.push_back(12);
		indices.push_back(15); indices.push_back(13); indices.push_back(14);

		// left
		indices.push_back(16); indices.push_back(17); indices.push_back(18);
		indices.push_back(18); indices.push_back(17); indices.push_back(19);

		// right
		indices.push_back(22); indices.push_back(21); indices.push_back(20);
		indices.push_back(23); indices.push_back(21); indices.push_back(22);
	}

	void MeshFilter::CreateQuad(vector<VertexPosTexNorTan>& vertices, vector<unsigned int>& indices)
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

	string& MeshFilter::GetGameObjectName()
	{
		static string gameObjName = !g_gameObject.expired() ? g_gameObject._Get()->GetName() : DATA_NOT_ASSIGNED;
		return gameObjName;
	}
}
