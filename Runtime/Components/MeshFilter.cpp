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
#include "../IO/StreamIO.h"
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
		Register(ComponentType_MeshFilter);
		m_meshType = Imported;
		m_boundingBox = BoundingBox();
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

	void MeshFilter::OnDisable()
	{

	}

	void MeshFilter::Remove()
	{

	}

	void MeshFilter::Update()
	{

	}

	void MeshFilter::Serialize(StreamIO* stream)
	{
		stream->Write((int)m_meshType);
		stream->Write(!m_mesh.expired() ? m_mesh._Get()->GetName() : (string)NOT_ASSIGNED);
		stream->Write(!m_mesh.expired() ? m_mesh._Get()->GetID() : NOT_ASSIGNED_HASH);
		stream->Write(!m_mesh.expired() ? m_mesh._Get()->GetModelID() : NOT_ASSIGNED_HASH);
	}

	void MeshFilter::Deserialize(StreamIO* stream)
	{
		m_meshType				= (MeshType)stream->ReadInt();
		string meshName			= NOT_ASSIGNED;
		unsigned int meshID		= 0;
		unsigned int modelID	= 0;

		stream->Read(meshName);
		stream->Read(meshID);
		stream->Read(modelID);

		// If the mesh is an engine constructed primitive
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
			LOG_WARNING("MeshFilter: Can't load mesh \"" + meshName + "\". The model it belongs to is expired.");
			return;
		}

		// Get the mesh
		weak_ptr<Mesh> mesh = model._Get()->GetMeshByID(meshID);
		if (mesh.expired())
		{
			LOG_WARNING("MeshFilter: Can't load mesh \"" + meshName + "\". It's not part of the model.");
			return;
		}

		SetMesh(mesh);
	}

	// Sets a mesh from memory
	bool MeshFilter::SetMesh(weak_ptr<Mesh> mesh)
	{
		m_mesh = mesh;

		if (m_mesh.expired())
		{
			m_boundingBox.Undefine();
			LOG_WARNING("MeshFilter: Can't create vertex and index buffers for an expired mesh");
			return false;
		}

		// Re-create the buffers whenever the mesh updates
		m_mesh._Get()->SubscribeToUpdate(bind(&MeshFilter::CreateBuffers, this));

		CreateBuffers();

		m_boundingBox.ComputeFromMesh(m_mesh);

		return true;
	}

	// Sets a default mesh (cube, quad)
	bool MeshFilter::SetMesh(MeshType defaultMesh)
	{
		// Construct vertices/indices
		vector<VertexPosTexTBN> vertices;
		vector<unsigned int> indices;
		if (defaultMesh == Cube)
		{
			CreateCube(vertices, indices);
		}
		else if (defaultMesh == Quad)
		{
			CreateQuad(vertices, indices);
		}

		// Find out some details
		m_meshType = defaultMesh;
		string resourceName = (defaultMesh == Cube) ? "Engine_Default_Cube" : "Engine_Default_Quad";
		string meshName = (defaultMesh == Cube) ? "Cube" : "Quad";

		// Initialize a model that contains this mesh
		shared_ptr<Model> modelShared = make_shared<Model>(g_context);
		modelShared->SetRootGameObject(g_gameObject);
		modelShared->SetResourceName(resourceName);
		string projectDir = g_context->GetSubsystem<ResourceManager>()->GetProjectDirectory();
		modelShared->SetResourceFilePath(projectDir + resourceName + MODEL_EXTENSION);
		modelShared->AddMeshAsNewResource(g_gameObject._Get()->GetID(), meshName, vertices, indices);

		// Add the model to the resource manager and get it as a weak reference. It's important to do that
		// because the resource manager will maintain it's own copy, thus any external references like
		// the local shared model here, will expire when this function goes out of scope.
		weak_ptr<Model> modelWeak = g_context->GetSubsystem<ResourceManager>()->Add<Model>(modelShared);
		auto mesh = modelWeak._Get()->GetMeshByName(meshName);

		return SetMesh(mesh);
	}

	// Set the buffers to active in the input assembler so they can be rendered.
	bool MeshFilter::SetBuffers()
	{
		if (!m_vertexBuffer)
		{
			LOG_WARNING("MeshFilter: Can't set vertex buffer. Mesh \"" + GetMeshName() + "\" doesn't have an initialized vertex buffer \"" + GetGameObjectName() + "\".");
		}

		if (!m_indexBuffer)
		{
			LOG_WARNING("MeshFilter: Can't set index buffer. Mesh \"" + GetMeshName() + "\" doesn't have an initialized index buffer \"" + GetGameObjectName() + "\".");
		}

		if (!m_vertexBuffer || !m_indexBuffer)
			return false;

		m_vertexBuffer->SetIA();
		m_indexBuffer->SetIA();

		// Set the type of primitive that should be rendered from this vertex buffer
		g_context->GetSubsystem<Graphics>()->SetPrimitiveTopology(TriangleList);

		return true;
	}

	const BoundingBox& MeshFilter::GetBoundingBox() const
	{
		return m_boundingBox;
	}

	BoundingBox MeshFilter::GetBoundingBoxTransformed()
	{
		return m_boundingBox.Transformed(g_transform->GetWorldTransform());
	}

	string MeshFilter::GetMeshName()
	{
		return !m_mesh.expired() ? m_mesh._Get()->GetName() : NOT_ASSIGNED;
	}

	bool MeshFilter::CreateBuffers()
	{
		auto graphicsDevice = g_context->GetSubsystem<Graphics>();
		if (!graphicsDevice->GetDevice())
		{
			LOG_ERROR("MeshFilter: Aborting vertex buffer creation. Graphics device is not present.");
			return false;
		}

		if (m_mesh.expired())
		{
			LOG_ERROR("MeshFilter: Aborting vertex buffer creation for \"" + GetGameObjectName() + "\". The mesh has expired.");
			return false;
		}

		m_vertexBuffer.reset();
		m_indexBuffer.reset();

		m_vertexBuffer = make_shared<D3D11VertexBuffer>(graphicsDevice);
		if (!m_vertexBuffer->Create(m_mesh._Get()->GetVertices()))
		{
			LOG_ERROR("MeshFilter: Failed to create vertex buffer \"" + GetGameObjectName() + "\".");
			return false;
		}

		m_indexBuffer = make_shared<D3D11IndexBuffer>(graphicsDevice);
		if (!m_indexBuffer->Create(m_mesh._Get()->GetIndices()))
		{
			LOG_ERROR("MeshFilter: Failed to create index buffer \"" + GetGameObjectName() + "\".");
			return false;
		}

		return true;
	}

	void MeshFilter::CreateCube(vector<VertexPosTexTBN>& vertices, vector<unsigned int>& indices)
	{
		// front
		vertices.push_back({ Vector3(-0.5f, -0.5f, -0.5f), Vector2(0, 1), Vector3(0, 0, -1), Vector3(0, 1, 0), Vector3(1, 0, 0) }); // 0
		vertices.push_back({ Vector3(-0.5f, 0.5f, -0.5f), Vector2(0, 0), Vector3(0, 0, -1), Vector3(0, 1, 0), Vector3(1, 0, 0) }); // 1
		vertices.push_back({ Vector3(0.5f, -0.5f, -0.5f), Vector2(1, 1), Vector3(0, 0, -1), Vector3(0, 1, 0), Vector3(1, 0, 0) }); // 2
		vertices.push_back({ Vector3(0.5f, 0.5f, -0.5f), Vector2(1, 0), Vector3(0, 0, -1), Vector3(0, 1, 0), Vector3(1, 0, 0) }); // 3

		// bottom
		vertices.push_back({ Vector3(-0.5f, -0.5f, 0.5f), Vector2(0, 1), Vector3(0, -1, 0), Vector3(1, 0, 0), Vector3(0, 0, 1) }); // 4
		vertices.push_back({ Vector3(-0.5f, -0.5f, -0.5f), Vector2(0, 0), Vector3(0, -1, 0), Vector3(1, 0, 0), Vector3(0, 0, 1) }); // 5
		vertices.push_back({ Vector3(0.5f, -0.5f, 0.5f), Vector2(1, 1), Vector3(0, -1, 0), Vector3(1, 0, 0), Vector3(0, 0, 1) }); // 6
		vertices.push_back({ Vector3(0.5f, -0.5f, -0.5f), Vector2(1, 0), Vector3(0, -1, 0), Vector3(1, 0, 0), Vector3(0, 0, 1) }); // 7

		// back
		vertices.push_back({ Vector3(-0.5f, -0.5f, 0.5f), Vector2(1, 1), Vector3(0, 0, 1), Vector3(0, 1, 0), Vector3(1, 0, 0) }); // 8
		vertices.push_back({ Vector3(-0.5f, 0.5f, 0.5f), Vector2(1, 0), Vector3(0, 0, 1), Vector3(0, 1, 0), Vector3(1, 0, 0) }); // 9
		vertices.push_back({ Vector3(0.5f, -0.5f, 0.5f), Vector2(0, 1), Vector3(0, 0, 1), Vector3(0, 1, 0), Vector3(1, 0, 0) }); // 10
		vertices.push_back({ Vector3(0.5f, 0.5f, 0.5f), Vector2(0, 0), Vector3(0, 0, 1), Vector3(0, 1, 0), Vector3(1, 0, 0) }); // 11

		// top
		vertices.push_back({ Vector3(-0.5f, 0.5f, 0.5f), Vector2(0, 0), Vector3(0, 1, 0), Vector3(1, 0, 0), Vector3(0, 0, 1) }); // 12
		vertices.push_back({ Vector3(-0.5f, 0.5f, -0.5f), Vector2(0, 1), Vector3(0, 1, 0), Vector3(1, 0, 0), Vector3(0, 0, 1) }); // 13
		vertices.push_back({ Vector3(0.5f, 0.5f, 0.5f), Vector2(1, 0), Vector3(0, 1, 0), Vector3(1, 0, 0), Vector3(0, 0, 1) }); // 14
		vertices.push_back({ Vector3(0.5f, 0.5f, -0.5f), Vector2(1, 1), Vector3(0, 1, 0), Vector3(1, 0, 0), Vector3(0, 0, 1) }); // 15

		// left
		vertices.push_back({ Vector3(-0.5f, -0.5f, 0.5f), Vector2(0, 1), Vector3(-1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1) }); // 16
		vertices.push_back({ Vector3(-0.5f, 0.5f, 0.5f), Vector2(0, 0), Vector3(-1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1) }); // 17
		vertices.push_back({ Vector3(-0.5f, -0.5f, -0.5f), Vector2(1, 1), Vector3(-1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1) }); // 18
		vertices.push_back({ Vector3(-0.5f, 0.5f, -0.5f), Vector2(1, 0), Vector3(-1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1) }); // 19

		// right
		vertices.push_back({ Vector3(0.5f, -0.5f, 0.5f), Vector2(1, 1), Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1) }); // 20
		vertices.push_back({ Vector3(0.5f, 0.5f, 0.5f), Vector2(1, 0), Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1) }); // 21
		vertices.push_back({ Vector3(0.5f, -0.5f, -0.5f), Vector2(0, 1), Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1) }); // 22
		vertices.push_back({ Vector3(0.5f, 0.5f, -0.5f), Vector2(0, 0), Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1) }); // 23

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

	void MeshFilter::CreateQuad(vector<VertexPosTexTBN>& vertices, vector<unsigned int>& indices)
	{
		vertices.push_back({ Vector3(-0.5f, 0.0f, 0.5f),Vector2(0, 0), Vector3(0, 1, 0), Vector3(1, 0, 0), Vector3(0, 0, 1) }); // 0 top-left
		vertices.push_back({ Vector3(0.5f, 0.0f, 0.5f), Vector2(1, 0), Vector3(0, 1, 0), Vector3(1, 0, 0), Vector3(0, 0, 1) }); // 1 top-right
		vertices.push_back({ Vector3(-0.5f, 0.0f, -0.5f), Vector2(0, 1), Vector3(0, 1, 0), Vector3(1, 0, 0), Vector3(0, 0, 1) }); // 2 bottom-left
		vertices.push_back({ Vector3(0.5f, 0.0f, -0.5f),Vector2(1, 1), Vector3(0, 1, 0), Vector3(1, 0, 0), Vector3(0, 0, 1) }); // 3 bottom-right

		indices.push_back(3);
		indices.push_back(2);
		indices.push_back(0);
		indices.push_back(3);
		indices.push_back(0);
		indices.push_back(1);
	}

	string MeshFilter::GetGameObjectName()
	{
		return !g_gameObject.expired() ? g_gameObject._Get()->GetName() : NOT_ASSIGNED;
	}
}
