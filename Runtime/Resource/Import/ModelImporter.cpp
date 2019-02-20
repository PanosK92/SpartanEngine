/*
Copyright(c) 2016-2019 Panos Karabelas

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
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/version.h>
#include "AssimpHelper.h"
#include "../ProgressReport.h"
#include "../../Core/Settings.h"
#include "../../Rendering/Model.h"
#include "../../Rendering/Animation.h"
#include "../../Rendering/Material.h"
#include "../../World/Components/Renderable.h"
//============================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
using namespace Assimp;
//=============================

namespace Directus
{
	namespace _ModelImporter
	{
		static float maxNormalSmoothingAngle	= 80.0f;	// Normals exceeding this limit are not smoothed.
		static float maxTangentSmoothingAngle	= 80.0f;	// Tangents exceeding this limit are not smoothed. Default is 45, max is 175
		std::string m_modelPath;

		// Things for Assimp to do
		static auto flags =
			aiProcess_CalcTangentSpace |
			aiProcess_GenSmoothNormals |
			aiProcess_JoinIdenticalVertices |
			aiProcess_OptimizeMeshes |
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
			aiProcess_Debone |
			aiProcess_ConvertToLeftHanded;
	}

	ModelImporter::ModelImporter(Context* context)
	{
		m_context	= context;
		m_world		= context->GetSubsystem<World>().get();

		// Get version
		int major	= aiGetVersionMajor();
		int minor	= aiGetVersionMinor();
		int rev		= aiGetVersionRevision();
		Settings::Get().m_versionAssimp = to_string(major) + "." + to_string(minor) + "." + to_string(rev);
	}

	bool ModelImporter::Load(shared_ptr<Model> model, const string& filePath)
	{
		if (!m_context)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		_ModelImporter::m_modelPath = filePath;

		// Set up an Assimp importer
		Importer importer;	
		// Set normal smoothing angle
		importer.SetPropertyFloat(AI_CONFIG_PP_GSN_MAX_SMOOTHING_ANGLE, _ModelImporter::maxNormalSmoothingAngle);
		// Set tangent smoothing angle
		importer.SetPropertyFloat(AI_CONFIG_PP_CT_MAX_SMOOTHING_ANGLE, _ModelImporter::maxTangentSmoothingAngle);	
		// Maximum number of triangles in a mesh (before splitting)
		unsigned int triangleLimit = 1000000;
		importer.SetPropertyInteger(AI_CONFIG_PP_SLM_TRIANGLE_LIMIT, triangleLimit);
		// Maximum number of vertices in a mesh (before splitting)
		unsigned int vertexLimit = 1000000;
		importer.SetPropertyInteger(AI_CONFIG_PP_SLM_VERTEX_LIMIT, vertexLimit);
		// Remove points and lines.
		importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_LINE | aiPrimitiveType_POINT);	
		// Remove cameras and lights
		importer.SetPropertyInteger(AI_CONFIG_PP_RVC_FLAGS, aiComponent_CAMERAS | aiComponent_LIGHTS);		
		// Enable progress tracking
		importer.SetPropertyBool(AI_CONFIG_GLOB_MEASURE_TIME, true);
		importer.SetProgressHandler(new AssimpHelper::AssimpProgress(filePath));
		// Enable logging
		DefaultLogger::set(new AssimpHelper::AssimpLogger());

		// Read the 3D model file from disk
		const aiScene* scene = importer.ReadFile(_ModelImporter::m_modelPath, _ModelImporter::flags);
		bool result = scene != nullptr;
		if (result)
		{
			FIRE_EVENT(Event_World_Stop);
			ReadNodeHierarchy(scene, scene->mRootNode, model);
			ReadAnimations(scene, model);
			model->Geometry_Update();
			FIRE_EVENT(Event_World_Start);
		}
		else
		{
			LOGF_ERROR("%s", importer.GetErrorString());
		}

		importer.FreeScene();

		return result;
	}

	void ModelImporter::ReadNodeHierarchy(const aiScene* assimpScene, aiNode* assimpNode, shared_ptr<Model>& model, Entity* parentNode, Entity* newEntity)
	{
		// Is this the root node?
		if (!assimpNode->mParent || !newEntity)
		{
			newEntity = m_world->Entity_Create().get();
			model->SetRootentity(newEntity->GetPtrShared());

			int jobCount;
			AssimpHelper::ComputeNodeCount(assimpNode, &jobCount);
			ProgressReport::Get().SetJobCount(g_progress_ModelImporter, jobCount);
		}

		//= GET NODE NAME ==========================================================================================================================
		// In case this is the root node, aiNode.mName will be "RootNode". 
		// To get a more descriptive name we instead get the name from the file path.
		string name = assimpNode->mParent ? assimpNode->mName.C_Str() : FileSystem::GetFileNameNoExtensionFromFilePath(_ModelImporter::m_modelPath);
		newEntity->SetName(name);
		ProgressReport::Get().SetStatus(g_progress_ModelImporter, "Creating entity for " + name);
		//==========================================================================================================================================

		// Set the transform of parentNode as the parent of the newNode's transform
		Transform* parentTrans = parentNode ? parentNode->GetTransform_PtrRaw() : nullptr;
		newEntity->GetTransform_PtrRaw()->SetParent(parentTrans);

		// Set the transformation matrix of the Assimp node to the new node
		AssimpHelper::SetentityTransform(assimpNode, newEntity);

		// Process all the node's meshes
		for (unsigned int i = 0; i < assimpNode->mNumMeshes; i++)
		{
			Entity* entity		= newEntity; // set the current entity
			aiMesh* assimpMesh	= assimpScene->mMeshes[assimpNode->mMeshes[i]]; // get mesh
			string name			= assimpNode->mName.C_Str(); // get name

			// if this node has many meshes, then assign a new entity for each one of them
			if (assimpNode->mNumMeshes > 1)
			{
				entity = m_world->Entity_Create().get(); // create
				entity->GetTransform_PtrRaw()->SetParent(newEntity->GetTransform_PtrRaw()); // set parent
				name += "_" + to_string(i + 1); // set name
			}

			// Set entity name
			entity->SetName(name);

			// Process mesh
			LoadMesh(assimpScene, assimpMesh, model, entity);
		}

		// Process children
		for (unsigned int i = 0; i < assimpNode->mNumChildren; i++)
		{
			shared_ptr<Entity> child = m_world->Entity_Create();
			ReadNodeHierarchy(assimpScene, assimpNode->mChildren[i], model, newEntity, child.get());
		}

		ProgressReport::Get().IncrementJobsDone(g_progress_ModelImporter);
	}

	void ModelImporter::ReadAnimations(const aiScene* scene, shared_ptr<Model>& model)
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

					animationNode.positionFrames.emplace_back(KeyVector{ time, value });
				}

				// Rotation keys
				for (unsigned int k = 0; k < assimpNodeAnim->mNumRotationKeys; k++)
				{
					double time = assimpNodeAnim->mPositionKeys[k].mTime;
					Quaternion value = AssimpHelper::ToQuaternion(assimpNodeAnim->mRotationKeys[k].mValue);

					animationNode.rotationFrames.emplace_back(KeyQuaternion{ time, value });
				}

				// Scaling keys
				for (unsigned int k = 0; k < assimpNodeAnim->mNumScalingKeys; k++)
				{
					double time = assimpNodeAnim->mPositionKeys[k].mTime;
					Vector3 value = AssimpHelper::ToVector3(assimpNodeAnim->mScalingKeys[k].mValue);

					animationNode.scaleFrames.emplace_back(KeyVector{ time, value });
				}
			}

			model->AddAnimation(animation);
		}
	}

	void ModelImporter::LoadMesh(const aiScene* assimpScene, aiMesh* assimpMesh, shared_ptr<Model>& model, Entity* entity_parent)
	{
		if (!model || !assimpMesh || !assimpScene || !entity_parent)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

		// Vertices
		vector<RHI_Vertex_PosUvNorTan> vertices;
		{
			// Pre-allocate for extra performance
			unsigned int vertexCount = assimpMesh->mNumVertices;
			vertices.reserve(vertexCount);
			vertices.resize(vertexCount);

			for (unsigned int i = 0; i < vertexCount; i++)
			{
				auto& vertex = vertices[i];

				// Position
				const aiVector3D& pos = assimpMesh->mVertices[i];
				vertex.pos[0] = pos.x;
				vertex.pos[1] = pos.y;
				vertex.pos[2] = pos.z;

				// Normal
				if (assimpMesh->mNormals)
				{
					const aiVector3D& normal = assimpMesh->mNormals[i];
					vertex.normal[0] = normal.x;
					vertex.normal[1] = normal.y;
					vertex.normal[2] = normal.z;
				}

				// Tangent
				if (assimpMesh->mTangents)
				{
					const aiVector3D& tangent = assimpMesh->mTangents[i];
					vertex.tangent[0] = tangent.x;
					vertex.tangent[1] = tangent.y;
					vertex.tangent[2] = tangent.z;
				}

				// Texture coordinates
				unsigned int uvChannel = 0;
				if (assimpMesh->HasTextureCoords(uvChannel))
				{
					const aiVector3D& texCoords = assimpMesh->mTextureCoords[uvChannel][i];
					vertex.uv[0] = texCoords.x;
					vertex.uv[1] = texCoords.y;
				}
			}
		}

		// Indices
		vector<unsigned int> indices;
		{
			// Pre-allocate for extra performance
			unsigned int indexCount = assimpMesh->mNumFaces * 3;
			indices.reserve(indexCount);
			indices.resize(indexCount);

			// Get indices by iterating through each face of the mesh.
			for (unsigned int faceIndex = 0; faceIndex < assimpMesh->mNumFaces; faceIndex++)
			{
				// if (aiPrimitiveType_LINE | aiPrimitiveType_POINT) && aiProcess_Triangulate) then (face.mNumIndices == 3)
				aiFace& face				= assimpMesh->mFaces[faceIndex];
				unsigned int indices_index	= (faceIndex * 3);
				indices[indices_index + 0]	= face.mIndices[0];
				indices[indices_index + 1]	= face.mIndices[1];
				indices[indices_index + 2]	= face.mIndices[2];
			}
		}

		// Compute AABB (before doing move operation on vertices)
		BoundingBox aabb = BoundingBox(vertices);

		// Add the mesh to the model
		unsigned int indexOffset;
		unsigned int vertexOffset;
		model->Geometry_Append(move(indices), move(vertices), &indexOffset, &vertexOffset);

		// Add a renderable component to this entity
		auto renderable	= entity_parent->AddComponent<Renderable>();

		// Set the geometry
		renderable->Geometry_Set(
			entity_parent->GetName(),
			indexOffset,
			(unsigned int)indices.size(),
			vertexOffset,
			(unsigned int)vertices.size(),
			move(aabb),
			model
		);

		// Material
		if (assimpScene->HasMaterials())
		{
			// Get aiMaterial
			aiMaterial* assimpMaterial = assimpScene->mMaterials[assimpMesh->mMaterialIndex];
			// Convert it and add it to the model
			model->AddMaterial(AiMaterialToMaterial(assimpMaterial, model), entity_parent->GetPtrShared());
		}

		// Bones
		//for (unsigned int boneIndex = 0; boneIndex < assimpMesh->mNumBones; boneIndex++)
		//{
			//aiBone* bone = assimpMesh->mBones[boneIndex];
		//}
	}

	shared_ptr<Material> ModelImporter::AiMaterialToMaterial(aiMaterial* assimpMaterial, shared_ptr<Model>& model)
	{
		if (!model || !assimpMaterial)
		{
			LOG_WARNING("One of the provided materials is null, can't execute function");
			return nullptr;
		}

		auto material = make_shared<Material>(m_context);

		// NAME
		aiString name;
		aiGetMaterialString(assimpMaterial, AI_MATKEY_NAME, &name);
		material->SetResourceName(name.C_Str());

		// CULL MODE
		// Specifies whether meshes using this material must be rendered 
		// without back face CullMode. 0 for false, !0 for true.
		int isTwoSided		= 0;
		unsigned int max	= 1;
		if (AI_SUCCESS == aiGetMaterialIntegerArray(assimpMaterial, AI_MATKEY_TWOSIDED, &isTwoSided, &max))
		{
			if (isTwoSided != 0)
			{
				material->SetCullMode(Cull_None);
			}
		}

		// DIFFUSE COLOR
		aiColor4D colorDiffuse(1.0f, 1.0f, 1.0f, 1.0f);
		aiGetMaterialColor(assimpMaterial, AI_MATKEY_COLOR_DIFFUSE, &colorDiffuse);
		
		// OPACITY
		aiColor4D opacity(1.0f, 1.0f, 1.0f, 1.0f);
		aiGetMaterialColor(assimpMaterial, AI_MATKEY_OPACITY, &opacity);

		material->SetColorAlbedo(Vector4(colorDiffuse.r, colorDiffuse.g, colorDiffuse.b, opacity.r));

		// TEXTURES
		auto LoadMatTex = [this, &model, &assimpMaterial, &material](aiTextureType assimpTex, TextureType engineTex)
		{
			aiString texturePath;
			if (assimpMaterial->GetTextureCount(assimpTex) > 0)
			{
				if (AI_SUCCESS == assimpMaterial->GetTexture(assimpTex, 0, &texturePath))
				{
					auto deducedPath = AssimpHelper::Texture_ValidatePath(texturePath.data, _ModelImporter::m_modelPath);
					if (FileSystem::IsSupportedImageFile(deducedPath))
					{
						model->AddTexture(material, engineTex, AssimpHelper::Texture_ValidatePath(texturePath.data, _ModelImporter::m_modelPath));
					}

					if (assimpTex == aiTextureType_DIFFUSE)
					{
						// FIX: materials that have a diffuse texture should not be tinted black/gray
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
}
