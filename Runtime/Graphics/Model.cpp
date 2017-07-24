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
#include "Model.h"
#include "Mesh.h"
#include "../Core/GameObject.h"
#include "../Core/GUIDGenerator.h"
#include "../Resource/ResourceManager.h"
#include "../Components/MeshFilter.h"
#include "../Components/Transform.h"
#include "../Graphics/Vertex.h"
#include "../IO/Serializer.h"
//======================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	Model::Model(Context* context)
	{
		m_context = context;

		//= RESOURCE INTERFACE =========
		m_resourceID = GENERATE_GUID;
		m_resourceType = Model_Resource;
		//==============================

		m_normalizedScale = 1.0f;

		if (!m_context)
			return;

		m_resourceManager = m_context->GetSubsystem<ResourceManager>();
	}

	Model::~Model()
	{

	}

	//= RESOURCE INTERFACE ====================================================================
	bool Model::LoadFromFile(const string& filePath)
	{
		bool engineFormat = FileSystem::GetExtensionFromFilePath(filePath) == MODEL_EXTENSION;
		bool success = engineFormat ? LoadFromEngineFormat(filePath) : LoadFromForeignFormat(filePath);

		return success;
	}

	bool Model::SaveToFile(const string& filePath)
	{
		string savePath = filePath;
		if (filePath == RESOURCE_SAVE)
		{
			savePath = m_resourceFilePath;
		}

		if (!Serializer::StartWriting(savePath))
			return false;

		Serializer::WriteSTR(m_resourceID);
		Serializer::WriteSTR(m_resourceName);
		Serializer::WriteSTR(m_resourceFilePath);
		Serializer::WriteFloat(m_normalizedScale);
		Serializer::WriteInt((int)m_meshes.size());

		for (const auto& mesh : m_meshes)
		{
			mesh->Serialize();
		}

		Serializer::StopWriting();

		return true;
	}
	//============================================================================================

	weak_ptr<Mesh> Model::AddMesh(const string& gameObjID, const string& name, vector<VertexPosTexNorTan> vertices, vector<unsigned int> indices)
	{
		// Create a mesh
		auto mesh = make_shared<Mesh>();
		mesh->SetModelID(m_resourceID);
		mesh->SetGameObjectID(gameObjID);
		mesh->SetName(name);
		mesh->SetVertices(vertices);
		mesh->SetIndices(indices);

		AddMesh(mesh);

		return mesh;
	}

	weak_ptr<Mesh> Model::GetMeshByID(const string& id)
	{
		for (const auto& mesh : m_meshes)
		{
			if (mesh->GetID() == id)
			{
				return mesh;
			}
		}

		return weak_ptr<Mesh>();
	}

	weak_ptr<Mesh> Model::GetMeshByName(const string& name)
	{
		for (const auto& mesh : m_meshes)
		{
			if (mesh->GetName() == name)
			{
				return mesh;
			}
		}

		return weak_ptr<Mesh>();
	}

	string Model::CopyFileToLocalDirectory(const string& from)
	{
		string textureDestination = GetResourceDirectory() + FileSystem::GetFileNameFromFilePath(from);
		FileSystem::CopyFileFromTo(from, textureDestination);

		return textureDestination;
	}

	float Model::GetBoundingSphereRadius()
	{
		Vector3 extent = m_boundingBox.GetHalfSize().Absolute();
		return Max(Max(extent.x, extent.y), extent.z);
	}

	void Model::AddMesh(shared_ptr<Mesh> mesh)
	{
		if (!mesh)
			return;

		// Updates mesh bounding box, center, min, max etc.
		mesh->Update();

		// Calculate the bounding box of the model as well
		ComputeDimensions();

		// Save it
		m_meshes.push_back(mesh);
	}

	bool Model::LoadFromEngineFormat(const string& filePath)
	{
		// Deserialize
		if (!Serializer::StartReading(filePath))
			return false;

		m_resourceID = Serializer::ReadSTR();
		m_resourceName = Serializer::ReadSTR();
		m_resourceFilePath = Serializer::ReadSTR();
		m_normalizedScale = Serializer::ReadFloat();
		int meshCount = Serializer::ReadInt();

		for (int i = 0; i < meshCount; i++)
		{
			auto mesh = make_shared<Mesh>();
			mesh->Deserialize();
			AddMesh(mesh);
		}

		Serializer::StopReading();

		return true;
	}

	bool Model::LoadFromForeignFormat(const string& filePath)
	{
		// Set some crucial data (Required by ModelImporter)
		string dir = "Assets//" + FileSystem::GetFileNameNoExtensionFromFilePath(filePath) + "//"; // Assets/Sponza/
		m_resourceFilePath = dir + FileSystem::GetFileNameNoExtensionFromFilePath(filePath) + MODEL_EXTENSION; // Assets/Sponza/Sponza.model
		m_resourceName = FileSystem::GetFileNameNoExtensionFromFilePath(filePath); // Sponza
		
		// Create asset directory (if it doesn't exist)
		FileSystem::CreateDirectory_(dir + "Materials//");
		FileSystem::CreateDirectory_(dir + "Shaders//");

		// Load the model
		if (m_resourceManager->GetModelImporter()._Get()->Load(this, filePath))
		{
			// Set the normalized scale to the root GameObject's transform
			m_normalizedScale = ComputeNormalizeScale();
			m_rootGameObj._Get()->GetComponent<Transform>()->SetScale(m_normalizedScale);
			m_rootGameObj._Get()->GetComponent<Transform>()->UpdateTransform();

			// Save the model as custom/binary format
			SaveToFile(m_resourceFilePath);

			return true;
		}

		return false;
	}

	void Model::SetScale(float scale)
	{
		for (const auto& mesh : m_meshes)
		{
			mesh->SetScale(scale);
		}
	}

	float Model::ComputeNormalizeScale()
	{
		// Find the mesh with the largest bounding box
		auto largestBoundingBoxMesh = ComputeLargestBoundingBox().lock();

		// Calculate the scale offset
		float scaleOffset = !largestBoundingBoxMesh ? 1.0f : largestBoundingBoxMesh->GetBoundingBox().GetHalfSize().Length();

		// Return the scale
		return 1.0f / scaleOffset;
	}

	weak_ptr<Mesh> Model::ComputeLargestBoundingBox()
	{
		if (m_meshes.empty())
			return weak_ptr<Mesh>();

		Vector3 largestBoundingBox = Vector3::Zero;
		weak_ptr<Mesh> largestBoundingBoxMesh = m_meshes.front();

		for (auto& mesh : m_meshes)
		{
			if (!mesh)
				continue;

			Vector3 boundingBox = mesh->GetBoundingBox().GetHalfSize();
			if (boundingBox.Volume() > largestBoundingBox.Volume())
			{
				largestBoundingBox = boundingBox;
				largestBoundingBoxMesh = mesh;
			}
		}

		return largestBoundingBoxMesh;
	}

	void Model::ComputeDimensions()
	{
		for (auto& mesh : m_meshes)
		{
			if (!m_boundingBox.Defined())
			{
				m_boundingBox.ComputeFromMesh(mesh);
			}

			m_boundingBox.Merge(mesh->GetBoundingBox());
		}
	}
}
