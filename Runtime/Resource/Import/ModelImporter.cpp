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
#include <assimp/DefaultLogger.hpp>
#include <assimp/ProgressHandler.hpp>
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

// Implement Assimp:Logger
class AssimpLogger : public Assimp::Logger
{
public:
	bool attachStream(LogStream* pStream, unsigned int severity) override	{ return true; }
	bool detatchStream(LogStream* pStream, unsigned int severity) override	{ return true; }

private:
	void OnDebug(const char* message) override
	{
		#ifdef DEBUG
		Directus::Log::m_callerName = "Directus::ModelImporter"; 
		Directus::Log::Write(message, Directus::Log_Type::Log_Info);
		#endif
	}

	void OnInfo(const char* message) override
	{
		Directus::Log::m_callerName = "Directus::ModelImporter";
		Directus::Log::Write(message, Directus::Log_Type::Log_Info);
	}

	void OnWarn(const char* message) override
	{
		Directus::Log::m_callerName = "Directus::ModelImporter";
		Directus::Log::Write(message, Directus::Log_Type::Log_Warning);
	}

	void OnError(const char* message) override
	{
		Directus::Log::m_callerName = "Directus::ModelImporter";
		Directus::Log::Write(message, Directus::Log_Type::Log_Error);
	}
};

// Implement Assimp::ProgressHandler
class AssimpProgress : public Assimp::ProgressHandler
{
public:
	AssimpProgress(const string& filePath)
	{
		m_filePath = filePath;
		m_fileName = Directus::FileSystem::GetFileNameFromFilePath(filePath);

		// Start progress tracking
		Directus::ProgressReport& progress = Directus::ProgressReport::Get();
		progress.Reset(Directus::g_progress_ModelImporter);
		progress.SetIsLoading(Directus::g_progress_ModelImporter, true);
	}

	~AssimpProgress() 
	{
		Directus::ProgressReport::Get().SetIsLoading(Directus::g_progress_ModelImporter, false);
	}

	bool Update(float percentage) override { return true; }

	void UpdateFileRead(int currentStep, int numberOfSteps) override
	{
		Directus::ProgressReport& progress = Directus::ProgressReport::Get();
		progress.SetStatus(Directus::g_progress_ModelImporter, "Loading \"" + m_fileName + "\" from disk...");
		progress.SetJobsDone(Directus::g_progress_ModelImporter, currentStep);
		progress.SetJobCount(Directus::g_progress_ModelImporter, numberOfSteps);
	}

