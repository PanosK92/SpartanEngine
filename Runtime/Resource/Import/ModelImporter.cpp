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

//= INCLUDES =============================
#include "ModelImporter.h"
#include <vector>
#include <future>
#include <assimp/scene.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/version.h>
#include "../../Scene/Scene.h"
#include "../../Scene/GameObject.h"
#include "../../Core/Context.h"
#include "../../Components/Transform.h"
#include "../../FileSystem/FileSystem.h"
#include "../../Logging/Log.h"
#include "../../Graphics/Model.h"
#include "../../Graphics/Animation.h"
#include "../../Graphics/Mesh.h"
#include "../../EventSystem/EventSystem.h"
#include "../../Graphics/Material.h"
#include "AssimpHelper.h"
//========================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

// Things for Assimp to do
static auto ppsteps =
aiProcess_CalcTangentSpace |
aiProcess_GenSmoothNormals |
aiProcess_JoinIdenticalVertices |
aiProcess_ImproveCacheLocality |
aiProcess_LimitBoneWeights |
aiProcess_SplitLargeMeshes |
aiProcess_Triangulate |
aiProcess_GenUVCoords |
aiProcess_SortByPType |
aiProcess_FindDegenerates |
aiProcess_FindInvalidData |
aiProcess_FindInstances |
aiProcess_ValidateDataStructure |
aiProcess_OptimizeMeshes |
aiProcess_Debone |
aiProcess_ConvertToLeftHanded;

static int normalSmoothAngle = 80;

namespace Directus
{
	vector<string> materialNames;

	ModelImporter::ModelImporter(Context* context)
	{
		m_context = context;
		m_isLoading = false;
		m_model = nullptr;
		ResetStats();

		// Log version
		int major = aiGetVersionMajor();
		int minor = aiGetVersionMinor();
		int rev = aiGetVersionRevision();
		string version = to_string(major) + "." + to_string(minor) + "." + to_string(rev);
		LOG_INFO("ModelImporter: Assimp " + version);
	}

	ModelImporter::~ModelImporter()
	{

	}

	void ModelImporter::LoadAsync(Model* model, const string& filePath)
	{
		m_model = model;
		m_modelPath = filePath;
		m_context->GetSubsystem<Threading>()->AddTask([this, &model, &filePath]()
		{
			Load(m_model, m_modelPath);
		});
	}

	bool ModelImporter::Load(Model* model, const string& filePath)
	{
		if (!m_context)
		{
			LOG_ERROR("Aborting loading. ModelImporter requires an initialized Context");
			return false;
		}

		m_model = model;
		m_modelPath = filePath;
		m_isLoading = true;

		// Set up an Assimp importer
		Assimp::Importer importer;
		importer.SetPropertyInteger(AI_CONFIG_PP_ICL_PTCACHE_SIZE, 64); // Optimize mesh
		importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_LINE | aiPrimitiveType_POINT); // Remove points and lines.
		importer.SetPropertyInteger(AI_CONFIG_PP_RVC_FLAGS, aiComponent_CAMERAS | aiComponent_LIGHTS); // Remove cameras and lights
		importer.SetPropertyInteger(AI_CONFIG_PP_CT_MAX_SMOOTHING_ANGLE, normalSmoothAngle); // Default is 45, max is 175

		// Read the 3D model file from disk
		m_status = "Loading \"" + FileSystem::GetFileNameFromFilePath(filePath) + "\" from disk...";
		const aiScene* scene = importer.ReadFile(m_modelPath, ppsteps);
		if (!scene)
		{
			LOG_ERROR("ModelImporter: Failed to load \"" + model->GetResourceName() + "\". " + importer.GetErrorString());
			m_isLoading = false;
			return false;
		}

		// Map all the nodes as GameObjects while mentaining hierarchical relationships
		// as well as their properties (meshes, materials, textures etc.).
		ReadNodeHierarchy(model, scene, scene->mRootNode, weak_ptr<GameObject>(), weak_ptr<GameObject>());

		// Load animation (in case there are any)
		ReadAnimations(model, scene);

		// Cleanup
		importer.FreeScene();

		// Stats
		m_isLoading = false;
		ResetStats();

		FIRE_EVENT(EVENT_MODEL_LOADED);

