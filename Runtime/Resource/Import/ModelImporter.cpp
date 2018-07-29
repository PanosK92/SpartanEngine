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

//= INCLUDES =================================
#include "ModelImporter.h"
#include <vector>
#include <assimp/scene.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/version.h>
#include "AssimpHelper.h"
#include "../../Core/Context.h"
#include "../../Core/Settings.h"
#include "../../Core/EventSystem.h"
#include "../../FileSystem/FileSystem.h"
#include "../../Logging/Log.h"
#include "../../Rendering/Model.h"
#include "../../Rendering/Animation.h"
#include "../../Rendering/Mesh.h"
#include "../../Rendering/Material.h"
#include "../../Scene/Scene.h"
#include "../../Scene/Actor.h"
#include "../../Scene/Components/Transform.h"
#include "../../Scene/Components/Renderable.h"
#include "../ProgressReport.h"
#include "../../RHI/IRHI_Device.h"
#include "../../RHI/RHI_Texture.h"
//============================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	namespace AssimpSettings
	{
		// Things for Assimp to do
		static auto g_postProcessSteps =
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

		static int g_normalSmoothAngle = 45; // Default is 45, max is 175
	}


	ModelImporter::ModelImporter(Context* context)
	{
		m_context = context;
		m_model = nullptr;

		// Get version
		int major = aiGetVersionMajor();
		int minor = aiGetVersionMinor();
		int rev = aiGetVersionRevision();
		Settings::Get().m_versionAssimp = to_string(major) + "." + to_string(minor) + "." + to_string(rev);
	}

	ModelImporter::~ModelImporter()
	{

	}

	bool ModelImporter::Load(Model* model, const string& filePath)
	{
		if (!m_context)
		{
			LOG_ERROR("ModelImporter::Load: Uninitialized context");
			return false;
		}

		m_model = model;
		m_modelPath = filePath;

		// Set up an Assimp importer
		Assimp::Importer importer;
		importer.SetPropertyInteger(AI_CONFIG_PP_ICL_PTCACHE_SIZE, 64); // Optimize mesh
		importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_LINE | aiPrimitiveType_POINT); // Remove points and lines.
		importer.SetPropertyInteger(AI_CONFIG_PP_RVC_FLAGS, aiComponent_CAMERAS | aiComponent_LIGHTS); // Remove cameras and lights
		importer.SetPropertyInteger(AI_CONFIG_PP_CT_MAX_SMOOTHING_ANGLE, AssimpSettings::g_normalSmoothAngle); 

		// Read the 3D model file from disk
		ProgressReport::Get().Reset(g_progress_ModelImporter);
		ProgressReport::Get().SetStatus(g_progress_ModelImporter, "Loading \"" + FileSystem::GetFileNameFromFilePath(filePath) + "\" from disk...");
		const aiScene* scene = importer.ReadFile(m_modelPath, AssimpSettings::g_postProcessSteps);
		if (!scene)
		{
			LOGF_ERROR("ModelImporter::Load:  Failed to load \"%s\". %s", model->GetResourceName().c_str(), importer.GetErrorString());
			ProgressReport::Get().SetIsLoading(g_progress_ModelImporter, false);
			return false;
		}

		// Map all the nodes as actors while maintaining hierarchical relationships
		// as well as their properties (meshes, materials, textures etc.).
		ReadNodeHierarchy(model, scene, scene->mRootNode);

		// Load animation (in case there are any)
		ReadAnimations(model, scene);

		model->Geometry_Update();

		// Cleanup
		importer.FreeScene();

		// Stats
		ProgressReport::Get().SetIsLoading(g_progress_ModelImporter, false);

		FIRE_EVENT(EVENT_MODEL_LOADED);

		return true;
	}

	//= PROCESSING ===============================================================================
	void ModelImporter::ReadNodeHierarchy(Model* model, const aiScene* assimpScene, aiNode* assimpNode, const weak_ptr<Actor> parentNode, weak_ptr<Actor> newNode)
	{
		auto scene = m_context->GetSubsystem<Scene>();

		// Is this the root node?
		if (!assimpNode->mParent || newNode.expired())
		{
			newNode = scene->Actor_CreateAdd();
			model->SetRootactor(newNode.lock());

			int jobCount;
			ComputeNodeCount(assimpNode, &jobCount);
			ProgressReport::Get().SetJobCount(g_progress_ModelImporter, jobCount);
		}

		//= GET NODE NAME ============================================================
		// Note: In case this is the root node, aiNode.mName will be "RootNode". 
		// To get a more descriptive name we instead get the name from the file path.
		if (assimpNode->mParent)
		{
			string name = assimpNode->mName.C_Str();
			newNode.lock()->SetName(name);

			ProgressReport::Get().SetStatus(g_progress_ModelImporter, "Processing: " + name);
		}
		else
		{
			string name = FileSystem::GetFileNameNoExtensionFromFilePath(m_modelPath);
			newNode.lock()->SetName(name);

			ProgressReport::Get().SetStatus(g_progress_ModelImporter, "Processing: " + name);
		}
		//============================================================================

		// Set the transform of parentNode as the parent of the newNode's transform
		Transform* parentTrans = !parentNode.expired() ? parentNode.lock()->GetTransform_PtrRaw() : nullptr;
		newNode.lock()->GetTransform_PtrRaw()->SetParent(parentTrans);

		// Set the transformation matrix of the Assimp node to the new node
		AssimpHelper::SetActorTransform(newNode, assimpNode);

		// Process all the node's meshes
		for (unsigned int i = 0; i < assimpNode->mNumMeshes; i++)
		{
			weak_ptr<Actor> actor = newNode; // set the current actor
			aiMesh* mesh = assimpScene->mMeshes[assimpNode->mMeshes[i]]; // get mesh
			string name = assimpNode->mName.C_Str(); // get name

			// if this node has many meshes, then assign a new actor for each one of them
			if (assimpNode->mNumMeshes > 1)
			{
				actor = scene->Actor_CreateAdd(); // create
				actor.lock()->GetTransform_PtrRaw()->SetParent(newNode.lock()->GetTransform_PtrRaw()); // set parent
				name += "_" + to_string(i + 1); // set name
			}

			// Set actor name
			actor.lock()->SetName(name);

			// Process mesh
			LoadMesh(model, mesh, assimpScene, actor);
		}

		// Process children
		for (unsigned int i = 0; i < assimpNode->mNumChildren; i++)
		{
			weak_ptr<Actor> child = scene->Actor_CreateAdd();
			ReadNodeHierarchy(model, assimpScene, assimpNode->mChildren[i], newNode, child);
		}

		ProgressReport::Get().JobDone(g_progress_ModelImporter);
	}

	void ModelImporter::ReadAnimations(Model* model, const aiScene* scene)
	{
		for (unsigned int i = 0; i < scene->mNumAnimations; i++)
		{
			aiAnimation* assimpAnimation = scene->mAnimations[i];
			shared_ptr<Animation> animation = make_shared<Animation>(m_context);

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

	void ModelImporter::LoadMesh(Model* model, aiMesh* assimpMesh, const aiScene* assimpScene, const weak_ptr<Actor>& parentActor)
	{
		if (!model || !assimpMesh || !assimpScene || parentActor.expired())
			return;

		//= MESH ======================================================================
		vector<RHI_Vertex_PosUVTBN> vertices;
		AssimpMesh_ExtractVertices(assimpMesh, &vertices);

		vector<unsigned int> indices;
		AssimpMesh_ExtractIndices(assimpMesh, &indices);

		// Add the mesh to the model
		unsigned int indexOffset;
		unsigned int vertexOffset;
		model->Geometry_Append(indices, vertices, &indexOffset, &vertexOffset);

		// Add a renderable component to this Actor
		auto actorShared	= parentActor.lock();
		auto renderable			= actorShared->AddComponent<Renderable>().lock();

		// Set the geometry
		renderable->Geometry_Set(
			actorShared->GetName(),
			indexOffset,
			(unsigned int)indices.size(),
			vertexOffset,
			(unsigned int)vertices.size(),
			BoundingBox(vertices),
			model
		);
		//=============================================================================

		//= MATERIAL ========================================================================
		auto material = shared_ptr<Material>();
		if (assimpScene->HasMaterials())
		{
			// Get aiMaterial
			aiMaterial* assimpMaterial = assimpScene->mMaterials[assimpMesh->mMaterialIndex];
			// Convert it and add it to the model
			model->AddMaterial(AiMaterialToMaterial(model, assimpMaterial), parentActor);
		}
		//===================================================================================

		//= BONES ======================================================================
		for (unsigned int boneIndex = 0; boneIndex < assimpMesh->mNumBones; boneIndex++)
		{
			//aiBone* bone = assimpMesh->mBones[boneIndex];
		}
		//==============================================================================
	}

	void ModelImporter::AssimpMesh_ExtractVertices(aiMesh* assimpMesh, vector<RHI_Vertex_PosUVTBN>* vertices)
	{
		Vector3 position;
		Vector2 uv;
		Vector3 normal;
		Vector3 tangent;
		Vector3 bitangent;

		vertices->reserve(assimpMesh->mNumVertices);

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
			vertices->emplace_back(position, uv, normal, tangent, bitangent);

			// reset the vertex for use in the next loop
			uv			= Vector2::Zero;
			normal		= Vector3::Zero;
			tangent		= Vector3::Zero;
			bitangent	= Vector3::Zero;
		}
	}

	void ModelImporter::AssimpMesh_ExtractIndices(aiMesh* assimpMesh, vector<unsigned int>* indices)
	{
		// Get indices by iterating through each face of the mesh.
		for (unsigned int faceIndex = 0; faceIndex < assimpMesh->mNumFaces; faceIndex++)
		{
			aiFace face = assimpMesh->mFaces[faceIndex];

			if (face.mNumIndices < 3)
				continue;

			for (unsigned int j = 0; j < face.mNumIndices; j++)
			{
				indices->emplace_back(face.mIndices[j]);
			}
		}
	}

	shared_ptr<Material> ModelImporter::AiMaterialToMaterial(Model* model, aiMaterial* assimpMaterial)
	{
		if (!model || !assimpMaterial)
		{
			LOG_WARNING("ModelImporter::AiMaterialToMaterial(): One of the provided materials is null, can't execute function");
			return nullptr;
		}

		auto material = make_shared<Material>(m_context);

		// NAME
		aiString name;
		aiGetMaterialString(assimpMaterial, AI_MATKEY_NAME, &name);
		material->SetResourceName(name.C_Str());
		material->SetModelID(GUIDGenerator::ToUnsignedInt(model->GetResourceName()));

		// CULL MODE
		// Specifies whether meshes using this material must be rendered 
		// without back face CullMode. 0 for false, !0 for true.
		bool isTwoSided = false;
		int result = assimpMaterial->Get(AI_MATKEY_TWOSIDED, isTwoSided);
		if (result == aiReturn_SUCCESS && isTwoSided)
		{
			material->SetCullMode(Cull_None);
		}

		// DIFFUSE COLOR
		aiColor4D colorDiffuse(1.0f, 1.0f, 1.0f, 1.0f);
		aiGetMaterialColor(assimpMaterial, AI_MATKEY_COLOR_DIFFUSE, &colorDiffuse);
		material->SetColorAlbedo(AssimpHelper::ToVector4(colorDiffuse));

		// OPACITY
		aiColor4D opacity(1.0f, 1.0f, 1.0f, 1.0f);
		aiGetMaterialColor(assimpMaterial, AI_MATKEY_OPACITY, &opacity);
		material->SetOpacity(opacity.r);

		// TEXTURES
		auto LoadMatTex = [this, &model, &assimpMaterial, &material](aiTextureType assimpTex, TextureType engineTex)
		{
			aiString texturePath;
			if (assimpMaterial->GetTextureCount(assimpTex) > 0)
			{
				if (assimpMaterial->GetTexture(assimpTex, 0, &texturePath, nullptr, nullptr, nullptr, nullptr, nullptr) == AI_SUCCESS)
				{
					auto deducedPath = ValidateTexturePath(texturePath.data);
					if (FileSystem::IsSupportedImageFile(deducedPath))
					{
						model->AddTexture(material, engineTex, ValidateTexturePath(texturePath.data));
					}

					if (assimpTex == aiTextureType_DIFFUSE)
					{
						// FIX: materials that have a diffuse texture should not be tinted black/grey
						material->SetColorAlbedo(Vector4::One);
					}
				}
			}
		};

		LoadMatTex(aiTextureType_DIFFUSE,	TextureType_Albedo);
		LoadMatTex(aiTextureType_SHININESS,	TextureType_Roughness); // Specular as roughness
		LoadMatTex(aiTextureType_AMBIENT,	TextureType_Metallic);	// Ambient as metallic
		LoadMatTex(aiTextureType_NORMALS,	TextureType_Normal);
		LoadMatTex(aiTextureType_LIGHTMAP,	TextureType_Occlusion);
		LoadMatTex(aiTextureType_EMISSIVE,	TextureType_Emission);
		LoadMatTex(aiTextureType_LIGHTMAP,	TextureType_Occlusion);
		LoadMatTex(aiTextureType_HEIGHT,	TextureType_Height);
		LoadMatTex(aiTextureType_OPACITY,	TextureType_Mask);

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

	void ModelImporter::ComputeNodeCount(aiNode* node, int* count)
	{
		if (!node)
			return;

		(*count)++;

		// Process children
		for (unsigned int i = 0; i < node->mNumChildren; i++)
		{
			ComputeNodeCount(node->mChildren[i], count);
		}
	}
}
