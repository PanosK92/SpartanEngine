/*
Copyright(c) 2016-2018 Panos Karabelas

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

//= INCLUDES ==============================
#include "MeshFilter.h"
#include "Transform.h"
#include "../GameObject.h"
#include "../../Logging/Log.h"
#include "../../Math/Vector3.h"
#include "../../IO/FileStream.h"
#include "../../Graphics/Mesh.h"
#include "../../FileSystem/FileSystem.h"
#include "../../Resource/ResourceManager.h"
//=========================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	MeshFilter::MeshFilter()
	{
		m_meshType = MeshType_Custom;
	}

	MeshFilter::~MeshFilter()
	{

	}

	void MeshFilter::Serialize(FileStream* stream)
	{
		stream->Write((int)m_meshType);
		stream->Write(!m_mesh.expired() ? m_mesh.lock()->GetResourceName() : (string)NOT_ASSIGNED);
	}

	void MeshFilter::Deserialize(FileStream* stream)
	{
		m_meshType		= (MeshType)stream->ReadInt();
		string meshName	= NOT_ASSIGNED;
		stream->Read(&meshName);

		// Get the mesh from the ResourceManager
		m_mesh = GetContext()->GetSubsystem<ResourceManager>()->GetResourceByName<Mesh>(meshName);
		if (m_mesh.expired())
		{
			LOG_WARNING("MeshFilter: Failed to load mesh \"" + meshName + "\".");
		}
	}

	// Sets a default mesh (cube, quad)
	void MeshFilter::SetMesh(MeshType type)
	{
		m_meshType = type;

		// Create a name for this standard mesh
		string meshName = (type == MeshType_Cube) ? "Standard_Cube" : "Standard_Quad";

		// Check if this mesh is already loaded, if so, use the existing one
		auto meshExisting = GetContext()->GetSubsystem<ResourceManager>()->GetResourceByName<Mesh>(meshName);
		if (!meshExisting.expired())
		{
			m_mesh = meshExisting;
			return;
		}

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

		// Create a file path (in the project directory) for this standard mesh
		string projectStandardAssetDir = GetContext()->GetSubsystem<ResourceManager>()->GetProjectStandardAssetsDirectory();
		FileSystem::CreateDirectory_(projectStandardAssetDir);
		string meshFilePath = projectStandardAssetDir + meshName + MESH_EXTENSION;

		// Create a mesh, save it and add it to the ResourceManager
		auto mesh = make_shared<Mesh>(GetContext());
		mesh->SetVertices(vertices);
		mesh->SetIndices(indices);
		mesh->SetResourceName(meshName);
		mesh->SetResourceFilePath(meshFilePath);	
		mesh->SaveToFile(meshFilePath);
		mesh->Construct();
		m_mesh = GetContext()->GetSubsystem<ResourceManager>()->Add<Mesh>(mesh);
	}

	bool MeshFilter::SetBuffers()
	{
		if (m_mesh.expired())
			return false;

		m_mesh.lock()->SetBuffers();
		return true;
	}

	const BoundingBox& MeshFilter::GetBoundingBox() const
	{
		return !m_mesh.expired() ? m_mesh.lock()->GetBoundingBox() : BoundingBox();
	}

	BoundingBox MeshFilter::GetBoundingBoxTransformed()
	{
		BoundingBox boundingBox = !m_mesh.expired() ? m_mesh.lock()->GetBoundingBox() : BoundingBox();
		return boundingBox.Transformed(GetTransform()->GetWorldTransform());
	}

	string MeshFilter::GetMeshName()
	{
		return !m_mesh.expired() ? m_mesh.lock()->GetResourceName() : NOT_ASSIGNED;
	}

	void MeshFilter::CreateCube(vector<VertexPosTexTBN>& vertices, vector<unsigned int>& indices)
	{
		// front
		vertices.emplace_back(Vector3(-0.5f, -0.5f, -0.5f),	Vector2(0, 1), Vector3(0, 0, -1), Vector3(0, 1, 0), Vector3(1, 0, 0));
		vertices.emplace_back(Vector3(-0.5f, 0.5f, -0.5f),	Vector2(0, 0), Vector3(0, 0, -1), Vector3(0, 1, 0), Vector3(1, 0, 0));
		vertices.emplace_back(Vector3(0.5f, -0.5f, -0.5f),	Vector2(1, 1), Vector3(0, 0, -1), Vector3(0, 1, 0), Vector3(1, 0, 0));
		vertices.emplace_back(Vector3(0.5f, 0.5f, -0.5f),	Vector2(1, 0), Vector3(0, 0, -1), Vector3(0, 1, 0), Vector3(1, 0, 0));

		// bottom
		vertices.emplace_back(Vector3(-0.5f, -0.5f, 0.5f),	Vector2(0, 1), Vector3(0, -1, 0), Vector3(1, 0, 0), Vector3(0, 0, 1));
		vertices.emplace_back(Vector3(-0.5f, -0.5f, -0.5f),	Vector2(0, 0), Vector3(0, -1, 0), Vector3(1, 0, 0), Vector3(0, 0, 1));
		vertices.emplace_back(Vector3(0.5f, -0.5f, 0.5f),	Vector2(1, 1), Vector3(0, -1, 0), Vector3(1, 0, 0), Vector3(0, 0, 1));
		vertices.emplace_back(Vector3(0.5f, -0.5f, -0.5f),	Vector2(1, 0), Vector3(0, -1, 0), Vector3(1, 0, 0), Vector3(0, 0, 1));

		// back
		vertices.emplace_back(Vector3(-0.5f, -0.5f, 0.5f),	Vector2(1, 1), Vector3(0, 0, 1), Vector3(0, 1, 0), Vector3(1, 0, 0)); 
		vertices.emplace_back(Vector3(-0.5f, 0.5f, 0.5f),	Vector2(1, 0), Vector3(0, 0, 1), Vector3(0, 1, 0), Vector3(1, 0, 0)); 
		vertices.emplace_back(Vector3(0.5f, -0.5f, 0.5f),	Vector2(0, 1), Vector3(0, 0, 1), Vector3(0, 1, 0), Vector3(1, 0, 0)); 
		vertices.emplace_back(Vector3(0.5f, 0.5f, 0.5f),	Vector2(0, 0), Vector3(0, 0, 1), Vector3(0, 1, 0), Vector3(1, 0, 0)); 

		// top
		vertices.emplace_back(Vector3(-0.5f, 0.5f, 0.5f),	Vector2(0, 0), Vector3(0, 1, 0), Vector3(1, 0, 0), Vector3(0, 0, 1)); 
		vertices.emplace_back(Vector3(-0.5f, 0.5f, -0.5f),	Vector2(0, 1), Vector3(0, 1, 0), Vector3(1, 0, 0), Vector3(0, 0, 1)); 
		vertices.emplace_back(Vector3(0.5f, 0.5f, 0.5f),	Vector2(1, 0), Vector3(0, 1, 0), Vector3(1, 0, 0), Vector3(0, 0, 1)); 
		vertices.emplace_back(Vector3(0.5f, 0.5f, -0.5f),	Vector2(1, 1), Vector3(0, 1, 0), Vector3(1, 0, 0), Vector3(0, 0, 1)); 

		// left
		vertices.emplace_back(Vector3(-0.5f, -0.5f, 0.5f),	Vector2(0, 1), Vector3(-1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1));
		vertices.emplace_back(Vector3(-0.5f, 0.5f, 0.5f),	Vector2(0, 0), Vector3(-1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1));
		vertices.emplace_back(Vector3(-0.5f, -0.5f, -0.5f),	Vector2(1, 1), Vector3(-1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1));
		vertices.emplace_back(Vector3(-0.5f, 0.5f, -0.5f),	Vector2(1, 0), Vector3(-1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1));

		// right
		vertices.emplace_back(Vector3(0.5f, -0.5f, 0.5f),	Vector2(1, 1), Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1));
		vertices.emplace_back(Vector3(0.5f, 0.5f, 0.5f),	Vector2(1, 0), Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1));
		vertices.emplace_back(Vector3(0.5f, -0.5f, -0.5f),	Vector2(0, 1), Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1));
		vertices.emplace_back(Vector3(0.5f, 0.5f, -0.5f),	Vector2(0, 0), Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1));

		// front
		indices.emplace_back(0); indices.emplace_back(1); indices.emplace_back(2);
		indices.emplace_back(2); indices.emplace_back(1); indices.emplace_back(3);

		// bottom
		indices.emplace_back(4); indices.emplace_back(5); indices.emplace_back(6);
		indices.emplace_back(6); indices.emplace_back(5); indices.emplace_back(7);

		// back
		indices.emplace_back(10); indices.emplace_back(9); indices.emplace_back(8);
		indices.emplace_back(11); indices.emplace_back(9); indices.emplace_back(10);

		// top
		indices.emplace_back(14); indices.emplace_back(13); indices.emplace_back(12);
		indices.emplace_back(15); indices.emplace_back(13); indices.emplace_back(14);

		// left
		indices.emplace_back(16); indices.emplace_back(17); indices.emplace_back(18);
		indices.emplace_back(18); indices.emplace_back(17); indices.emplace_back(19);

		// right
		indices.emplace_back(22); indices.emplace_back(21); indices.emplace_back(20);
		indices.emplace_back(23); indices.emplace_back(21); indices.emplace_back(22);
	}

	void MeshFilter::CreateQuad(vector<VertexPosTexTBN>& vertices, vector<unsigned int>& indices)
	{
		vertices.emplace_back(Vector3(-0.5f, 0.0f, 0.5f),	Vector2(0, 0), Vector3(0, 1, 0), Vector3(1, 0, 0), Vector3(0, 0, 1)); // 0 top-left
		vertices.emplace_back(Vector3(0.5f, 0.0f, 0.5f),	Vector2(1, 0), Vector3(0, 1, 0), Vector3(1, 0, 0), Vector3(0, 0, 1)); // 1 top-right
		vertices.emplace_back(Vector3(-0.5f, 0.0f, -0.5f),	Vector2(0, 1), Vector3(0, 1, 0), Vector3(1, 0, 0), Vector3(0, 0, 1)); // 2 bottom-left
		vertices.emplace_back(Vector3(0.5f, 0.0f, -0.5f),	Vector2(1, 1), Vector3(0, 1, 0), Vector3(1, 0, 0), Vector3(0, 0, 1)); // 3 bottom-right

		indices.emplace_back(3);
		indices.emplace_back(2);
		indices.emplace_back(0);
		indices.emplace_back(3);
		indices.emplace_back(0);
		indices.emplace_back(1);
	}
}