		return true;
	}

	//= PROCESSING ===============================================================================
	void ModelImporter::ReadNodeHierarchy(Model* model, const aiScene* assimpScene, aiNode* assimpNode, const weak_ptr<GameObject>& parentNode, weak_ptr<GameObject>& newNode)
	{
		auto scene = m_context->GetSubsystem<Scene>();

		// Is this the root node?
		if (!assimpNode->mParent || newNode.expired())
		{
			newNode = scene->CreateGameObject();
			model->SetRootGameObject(newNode.lock());

			CalculateNodeCount(assimpNode, m_jobsDone);
		}

		m_jobsTotal++;

		//= GET NODE NAME ============================================================
		// Note: In case this is the root node, aiNode.mName will be "RootNode". 
		// To get a more descriptive name we instead get the name from the file path.
		if (assimpNode->mParent)
		{
			string name = assimpNode->mName.C_Str();
			newNode.lock()->SetName(name);

			m_status = "Processing: " + name;
		}
		else
		{
			string name = FileSystem::GetFileNameNoExtensionFromFilePath(m_modelPath);
			newNode.lock()->SetName(name);

			m_status = "Processing: " + name;
		}
		//============================================================================

		// Set the transform of parentNode as the parent of the newNode's transform
		Transform* parentTrans = !parentNode.expired() ? parentNode.lock()->GetTransform() : nullptr;
		newNode.lock()->GetTransform()->SetParent(parentTrans);

		// Set the transformation matrix of the Assimp node to the new node
		AssimpHelper::SetGameObjectTransform(newNode, assimpNode);

		// Process all the node's meshes
		for (unsigned int i = 0; i < assimpNode->mNumMeshes; i++)
		{
			std::weak_ptr<GameObject> gameobject = newNode; // set the current gameobject
			aiMesh* mesh = assimpScene->mMeshes[assimpNode->mMeshes[i]]; // get mesh
			string name = assimpNode->mName.C_Str(); // get name

			// if this node has many meshes, then assign a new gameobject for each one of them
			if (assimpNode->mNumMeshes > 1)
			{
				gameobject = scene->CreateGameObject(); // create
				gameobject.lock()->GetTransform()->SetParent(newNode.lock()->GetTransform()); // set parent
				name += "_" + to_string(i + 1); // set name
			}

			// Set gameobject name
			gameobject.lock()->SetName(name);

			// Process mesh
			LoadMesh(model, mesh, assimpScene, gameobject);
		}

		// Process children
		for (unsigned int i = 0; i < assimpNode->mNumChildren; i++)
		{
			std::weak_ptr<GameObject> child = scene->CreateGameObject();
			ReadNodeHierarchy(model, assimpScene, assimpNode->mChildren[i], newNode, child);
		}
	}

	void ModelImporter::ReadAnimations(Model* model, const aiScene* scene)
	{
		for (unsigned int i = 0; i < scene->mNumAnimations; i++)
		{
			aiAnimation* assimpAnimation = scene->mAnimations[i];
			shared_ptr<Animation> animation = make_shared<Animation>();

			// Basic properties
			animation->SetName(assimpAnimation->mName.C_Str());
			animation->SetDuration(assimpAnimation->mDuration);
			animation->SetTicksPerSec(assimpAnimation->mTicksPerSecond != 0.0f ? assimpAnimation->mTicksPerSecond : 25.0f);

			// Animation channels
			for (unsigned int j = 0; j > assimpAnimation->mNumChannels; j++)
			{
				aiNodeAnim* assimpNodeAnim = assimpAnimation->mChannels[j];
				AnimationNode animationNode;

				animationNode.name = assimpNodeAnim->mNodeName.C_Str();

				// Position keys
				for (unsigned int k = 0; k < assimpNodeAnim->mNumPositionKeys; k++)
				{
					double time = assimpNodeAnim->mPositionKeys[k].mTime;
					Vector3 value = AssimpHelper::ToVector3(assimpNodeAnim->mPositionKeys[k].mValue);

					animationNode.positionFrames.push_back(KeyVector{ time, value });
				}

				// Rotation keys
				for (unsigned int k = 0; k < assimpNodeAnim->mNumRotationKeys; k++)
				{
					double time = assimpNodeAnim->mPositionKeys[k].mTime;
					Quaternion value = AssimpHelper::ToQuaternion(assimpNodeAnim->mRotationKeys[k].mValue);

					animationNode.rotationFrames.push_back(KeyQuaternion{ time, value });
				}

				// Scaling keys
				for (unsigned int k = 0; k < assimpNodeAnim->mNumScalingKeys; k++)
				{
					double time = assimpNodeAnim->mPositionKeys[k].mTime;
					Vector3 value = AssimpHelper::ToVector3(assimpNodeAnim->mScalingKeys[k].mValue);

					animationNode.scaleFrames.push_back(KeyVector{ time, value });
				}
			}

			model->AddAnimation(animation);
		}
	}

	void ModelImporter::LoadMesh(Model* model, aiMesh* assimpMesh, const aiScene* assimpScene, const weak_ptr<GameObject>& gameobject)
	{
		if (!model || !assimpMesh || !assimpScene || gameobject.expired())
			return;

		//= GEOMETRY =====================================
		// Create a new Mesh
		auto mesh = make_shared<Mesh>(m_context);
		mesh->SetResourceName(assimpMesh->mName.C_Str());

		// Populate mesh with vertices
		LoadAiMeshVertices(assimpMesh, mesh); 

		// Populate mesh with indices
		LoadAiMeshIndices(assimpMesh, mesh); 

		// Add the mesh to the model
		model->AddMesh(mesh, gameobject);
		//================================================

		//= MATERIAL ========================================================================
		auto material = shared_ptr<Material>();
		if (assimpScene->HasMaterials())
		{
			// Get aiMaterial
			aiMaterial* assimpMaterial = assimpScene->mMaterials[assimpMesh->mMaterialIndex];
			// Convert it and add it to the model
			model->AddMaterial(AiMaterialToMaterial(model, assimpMaterial), gameobject);
		}
		//===================================================================================

		//= BONES ======================================================================
		for (unsigned int boneIndex = 0; boneIndex < assimpMesh->mNumBones; boneIndex++)
		{
			//aiBone* bone = assimpMesh->mBones[boneIndex];
		}
		//==============================================================================
	}

	void ModelImporter::LoadAiMeshIndices(aiMesh* assimpMesh, shared_ptr<Mesh> mesh)
	{
		// Get indices by iterating through each face of the mesh.
		for (unsigned int faceIndex = 0; faceIndex < assimpMesh->mNumFaces; faceIndex++)
		{
			aiFace face = assimpMesh->mFaces[faceIndex];

			if (face.mNumIndices < 3)
				continue;

			for (unsigned int j = 0; j < face.mNumIndices; j++)
			{
				mesh->GetIndices().emplace_back(face.mIndices[j]);
			}
		}
	}

	void ModelImporter::LoadAiMeshVertices(aiMesh* assimpMesh, shared_ptr<Mesh> mesh)
	{
		Vector3 position;
		Vector2 uv;
		Vector3 normal;
		Vector3 tangent;
		Vector3 bitangent;

		mesh->GetVertices().reserve(assimpMesh->mNumVertices);

		for (unsigned int vertexIndex = 0; vertexIndex < assimpMesh->mNumVertices; vertexIndex++)
		{
			// Position
			position = AssimpHelper::ToVector3(assimpMesh->mVertices[vertexIndex]);

			// Normal
			if (assimpMesh->mNormals)
			{
				normal = AssimpHelper::ToVector3(assimpMesh->mNormals[vertexIndex]);
			}

			// Tangent
			if (assimpMesh->mTangents)
			{
				tangent = AssimpHelper::ToVector3(assimpMesh->mTangents[vertexIndex]);
			}

			// Bitagent
			if (assimpMesh->mBitangents)
			{
				bitangent = AssimpHelper::ToVector3(assimpMesh->mBitangents[vertexIndex]);
			}

			// Texture Coordinates
			if (assimpMesh->HasTextureCoords(0))
			{
				uv = AssimpHelper::ToVector2(aiVector2D(assimpMesh->mTextureCoords[0][vertexIndex].x, assimpMesh->mTextureCoords[0][vertexIndex].y));
			}

			// save the vertex
			mesh->GetVertices().emplace_back(position, uv, normal, tangent, bitangent);

			// reset the vertex for use in the next loop
			uv = Vector2::Zero;
			normal = Vector3::Zero;
			tangent = Vector3::Zero;
			bitangent = Vector3::Zero;
		}
	}

	shared_ptr<Material> ModelImporter::AiMaterialToMaterial(Model* model, aiMaterial* assimpMaterial)
	{
		if (!model || !assimpMaterial)
		{
			LOG_WARNING("ModelImporter: Can't convert AiMaterial to Material, one of them is null.");
			return nullptr;
		}

		auto material = make_shared<Material>(m_context);

		//= NAME ============================================
		aiString name;
		aiGetMaterialString(assimpMaterial, AI_MATKEY_NAME, &name);
		material->SetResourceName(name.C_Str());
		material->SetModelID(GUIDGenerator::ToUnsignedInt(model->GetResourceName()));

		//= CullMode ===================================================
		// Specifies whether meshes using this material must be rendered 
		// without back face CullMode. 0 for false, !0 for true.
		bool isTwoSided = false;
		int result = assimpMaterial->Get(AI_MATKEY_TWOSIDED, isTwoSided);
		if (result == aiReturn_SUCCESS && isTwoSided)
		{
			material->SetCullMode(CullNone);
		}

		//= DIFFUSE COLOR ===================================================
		aiColor4D colorDiffuse(1.0f, 1.0f, 1.0f, 1.0f);
		aiGetMaterialColor(assimpMaterial, AI_MATKEY_COLOR_DIFFUSE, &colorDiffuse);
		material->SetColorAlbedo(AssimpHelper::ToVector4(colorDiffuse));

		//= OPACITY ==============================================
		aiColor4D opacity(1.0f, 1.0f, 1.0f, 1.0f);
		aiGetMaterialColor(assimpMaterial, AI_MATKEY_OPACITY, &opacity);
		material->SetOpacity(opacity.r);

		//= ALBEDO TEXTURE =============================================================================================================
		aiString texturePath;
		if (assimpMaterial->GetTextureCount(aiTextureType_DIFFUSE) > 0)
		{
			if (assimpMaterial->GetTexture(aiTextureType_DIFFUSE, 0, &texturePath, nullptr, nullptr, nullptr, nullptr, nullptr) == AI_SUCCESS)
			{
				model->AddTexture(material, TextureType_Albedo, ValidateTexturePath(texturePath.data));
				// FIX: materials that have a diffuse texture should not be tinted black/grey
				material->SetColorAlbedo(Vector4(1.0f, 1.0f, 1.0f, 1.0f));
			}
		}

		//= SPECULAR (used as ROUGHNESS) TEXTURE =========================================================================================
		if (assimpMaterial->GetTextureCount(aiTextureType_SHININESS) > 0)
		{
			if (assimpMaterial->GetTexture(aiTextureType_SHININESS, 0, &texturePath, nullptr, nullptr, nullptr, nullptr, nullptr) == AI_SUCCESS)
			{
				model->AddTexture(material, TextureType_Roughness, ValidateTexturePath(texturePath.data));
			}
		}

		//= AMBIENT (used as METALLIC) TEXTURE ========================================================================================
		if (assimpMaterial->GetTextureCount(aiTextureType_AMBIENT) > 0)
		{
			if (assimpMaterial->GetTexture(aiTextureType_AMBIENT, 0, &texturePath, nullptr, nullptr, nullptr, nullptr, nullptr) == AI_SUCCESS)
			{
				model->AddTexture(material, TextureType_Metallic, ValidateTexturePath(texturePath.data));
			}
		}

		//= NORMAL TEXTURE =============================================================================================================
		if (assimpMaterial->GetTextureCount(aiTextureType_NORMALS) > 0)
		{
			if (assimpMaterial->GetTexture(aiTextureType_NORMALS, 0, &texturePath, nullptr, nullptr, nullptr, nullptr, nullptr) == AI_SUCCESS)
			{
				model->AddTexture(material, TextureType_Normal, ValidateTexturePath(texturePath.data));
			}
		}

		//= OCCLUSION TEXTURE ===========================================================================================================
		if (assimpMaterial->GetTextureCount(aiTextureType_LIGHTMAP) > 0)
		{
			if (assimpMaterial->GetTexture(aiTextureType_LIGHTMAP, 0, &texturePath, nullptr, nullptr, nullptr, nullptr, nullptr) == AI_SUCCESS)
			{
				model->AddTexture(material, TextureType_Occlusion, ValidateTexturePath(texturePath.data));
			}
		}

		//= EMISSIVE TEXTURE ============================================================================================================
		if (assimpMaterial->GetTextureCount(aiTextureType_EMISSIVE) > 0)
		{
			if (assimpMaterial->GetTexture(aiTextureType_EMISSIVE, 0, &texturePath, nullptr, nullptr, nullptr, nullptr, nullptr) == AI_SUCCESS)
			{
				model->AddTexture(material, TextureType_Emission, ValidateTexturePath(texturePath.data));
			}
		}
		//= HEIGHT TEXTURE ============================================================================================================
		if (assimpMaterial->GetTextureCount(aiTextureType_HEIGHT) > 0)
		{
			if (assimpMaterial->GetTexture(aiTextureType_HEIGHT, 0, &texturePath, nullptr, nullptr, nullptr, nullptr, nullptr) == AI_SUCCESS)
			{
				model->AddTexture(material, TextureType_Height, ValidateTexturePath(texturePath.data));
			}
		}

		//= MASK TEXTURE ===============================================================================================================
		if (assimpMaterial->GetTextureCount(aiTextureType_OPACITY) > 0)
		{
			if (assimpMaterial->GetTexture(aiTextureType_OPACITY, 0, &texturePath, nullptr, nullptr, nullptr, nullptr, nullptr) == AI_SUCCESS)
			{
				model->AddTexture(material, TextureType_Mask, ValidateTexturePath(texturePath.data));
			}
		}
		return material;
	}
	//============================================================================================

	//= HELPER FUNCTIONS =================================================================================================================================
	string ModelImporter::ValidateTexturePath(const string& originalTexturePath)
	{
		// Models usually return a texture path which is relative to the model's directory.
		// However, to load anything, we'll need an absolute path, so we construct it here.
		string modelDir = FileSystem::GetDirectoryFromFilePath(m_modelPath);
		string fullTexturePath = modelDir + originalTexturePath;

		// 1. Check if the texture path is valid
		if (FileSystem::FileExists(fullTexturePath))
			return fullTexturePath;

		// 2. Check the same texture path as previously but 
		// this time with different file extensions (jpg, png and so on).
		fullTexturePath = TryPathWithMultipleExtensions(fullTexturePath);
		if (FileSystem::FileExists(fullTexturePath))
			return fullTexturePath;

		// At this point we know the provided path is wrong, we will make a few guesses.
		// The most common mistake is that the artist provided a path which is absolute to his computer.

		// 3. Check if the texture is in the same folder as the model
		fullTexturePath = modelDir + FileSystem::GetFileNameFromFilePath(fullTexturePath);
		if (FileSystem::FileExists(fullTexturePath))
			return fullTexturePath;

		// 4. Check the same texture path as previously but 
		// this time with different file extensions (jpg, png and so on).
		fullTexturePath = TryPathWithMultipleExtensions(fullTexturePath);
		if (FileSystem::FileExists(fullTexturePath))
			return fullTexturePath;

		// Give up, no valid texture path was found
		return NOT_ASSIGNED;
	}

	string ModelImporter::TryPathWithMultipleExtensions(const string& filePath)
	{
		// Remove extension
		string filePathNoExt = FileSystem::GetFilePathWithoutExtension(filePath);

		// Check if the file exists using all engine supported extensions
		auto supportedFormats = FileSystem::GetSupportedImageFormats();
		for (unsigned int i = 0; i < supportedFormats.size(); i++)
		{
			string newFilePath = filePathNoExt + supportedFormats[i];
			string newFilePathUpper = filePathNoExt + FileSystem::ConvertToUppercase(supportedFormats[i]);

			if (FileSystem::FileExists(newFilePath))
			{
				return newFilePath;
			}

			if (FileSystem::FileExists(newFilePathUpper))
			{
				return newFilePathUpper;
			}
		}

		return filePath;
	}

	void ModelImporter::CalculateNodeCount(aiNode* node, int& count)
	{
		if (!node)
			return;

		count++;

		// Process children
		for (unsigned int i = 0; i < node->mNumChildren; i++)
		{
			CalculateNodeCount(node->mChildren[i], count);
		}
	}

	void ModelImporter::ResetStats()
	{
		m_status = NOT_ASSIGNED;
		m_jobsDone = 0;
		m_jobsTotal = 0;
	}
}
