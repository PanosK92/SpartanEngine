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
#include "../../Logging/Log.h"
#include "../../IO/FileStream.h"
#include "../../Graphics/Mesh.h"
#include "../../FileSystem/FileSystem.h"
#include "../../Resource/ResourceManager.h"
#include "../../Graphics/GeometryUtility.h"
//=========================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	MeshFilter::MeshFilter(Context* context, GameObject* gameObject, Transform* transform) : IComponent(context, gameObject, transform)
	{
		m_meshType = MeshType_Imported;
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

		if (m_meshType == MeshType_Imported) // If it was an imported mesh, get it from the resource cache
		{
			m_mesh = GetContext()->GetSubsystem<ResourceManager>()->GetResourceByName<Mesh>(meshName);
			if (m_mesh.expired())
			{
				LOG_WARNING("MeshFilter: Failed to load mesh \"" + meshName + "\".");
			}
		}
		else // If it was a standard mesh, reconstruct it
		{
			UseStandardMesh(m_meshType);
		}
	}

	void MeshFilter::SetMesh(const weak_ptr<Mesh>& mesh, bool autoCache /* true */)
	{
		m_mesh = mesh;

		// We do allow for a mesh filter with no mesh
		if (m_mesh.expired())
			return;

		m_mesh = autoCache ? mesh.lock()->Cache<Mesh>() : mesh;
	}

	// Sets a default mesh (cube, quad)
	void MeshFilter::UseStandardMesh(MeshType type)
	{
		m_meshType = type;

		// Create a name for this standard mesh
		string meshName;
		if (type == MeshType_Cube)
		{
			meshName = "Standard_Cube";
		}
		else if (type == MeshType_Quad)
		{
			meshName = "Standard_Quad";
		}
		else if (type == MeshType_Sphere)
		{
			meshName = "Standard_Sphere";
		}
		else if (type == MeshType_Cylinder)
		{
			meshName = "Standard_Cylinder";
		}
		else if (type == MeshType_Cone)
		{
			meshName = "Standard_Cone";
		}

		// Check if this mesh is already loaded, if so, use the existing one
		if (auto existingMesh = GetContext()->GetSubsystem<ResourceManager>()->GetResourceByName<Mesh>(meshName).lock())
		{
			// Cache it and keep a reference
			SetMesh(existingMesh->Cache<Mesh>(), false);
			return;
		}

		// Construct vertices/indices
		vector<VertexPosTexTBN> vertices;
		vector<unsigned int> indices;
		if (type == MeshType_Cube)
		{
			GeometryUtility::CreateCube(&vertices, &indices);
		}
		else if (type == MeshType_Quad)
		{
			GeometryUtility::CreateQuad(&vertices, &indices);
		}
		else if (type == MeshType_Sphere)
		{
			GeometryUtility::CreateSphere(&vertices, &indices);
		}
		else if (type == MeshType_Cylinder)
		{
			GeometryUtility::CreateCylinder(&vertices, &indices);
		}
		else if (type == MeshType_Cone)
		{
			GeometryUtility::CreateCone(&vertices, &indices);
		}

		// Create a file path (in the project directory) for this standard mesh
		string projectStandardAssetDir = GetContext()->GetSubsystem<ResourceManager>()->GetProjectStandardAssetsDirectory();
		FileSystem::CreateDirectory_(projectStandardAssetDir);

		// Create a mesh
		auto mesh = make_shared<Mesh>(GetContext());
		mesh->SetVertices(vertices);
		mesh->SetIndices(indices);
		mesh->SetResourceName(meshName);
		mesh->Construct();

		// Cache it and keep a reference
		SetMesh(mesh->Cache<Mesh>(), false);
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
}
