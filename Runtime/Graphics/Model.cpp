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

		//= RESOURCE INTERFACE ===========
		m_resourceID = GENERATE_GUID;
		m_resourceType = Model_Resource;
		//================================

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
		return engineFormat ? LoadFromEngineFormat(filePath) : LoadFromForeignFormat(filePath);
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
		Serializer::WriteInt((int)m_meshes.size());

		for (const auto& mesh : m_meshes)
		{
			mesh->Serialize();
		}

		Serializer::StopWriting();

		return true;
	}
	//============================================================================================

	std::weak_ptr<Mesh> Model::AddMesh(const std::string& gameObjID, const string& name, vector<VertexPosTexNorTan> vertices, vector<unsigned int> indices)
	{
		// Create a mesh
		auto mesh = make_shared<Mesh>();
		mesh->SetGameObjectID(gameObjID);
		mesh->SetName(name);
		mesh->SetVertices(vertices);
		mesh->SetIndices(indices);
		mesh->Update();

		// Save it
		m_meshes.push_back(mesh);

		return mesh;
	}

	weak_ptr<Mesh> Model::GetMeshByID(const string id)
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

	string Model::CopyFileToLocalDirectory(const string& filePath)
	{
		string textureDestination = m_resourceDirectory + FileSystem::GetFileNameFromFilePath(filePath);
		FileSystem::CopyFileFromTo(filePath, textureDestination);

		return textureDestination;
	}

	void Model::NormalizeScale()
	{
		float normalizedScale = GetNormalizedScale();
		SetScale(normalizedScale);
	}

	void Model::SetScale(float scale) // WHEN THAT HAPPENS I SHOULD ALSO SAVE THE MODEL AGAIN
	{
		for (const auto& mesh : m_meshes)
		{
			mesh->SetScale(scale);
		}
	}

	bool Model::LoadFromEngineFormat(const string& filePath)
	{
		// Deserialize
		if (!Serializer::StartReading(filePath))
			return false;

		m_resourceID = Serializer::ReadSTR();
		m_resourceName = Serializer::ReadSTR();
		m_resourceFilePath = Serializer::ReadSTR();
		int meshCount = Serializer::ReadInt();

		for (int i = 0; i < meshCount; i++)
		{
			auto mesh = make_shared<Mesh>();
			mesh->Deserialize();
			m_meshes.push_back(mesh);
		}

		Serializer::StopReading();

		return true;
	}

	bool Model::LoadFromForeignFormat(const string& filePath)
	{
		// Set some crucial data (Required by ModelImporter)
		m_originalFilePath = filePath;
		m_resourceDirectory = "Assets//" + FileSystem::GetFileNameNoExtensionFromFilePath(m_originalFilePath) + "//"; // Assets/Sponza/
		m_resourceName = FileSystem::GetFileNameFromFilePath(m_originalFilePath); // Sponza.obj
		m_resourceFilePath = m_resourceDirectory + FileSystem::GetFileNameNoExtensionFromFilePath(filePath) + MODEL_EXTENSION; // Assets/Sponza/Sponza.model

		// Create asset directory (if it doesn't exist)
		FileSystem::CreateDirectory_("Assets");
		FileSystem::CreateDirectory_(m_resourceDirectory);
		FileSystem::CreateDirectory_(m_resourceDirectory + "Materials//");

		// Load the model
		if (m_resourceManager->GetModelImporter().lock()->Load(this))
		{
			// Save the model as custom/binary format
			SaveToFile(m_resourceFilePath);
			return true;
		}

		return false;
	}

	float Model::GetNormalizedScale()
	{
		// Find the mesh with the largest bounding box
		auto largestBoundingBoxMesh = GetLargestBoundingBox().lock();

		// Calculate the scale offset
		float scaleOffset = !largestBoundingBoxMesh ? 1.0f : largestBoundingBoxMesh->GetBoundingBox().Length();

		// Return the scale
		return 1.0f / scaleOffset;
	}

	weak_ptr<Mesh> Model::GetLargestBoundingBox()
	{
		if (m_meshes.empty())
			return weak_ptr<Mesh>();

		Vector3 largestBoundingBox = Vector3::Zero;
		weak_ptr<Mesh> largestBoundingBoxMesh = m_meshes.front();

		for (auto mesh : m_meshes)
		{
			if (!mesh)
				continue;

			Vector3 boundingBox = mesh->GetBoundingBox();
			if (boundingBox.Volume() > largestBoundingBox.Volume())
			{
				largestBoundingBox = boundingBox;
				largestBoundingBoxMesh = mesh;
			}
		}

		return largestBoundingBoxMesh;
	}
}