	void UpdatePostProcess(int currentStep, int numberOfSteps) override
	{
		Directus::ProgressReport& progress = Directus::ProgressReport::Get();
		progress.SetStatus(Directus::g_progress_ModelImporter, "Post-Processing \"" + m_fileName + "\"");
		progress.SetJobsDone(Directus::g_progress_ModelImporter, currentStep);
		progress.SetJobCount(Directus::g_progress_ModelImporter, numberOfSteps);
	}

private:
	string m_filePath;
	string m_fileName;
};

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

		inline string TryPathWithMultipleExtensions(const string& filePath)
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

		inline string ValidateTexturePath(const string& originalTexturePath)
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

		inline void ComputeNodeCount(aiNode* node, int* count)
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
		importer.SetProgressHandler(new AssimpProgress(filePath));
		// Enable logging
		DefaultLogger::set(new AssimpLogger());

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

	void ModelImporter::ReadNodeHierarchy(const aiScene* assimpScene, aiNode* assimpNode, shared_ptr<Model>& model, Entity* parentNode, Entity* newNode)
	{
		// Is this the root node?
		if (!assimpNode->mParent || !newNode)
		{
			newNode = m_world->Entity_Create().get();
			model->SetRootentity(newNode->GetPtrShared());

			int jobCount;
			_ModelImporter::ComputeNodeCount(assimpNode, &jobCount);
			ProgressReport::Get().SetJobCount(g_progress_ModelImporter, jobCount);
		}

		//= GET NODE NAME ============================================================
		// Note: In case this is the root node, aiNode.mName will be "RootNode". 
		// To get a more descriptive name we instead get the name from the file path.
		if (assimpNode->mParent)
		{
			string name = assimpNode->mName.C_Str();
			newNode->SetName(name);

			ProgressReport::Get().SetStatus(g_progress_ModelImporter, "Creating entity for " + name);
		}
		else
		{
			string name = FileSystem::GetFileNameNoExtensionFromFilePath(_ModelImporter::m_modelPath);
			newNode->SetName(name);

			ProgressReport::Get().SetStatus(g_progress_ModelImporter, "Creating entity for " + name);
		}
		//============================================================================

		// Set the transform of parentNode as the parent of the newNode's transform
		Transform* parentTrans = parentNode ? parentNode->GetTransform_PtrRaw() : nullptr;
		newNode->GetTransform_PtrRaw()->SetParent(parentTrans);

		// Set the transformation matrix of the Assimp node to the new node
		AssimpHelper::SetentityTransform(assimpNode, newNode);

		// Process all the node's meshes
		for (unsigned int i = 0; i < assimpNode->mNumMeshes; i++)
		{
			Entity* entity		= newNode; // set the current entity
			aiMesh* assimpMesh	= assimpScene->mMeshes[assimpNode->mMeshes[i]]; // get mesh
			string name			= assimpNode->mName.C_Str(); // get name

			// if this node has many meshes, then assign a new entity for each one of them
			if (assimpNode->mNumMeshes > 1)
			{
				entity = m_world->Entity_Create().get(); // create
				entity->GetTransform_PtrRaw()->SetParent(newNode->GetTransform_PtrRaw()); // set parent
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
			ReadNodeHierarchy(assimpScene, assimpNode->mChildren[i], model, newNode, child.get());
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

	void ModelImporter::LoadMesh(const aiScene* assimpScene, aiMesh* assimpMesh, shared_ptr<Model>& model, Entity* parententity)
	{
		if (!model || !assimpMesh || !assimpScene || !parententity)
			return;

		//= MESH ======================================================================
		vector<RHI_Vertex_PosUvNorTan> vertices;
		AssimpMesh_ExtractVertices(assimpMesh, &vertices);

		vector<unsigned int> indices;
		AssimpMesh_ExtractIndices(assimpMesh, &indices);

		// Add the mesh to the model
		unsigned int indexOffset;
		unsigned int vertexOffset;
		model->Geometry_Append(indices, vertices, &indexOffset, &vertexOffset);

		// Add a renderable component to this entity
		auto renderable	= parententity->AddComponent<Renderable>();

		// Set the geometry
		renderable->Geometry_Set(
			parententity->GetName(),
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
			model->AddMaterial(AiMaterialToMaterial(assimpMaterial, model), parententity->GetPtrShared());
		}
		//===================================================================================

		//= BONES ======================================================================
		for (unsigned int boneIndex = 0; boneIndex < assimpMesh->mNumBones; boneIndex++)
		{
			//aiBone* bone = assimpMesh->mBones[boneIndex];
		}
		//==============================================================================
	}

	void ModelImporter::AssimpMesh_ExtractVertices(aiMesh* assimpMesh, vector<RHI_Vertex_PosUvNorTan>* vertices)
	{
		Vector3 position;
		Vector2 uv;
		Vector3 normal;
		Vector3 tangent;

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

			// Texture Coordinates
			if (assimpMesh->HasTextureCoords(0))
			{
				uv = AssimpHelper::ToVector2(aiVector2D(assimpMesh->mTextureCoords[0][vertexIndex].x, assimpMesh->mTextureCoords[0][vertexIndex].y));
			}

			// save the vertex
			vertices->emplace_back(position, uv, normal, tangent);

			// reset the vertex for use in the next loop
			uv			= Vector2::Zero;
			normal		= Vector3::Zero;
			tangent		= Vector3::Zero;
		}
	}

	void ModelImporter::AssimpMesh_ExtractIndices(aiMesh* assimpMesh, vector<unsigned int>* indices)
	{
		indices->reserve(assimpMesh->mNumFaces);

		// Get indices by iterating through each face of the mesh.
		for (unsigned int faceIndex = 0; faceIndex < assimpMesh->mNumFaces; faceIndex++)
		{
			aiFace& face = assimpMesh->mFaces[faceIndex];

			if (face.mNumIndices < 3)
				continue;

			for (unsigned int j = 0; j < face.mNumIndices; j++)
			{
				indices->emplace_back(face.mIndices[j]);
			}
		}
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
		bool isTwoSided = false;
		int result = assimpMaterial->Get(AI_MATKEY_TWOSIDED, isTwoSided);
		if (result == aiReturn_SUCCESS && isTwoSided)
		{
			material->SetCullMode(Cull_None);
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
				if (assimpMaterial->GetTexture(assimpTex, 0, &texturePath, nullptr, nullptr, nullptr, nullptr, nullptr) == AI_SUCCESS)
				{
					auto deducedPath = _ModelImporter::ValidateTexturePath(texturePath.data);
					if (FileSystem::IsSupportedImageFile(deducedPath))
					{
						model->AddTexture(material, engineTex, _ModelImporter::ValidateTexturePath(texturePath.data));
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
