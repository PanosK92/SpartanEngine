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
#include "../IO/FileStream.h"
#include "../Core/GameObject.h"
#include "../Logging/Log.h"
#include "../FileSystem/FileSystem.h"
#include "../Resource/ResourceManager.h"
#include "../Math/Vector3.h"
#include "../Graphics/Mesh.h"
//======================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	MeshFilter::MeshFilter()
	{
		Register(ComponentType_MeshFilter);
		m_type = MeshType_Custom;
	}

	MeshFilter::~MeshFilter()
	{

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

	void MeshFilter::Serialize(FileStream* stream)
	{
		stream->Write((int)m_type);
		stream->Write(!m_mesh.expired() ? m_mesh._Get()->GetResourceName() : (string)NOT_ASSIGNED);
		stream->Write(!m_mesh.expired() ? m_mesh._Get()->GetResourceID() : NOT_ASSIGNED_HASH);
	}

	void MeshFilter::Deserialize(FileStream* stream)
	{
		m_type					= (MeshType)stream->ReadInt();
		string meshName			= NOT_ASSIGNED;
		unsigned int meshID		= 0;
		stream->Read(&meshName);
		stream->Read(&meshID);

		// Get the mesh from the ResourceManager
		m_mesh = g_context->GetSubsystem<ResourceManager>()->GetResourceByID<Mesh>(meshID);
		if (m_mesh.expired())
		{
			LOG_WARNING("MeshFilter: Failed to load mesh \"" + meshName + "\".");
		}
	}

	// Sets a default mesh (cube, quad)
	void MeshFilter::SetMesh(MeshType type)
	{
		m_type = type;

		// Construct vertices/indices
		vector<VertexPosTexTBN> vertices;
		vector<unsigned int> indices;
		if (type == MeshType_Cube)
		{
			CreateCube(vertices, indices);
		}
		else if (type == MeshType_Quad)
		{
			CreateQuad(vertices, indices);
		}
	
		// Create a name for this standard mesh
		string meshName = (type == MeshType_Cube) ? "Standard_Cube" : "Standard_Quad";

		// Check if this mesh is already loaded, if so, use the existing one
		auto meshExisting = g_context->GetSubsystem<ResourceManager>()->GetResourceByName<Mesh>(meshName);
		if (!meshExisting.expired())
		{
			m_mesh = meshExisting;
			return;
		}

		// Create a file path (in the project directory) for this standard mesh
		string projectDir = g_context->GetSubsystem<ResourceManager>()->GetProjectDirectory();
		string standardAssetDir = projectDir + "Assets//Standard_Assets//";
		FileSystem::CreateDirectory_(standardAssetDir);
		string meshFilePath = standardAssetDir + meshName + MESH_EXTENSION;

		// Create a mesh, save it and add it to the ResourceManager
		auto mesh = make_shared<Mesh>(g_context);
		mesh->SetVertices(vertices);
		mesh->SetIndices(indices);
		mesh->SetResourceName(meshName);
		mesh->SetResourceFilePath(meshFilePath);	
		mesh->SaveToFile(meshFilePath);
		mesh->Construct();
		m_mesh = g_context->GetSubsystem<ResourceManager>()->Add<Mesh>(mesh);
	}

	bool MeshFilter::SetBuffers()
	{
		if (m_mesh.expired())
			return false;

		m_mesh._Get()->SetBuffers();
		return true;
	}

	const BoundingBox& MeshFilter::GetBoundingBox() const
	{
		return !m_mesh.expired() ? m_mesh._Get()->GetBoundingBox() : BoundingBox();
	}

	BoundingBox MeshFilter::GetBoundingBoxTransformed()
	{
		BoundingBox boundingBox = !m_mesh.expired() ? m_mesh._Get()->GetBoundingBox() : BoundingBox();
		return boundingBox.Transformed(g_transform->GetWorldTransform());
	}

	string MeshFilter::GetMeshName()
	{
		return !m_mesh.expired() ? m_mesh._Get()->GetResourceName() : NOT_ASSIGNED;
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
